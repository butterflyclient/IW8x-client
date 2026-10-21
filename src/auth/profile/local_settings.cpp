#include "auth/profile/local_settings.h"
#include "network/profile/settings_profile.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_stats.h"
#include "auth/profile/challenge_engine.h"
#include "network/profile/profile_store.h"
#include "engine/safemem.h"
#include "engine/game.h"
#include "common/console.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
namespace local_settings
{
	namespace
	{
		using settings120::Json;
#include "auth/profile/settings_bindings.inc"
		uintptr_t base = 0;
		bool installed = false, loaded = false, ready = false, pending = false, conflict = false, applying = false;
		uint64_t owner = 0, epoch = 0, serial = 0, revision = 0, next = 0;
		std::wstring origin;
		std::filesystem::path path;
		Json acknowledged = nullptr, upload = nullptr, latest = nullptr, baseline = nullptr, choiceValues = nullptr;
		std::string token;
		int choice = 0;
		std::atomic<char (*)(int)> originalRead{ nullptr };
		std::atomic<void (*)()> originalMode{ nullptr };

		struct Value
		{
			int type = 0, pad = 0;

			union
			{
				int number;
				float real;
				const char* text;
				uint64_t bits = 0;
			};
		};

		static_assert(sizeof(Value) == 16);

		template <class F>
		F native(uintptr_t rva)
		{
			return reinterpret_cast<F>(base + rva);
		}

		bool main()
		{
			return native<bool (*)()>(game::kSysIsMainThreadRVA)();
		}

		bool frontend()
		{
			return native<bool (*)()>(game::kIsInFrontEndRVA)();
		}

		bool capture(Json& values)
		{
			values = Json::object();
			for (const auto& field : settings120::fields)
			{
				Value v;
				native<Value* (*)(Value*, int, const char*)>(0x15D6D00)(&v, 0, field.name);
				if (v.type != int(field.type)) return false;
				Json value;
				switch (field.type)
				{
				case 1:
				case 2:
					value = int(uint8_t(v.number));
					break;
				case 3:
					value = int(int16_t(v.number));
					break;
				case 4:
				case 8:
					value = v.number;
					break;
				case 5:
					value = v.real;
					break;
				case 6: {
					std::string s(field.size, '\0');
					if (!v.text || !safemem::ReadBytes(v.text, s.data(), s.size())) return false;
					const auto end = s.find('\0');
					if (end == s.npos) return false;
					s.resize(end);
					value = std::move(s);
					break;
				}
				default:
					return false;
				}
				if (!settings120::ValueValid(field, value)) return false;
				values[field.name] = std::move(value);
			}
			return true;
		}

		void aliases(const Json& values)
		{
			for (const auto& field : settings120::fields)
				if (field.alias) native<void (*)(int, const char*, float)>(0x15D8BD0)(0, field.name, values.at(field.name).get<float>());
		}

		bool restore(const Json& values)
		{
			if (!settings120::Valid(values) || !main()) return false;
			applying = true;
			for (const auto& field : settings120::fields)
			{
				Value v;
				v.type = int(field.type);
				const auto& data = values.at(field.name);
				if (field.type == 6)
					v.text = data.get_ref<const std::string&>().c_str();
				else if (field.type == 5)
					v.real = data.get<float>();
				else
					v.number = data.get<int>();
				native<uintptr_t (*)(void*, int, const Value*)>(0x15DA1D0)(reinterpret_cast<void*>(base + 0xEF28AE0), field.index, &v);
			}
			native<void (*)(int)>(0x15D5BA0)(0);
			aliases(values);
			applying = false;
			return true;
		}

		// TU19 reads once synchronously and again through its native asynchronous
		// completion thunk. Hook the shared reader, not just LoginProfile's return.
		char read_profile(int controller)
		{
			Json before;
			const bool preserve = controller == 0 && !applying && ready && main() && frontend_backend::Authenticated() && capture(before);
			const char result = originalRead.load()(controller);
			if (preserve) restore(before);
			return result;
		}

		void update_mode()
		{
			uint16_t modes = 0;
			Json before;
			const bool preserve = ready && !applying && main() && frontend_backend::Authenticated() &&
				safemem::ReadBytes(reinterpret_cast<void*>(base + 0xC5244E0), &modes, sizeof(modes)) &&
				uint8_t(modes) != uint8_t(modes >> 8) && capture(before);
			originalMode.load()();
			if (preserve && native<uint8_t (*)()>(game::kComGameModeGetActiveGameModeRVA)() != uint8_t(modes)) aliases(before);
		}

		bool atomic_impl(const std::filesystem::path& target, const Json& data)
		{
			try
			{
				std::filesystem::create_directories(target.parent_path());
				const auto payload = data.dump();
				const auto bytes = Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump();
				const auto tmp = target.wstring() + L".tmp";
				HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE) return false;
				DWORD count = 0;
				const bool ok =
					WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file);
				CloseHandle(file);
				if (!ok) return false;
				if (std::filesystem::exists(target))
					return ReplaceFileW(target.c_str(), tmp.c_str(), (target.wstring() + L".bak").c_str(), 0, nullptr, nullptr) != FALSE;
				return MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			catch (...)
			{
				return false;
			}
		}

		Json journal(uint64_t rev, const Json& ack, const Json& sending, const std::string& key, const Json& current)
		{
			return { { "schema", 1 }, { "owner", std::to_string(owner) }, { "revision", rev }, { "ack", ack }, { "pending", sending },
				{ "token", key }, { "latest", current } };
		}

		bool persist()
		{
			return atomic_impl(path, journal(revision, acknowledged, upload, token, latest));
		}

		bool read_journal(const std::filesystem::path& file, Json& data)
		{
			try
			{
				if (std::filesystem::file_size(file) > 65536) return false;
				std::ifstream stream(file);
				Json envelope;
				stream >> envelope;
				const auto payload = envelope.at("payload").get<std::string>();
				if (envelope.at("checksum") != std::to_string(challenges::Hash(payload))) return false;
				data = Json::parse(payload);
				if (data.at("schema") != 1 || data.at("owner") != std::to_string(owner) || !settings120::Revision(data.at("revision")) ||
					!settings120::Valid(data.at("latest")))
					return false;
				if (!data.at("ack").is_null() && !settings120::Valid(data.at("ack"))) return false;
				if ((data.at("revision").get<uint64_t>() != 0) == data.at("ack").is_null()) return false;
				return data.at("pending").is_null()
					? data.at("token") == ""
					: settings120::Valid(data.at("pending")) && chat::Hex(data.at("token").get<std::string>(), 32);
			}
			catch (...)
			{
				return false;
			}
		}

		bool load_impl()
		{
			try
			{
				if (!capture(baseline)) return false;
				const auto backup = std::filesystem::path(path.wstring() + L".bak");
				Json data;
				if (!std::filesystem::exists(path) && !std::filesystem::exists(backup))
				{
					latest = baseline;
					loaded = true;
					return true;
				}
				if (!read_journal(path, data))
				{
					if (!read_journal(backup, data))
					{
						conflict = true;
						return false;
					}
					if (std::filesystem::exists(path) &&
						!MoveFileExW(path.c_str(), (path.wstring() + L".rejected." + std::to_wstring(GetTickCount64())).c_str(),
							MOVEFILE_WRITE_THROUGH))
					{
						conflict = true;
						return false;
					}
				}
				revision = data.at("revision");
				acknowledged = data.at("ack");
				upload = data.at("pending");
				token = data.at("token");
				latest = data.at("latest");
				// This per-owner/provider journal is the client's durable preference
				// copy, including edits newer than the last successful server ACK.
				if (!restore(latest)) return false;
				loaded = ready = true;
				return true;
			}
			catch (...)
			{
				conflict = true;
				return false;
			}
		}

		bool new_upload(const Json& values)
		{
			std::array<uint8_t, 16> random{};
			if (BCryptGenRandom(nullptr, random.data(), ULONG(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
			const auto key = profile_store::Encode(random);
			if (!atomic_impl(path, journal(revision, acknowledged, values, key, latest))) return false;
			upload = values;
			token = key;
			return true;
		}
	}

	hook::Status Install(uintptr_t target)
	{
		if (installed) return hook::Status::Installed;
		base = target;
		const auto matches = [&](uintptr_t rva, const auto& expected)
		{
			std::array<uint8_t, sizeof(expected)> actual{};
			const bool ok = safemem::ReadBytes(reinterpret_cast<void*>(base + rva), actual.data(), actual.size()) &&
				!memcmp(actual.data(), expected, actual.size());
			if (!ok)
			{
				static uintptr_t reported = 0;
				if (reported != rva)
				{
					reported = rva;
					LOG_ERR(
						"Settings", "native byte guard mismatch RVA=0x%llX size=%zu", static_cast<unsigned long long>(rva), actual.size());
				}
			}
			return ok;
		};
		if (!matches(0x15D6D00, kGetBytes) || !matches(0x15DA1D0, kWriteBytes) || !matches(0x15D8BD0, kAliasBytes) ||
			!matches(0x15D5BA0, kApplyBytes) || !matches(game::kSysIsMainThreadRVA, game::kSysIsMainThreadPrologue) ||
			!matches(game::kIsInFrontEndRVA, game::kIsInFrontEndPrologue) ||
			!matches(game::kComGameModeGetActiveGameModeRVA, game::kComGameModeGetActiveGameModePrologue))
			return hook::Status::NotReady;

		struct Descriptor
		{
			const char* name;
			uint64_t offset;
			int type, size;
			float initial, minimum, maximum;
			int pad, bit, tail;
		};

		static_assert(sizeof(Descriptor) == 48 && offsetof(Descriptor, bit) == 40);
		for (const auto& field : settings120::fields)
		{
			Descriptor actual{};
			char name[128]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x461C620 + 48 * field.index), &actual, sizeof(actual)) ||
				actual.offset != field.offset || actual.type != field.type || actual.size != field.size || actual.bit != field.bit ||
				!actual.name || !safemem::ReadBytes(actual.name, name, strlen(field.name) + 1) || strcmp(name, field.name))
			{
				static unsigned reported = ~0u;
				if (reported != field.index)
				{
					reported = field.index;
					LOG_ERR("Settings", "native descriptor mismatch index=%u name=%s", field.index, field.name);
				}
				return hook::Status::NotReady;
			}
		}
		hook::Batch hooks;
		hooks.Add(reinterpret_cast<void*>(base + 0x15D7A20), read_profile, kReadBytes, sizeof(kReadBytes), originalRead);
		hooks.Add(reinterpret_cast<void*>(base + 0x10C8A30), update_mode, kModeBytes, sizeof(kModeBytes), originalMode);
		const auto result = hooks.Commit();
		installed = result == hook::Status::Installed;
		return result;
	}

	void Pump()
	{
		if (!installed || !main() || !frontend_backend::Required()) return;
		const auto currentOrigin = frontend_backend::Origin();
		const auto id = offlineauth::GetIdentity().xuid;
		if (currentOrigin != origin || id != owner)
		{
			origin = currentOrigin;
			owner = id;
			++epoch;
			loaded = ready = pending = conflict = false;
			revision = next = 0;
			choice = 0;
			acknowledged = upload = latest = baseline = choiceValues = nullptr;
			token.clear();
			path = std::filesystem::path(L"players") /
				(L"settings_sync_" + std::to_wstring(owner) + L"_" +
					std::to_wstring(
						challenges::Hash(std::string(reinterpret_cast<const char*>(origin.data()), origin.size() * sizeof(wchar_t)))) +
					L".json");
		}
		if (!id || !frontend_backend::Authenticated() || !local_stats::RemoteReady() || !frontend() || conflict || GetTickCount64() < next)
			return;
		next = GetTickCount64() + 500;
		if (!loaded && !load_impl()) return;
		Json current;
		if (!capture(current)) return;
		if (current != latest)
		{
			if (!atomic_impl(path, journal(revision, acknowledged, upload, token, current))) return;
			latest = current;
		}
		if (pending) return;
		const bool writing = ready;
		if (writing && upload.is_null())
		{
			if (current == acknowledged) return;
			if (!new_upload(current)) return;
		}
		const auto generation = epoch;
		const auto nonce = "settings:" + std::to_string(++serial);
		const auto requestedOrigin = origin;
		Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "settings" }, { "nonce", nonce },
			{ "action", writing ? "save" : "fetch" } };
		if (writing) request.update({ { "revision", revision }, { "values", upload }, { "token", token } });
		pending = true;
		if (!social_runtime::SubmitFrontend(request.dump(),
				[generation, nonce, requestedOrigin, writing](unsigned status, std::string bytes)
				{
					if (epoch != generation || frontend_backend::Origin() != requestedOrigin) return;
					pending = false;
					try
					{
						const auto reply = Json::parse(bytes);
						if (!frontend_backend::Authenticated() || !frontend_service::Validate(reply, std::to_string(owner), nonce))
							throw std::runtime_error("envelope");
						if (status == 409)
						{
							conflict = true;
							ready = false;
							LOG_WARN("Settings120", "revision conflict; use Client Settings recovery");
							return;
						}
						if (status != 200) throw std::runtime_error("request");
						const auto& data = reply.at("settings");
						if (!settings120::Revision(data.at("revision"))) throw std::runtime_error("revision");
						const auto rev = data.at("revision").get<uint64_t>();
						if (writing)
						{
							if (rev != revision + 1 || data.at("token") != token) throw std::runtime_error("acknowledgement");
							if (!atomic_impl(path, journal(rev, upload, nullptr, {}, latest))) throw std::runtime_error("ack journal");
							acknowledged = upload;
							upload = nullptr;
							token.clear();
							revision = rev;
							return;
						}
						const auto& values = data.at("values");
						if (rev ? !settings120::Valid(values) : !values.is_null()) throw std::runtime_error("values");
						Json live;
						if (!frontend() || !capture(live)) throw std::runtime_error("not frontend");
						if (live != (choice ? choiceValues : baseline))
						{
							conflict = true;
							return;
						}
						if (choice)
						{
							const auto archive = path.wstring() + L".conflict." + std::to_wstring(GetTickCount64());
							if (!atomic_impl(
									archive, Json{ { "local", live }, { "server", values }, { "pending", upload }, { "revision", rev } }))
								throw std::runtime_error("archive");
						}
						if (!rev || choice == 2)
						{
							if (choice == 1 && !rev)
							{
								conflict = true;
								return;
							}
							if (!atomic_impl(path, journal(rev, values, nullptr, {}, live)))
								throw std::runtime_error("local import journal");
							revision = rev;
							acknowledged = values;
							latest = live;
							upload = nullptr;
							token.clear();
							ready = true;
							choice = 0;
							return;
						}
						if (!atomic_impl(path, journal(rev, values, nullptr, {}, values)) || !restore(values))
							throw std::runtime_error("restore");
						revision = rev;
						acknowledged = latest = values;
						upload = nullptr;
						token.clear();
						ready = true;
						choice = 0;
						LOG_INFO("Settings120", "restored %zu TU19 preferences revision=%llu", std::size(settings120::fields), rev);
					}
					catch (const std::exception& error)
					{
						next = GetTickCount64() + 2000;
						LOG_WARN("Settings120", "sync deferred http=%u reason=%s", status, error.what());
					}
				}))
			pending = false;
	}

	std::string Status()
	{
		return conflict ? "conflict" : !installed ? "unavailable" : pending ? "syncing" : ready ? "ready" : "connecting";
	}

	bool Resolve(bool keepLocal)
	{
		if (!installed || !main() || !frontend() || pending || !conflict || !frontend_backend::Authenticated()) return false;
		Json live;
		if (!capture(live)) return false;
		choice = keepLocal ? 2 : 1;
		choiceValues = live;
		baseline = live;
		latest = live;
		conflict = false;
		loaded = true;
		ready = false;
		next = 0;
		return true;
	}
}
