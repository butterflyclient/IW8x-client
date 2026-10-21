#include "auth/profile/local_stats.h"

#include "engine/addresses.hpp"

#include "auth/profile/stats_file.h"

#include "common/console.h"

#include "engine/safemem.h"
#include "engine/game.h"

#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "network/profile/profile_store.h"
#include "auth/profile/local_playercard.h"
#include "auth/profile/challenge_engine.h"
#include <bcrypt.h>

#include <filesystem>
#include <fstream>
#include <string>
namespace local_stats
{
	namespace
	{
		// Replay 1.20.4 ABI verified in replay_profile_{persistence,options,codec}.json.
		struct Context
		{
			uint8_t* buffer;
			int size;
			int pad;
			const uint8_t* definition;
			void* callback;
			void* userdata;
			bool obfuscated;
			uint8_t padding[3];
			uint32_t seed;
		};

		static_assert(sizeof(Context) == 48 && offsetof(Context, seed) == 44);
		uintptr_t g_base = 0;
		int g_source = 0;
		uint64_t g_xuid = 0;
		std::filesystem::path g_root, g_path;
		stats_file::Header g_header;
		bool g_ready = false, g_writable = true;
		ULONGLONG g_nextPoll = 0;
		std::vector<uint8_t> g_lastSaved;
		std::string g_saved_encoding;
		bool remoteReady = false, remotePending = false, remoteConflict = false, remoteImport = false;
		uint64_t remoteRevision = 0, remoteEpoch = 0, remoteSerial = 0, remoteNext = 0;
		std::wstring remoteOrigin;
		std::string downloaded, upload, uploadToken, acknowledged;
		std::vector<uint8_t> loadBaseline;
		size_t uploadOffset = 0;
		bool journalLoaded = false;
		int resolution = 0;
		std::string selectedLocal;
		std::filesystem::path journalPath;

		const std::string& saved_encoding()
		{
			// The two-second snapshot pump owns changes to g_lastSaved. Encoding
			// its 64 KB every render frame needlessly stalls an already synced client.
			if (g_saved_encoding.empty()) g_saved_encoding = profile_store::Encode(g_lastSaved);
			return g_saved_encoding;
		}

		template <class F>
		F Native(uintptr_t rva)
		{
			return reinterpret_cast<F>(g_base + rva);
		}

		bool BindingsMatch()
		{
			std::array<uint8_t, sizeof(game::kIsInFrontEndPrologue)> frontendBytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + game::kIsInFrontEndRVA), frontendBytes.data(), frontendBytes.size()) ||
				memcmp(frontendBytes.data(), game::kIsInFrontEndPrologue, frontendBytes.size()))
				return false;

			struct Binding
			{
				uintptr_t rva;
				std::array<uint8_t, 16> bytes;
			};

			const Binding bindings[] = {
				{ 0x15c7940, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 } },
				{ 0x2051750, { 0xe9, 0x5b, 0x26, 0, 0, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
				{ 0x2052150, { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x41, 0x56, 0x48, 0x81, 0xec, 0x70, 0x01, 0, 0 } },
				{ 0x2055710, { 0x0f, 0xb6, 0x51, 0x59, 0x33, 0xc0, 0x84, 0xd2, 0x74, 0x08, 0x38, 0x05, 0x34, 0x93, 0x9f, 0x03 } },
				{ 0x2052ee0, { 0xe9, 0x5b, 0x2a, 0, 0, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
				{ 0x12a3ee0, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48 } },
				{ 0x12a2860, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0x80, 0 } }
			};
			for (const auto& binding : bindings)
			{
				std::array<uint8_t, 16> bytes{};
				if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + binding.rva), bytes.data(), bytes.size()) ||
					bytes != binding.bytes)
				{
					LOG_ERR("Stats", "Replay stats binding mismatch at RVA %llX", static_cast<unsigned long long>(binding.rva));
					return false;
				}
			}
			return true;
		}

		bool GetContext(int group, Context& context)
		{
			return Native<bool (*)(Context*, int, int, int)>(0x15c7940)(&context, 0, g_source, group);
		}

		bool ContextForCopy(uint8_t* buffer, const stats_file::Group& group, const uint8_t* definition, Context& context)
		{
			return Native<bool (*)(void*, int, const void*, Context*, void*, void*)>(0x2051750)(
				buffer, static_cast<int>(group.size), definition, &context, nullptr, nullptr);
		}
		enum class ReadResult
		{
			Missing,
			Invalid,
			Valid
		};

		bool ValidNative(std::vector<uint8_t>& data)
		{
			if (!stats_file::Validate(data, g_header)) return false;
			size_t offset = sizeof(stats_file::Header);
			for (int group = 0; group < 6; ++group)
			{
				Context live{}, copy{};
				if (!GetContext(group, live) || !ContextForCopy(data.data() + offset, g_header.groups[group], live.definition, copy) ||
					!Native<bool (*)(void*, const void*)>(0x2052150)(copy.buffer, copy.definition))
					return false;
				offset += g_header.groups[group].size;
			}
			return true;
		}

		ReadResult Read(const std::filesystem::path& path, std::vector<uint8_t>& data)
		{
			std::error_code error;
			if (!std::filesystem::exists(path, error) && !error) return ReadResult::Missing;
			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			const auto size = stream.tellg();
			if (!stream || size < static_cast<std::streamoff>(sizeof(stats_file::Header)) ||
				size > static_cast<std::streamoff>(stats_file::kMaxSize))
				return ReadResult::Invalid;
			data.resize(static_cast<size_t>(size));
			stream.seekg(0);
			if (!stream.read(reinterpret_cast<char*>(data.data()), size) || !stats_file::Validate(data, g_header))
				return ReadResult::Invalid;
			size_t offset = sizeof(stats_file::Header);
			for (int group = 0; group < 6; ++group)
			{
				Context live{}, copy{};
				if (!GetContext(group, live) || !ContextForCopy(data.data() + offset, g_header.groups[group], live.definition, copy) ||
					!Native<bool (*)(void*, const void*)>(0x2052150)(copy.buffer, copy.definition))
					return ReadResult::Invalid;
				offset += g_header.groups[group].size;
			}
			return ReadResult::Valid;
		}

		bool Snapshot(std::vector<uint8_t>& data)
		{
			size_t size = sizeof(stats_file::Header);
			for (const auto& group : g_header.groups)
				size += group.size;
			data.resize(size);
			memcpy(data.data(), &g_header, sizeof(g_header));
			size_t offset = sizeof(stats_file::Header);
			for (int group = 0; group < 6; ++group)
			{
				Context live{}, copy{};
				const auto fail = [&](const char* reason)
				{
					static ULONGLONG next = 0;
					const auto now = GetTickCount64();
					if (now >= next)
					{
						next = now + 5000;
						LOG_WARN("Profile", "snapshot blocked group=%d stage=%s size=%d expected=%u obfuscated=%d", group, reason,
							live.size, g_header.groups[group].size, int(live.obfuscated));
					}
					return false;
				};
				if (!GetContext(group, live) || live.size != static_cast<int>(g_header.groups[group].size)) return fail("context");
				auto bytes = std::span<uint8_t>(data).subspan(offset, live.size);
				if (!safemem::ReadBytes(live.buffer, bytes.data(), bytes.size())) return fail("buffer");
				if (live.obfuscated)
				{
					const int headerBits = Native<int (*)(const void*)>(0x2055710)(live.definition);
					if (headerBits < 0 || headerBits % 8 || headerBits / 8 > live.size) return fail("header");
					stats_file::Decode(bytes, reinterpret_cast<uintptr_t>(live.buffer), headerBits / 8, live.seed);
				}
				if (!ContextForCopy(bytes.data(), g_header.groups[group], live.definition, copy)) return fail("copy context");
				// Writes the native DDL checksum into the isolated copy; callback is null.
				Native<void (*)(Context*)>(0x2052ee0)(&copy);
				if (!Native<bool (*)(void*, const void*)>(0x2052150)(copy.buffer, copy.definition)) return fail("checksum");
				offset += bytes.size();
			}
			const uint32_t crc = stats_file::Checksum(data);
			memcpy(data.data() + offsetof(stats_file::Header, checksum), &crc, sizeof(crc));
			return true;
		}

		bool Save(const std::vector<uint8_t>& data)
		{
			std::error_code error;
			std::filesystem::create_directories(g_root, error);
			if (error) return false;
			const auto temporary = g_path.wstring() + L".tmp";
			HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool flushed = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
				written == data.size() && FlushFileBuffers(file);
			CloseHandle(file);
			bool saved = false;
			if (flushed)
			{
				const auto backup = g_path.wstring() + L".bak";
				if (GetFileAttributesW(g_path.c_str()) != INVALID_FILE_ATTRIBUTES)
					saved = ReplaceFileW(g_path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr) != FALSE;
				else
					saved = MoveFileExW(temporary.c_str(), g_path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			if (!saved) DeleteFileW(temporary.c_str());
			return saved;
		}

		bool RestoreRemote(std::vector<uint8_t>& data)
		{
			// Never replace buffers during a match, or after a menu edit while a
			// download was pending. Local files remain the recovery copy on conflicts.
			if (!Native<bool (*)()>(game::kIsInFrontEndRVA)() || !ValidNative(data)) return false;
			std::vector<uint8_t> current;
			if (!Snapshot(current)) return false;
			if (current != loadBaseline)
			{
				remoteConflict = true;
				remoteReady = false;
				LOG_WARN("Profile", "local profile changed during fetch; resolve in Client Settings; both copies retained");
				return false;
			}
			std::array<Context, 6> contexts{};
			for (int group = 0; group < 6; ++group)
				if (!GetContext(group, contexts[group]) || contexts[group].size != int(g_header.groups[group].size)) return false;
			if (!Save(data)) return false;
			size_t offset = sizeof(stats_file::Header);
			for (int group = 0; group < 6; ++group)
			{
				memcpy(contexts[group].buffer, data.data() + offset, contexts[group].size);
				Native<void (*)(int, int, int, bool, uint32_t)>(0x12a3ee0)(0, group, g_source, false, 0);
				Native<void (*)(int, int, int)>(0x12a2860)(0, group, g_source);
				offset += contexts[group].size;
			}
			g_lastSaved = data;
			g_saved_encoding.clear();
			loadBaseline = data;
			return true;
		}

		bool SaveJournal(uint64_t revision, const std::string& ack, const std::string& pending, const std::string& token, int choice = 0,
			const std::string& chosen = {})
		{
			try
			{
				std::filesystem::create_directories(journalPath.parent_path());
				const auto payload = profile_store::Json{
					{ "schema", 1 }, { "owner", std::to_string(g_xuid) }, { "revision", revision }, { "acknowledged", ack },
					{ "pending", pending }, { "token", token }, { "resolution", choice }, { "selected", chosen }
				}.dump();
				const auto bytes =
					profile_store::Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump();
				const auto temp = journalPath.wstring() + L".tmp";
				HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE) return false;
				DWORD count = 0;
				const bool ok =
					WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file);
				CloseHandle(file);
				if (!ok) return false;
				if (std::filesystem::exists(journalPath))
					return ReplaceFileW(
							   journalPath.c_str(), temp.c_str(), (journalPath.wstring() + L".bak").c_str(), 0, nullptr, nullptr) != FALSE;
				return MoveFileExW(temp.c_str(), journalPath.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			catch (...)
			{
				return false;
			}
		}

		bool ArchiveConflict(const std::string& local, const std::string& server)
		{
			try
			{
				const auto path =
					g_root / (L"profile_conflict_" + std::to_wstring(g_xuid) + L"_" + std::to_wstring(GetTickCount64()) + L".json");
				auto content = profile_store::Json{ { "owner", std::to_string(g_xuid) }, { "local", local }, { "server", server },
					{ "pending", upload }, { "revision", remoteRevision } };
				if (std::filesystem::exists(journalPath))
				{
					if (std::filesystem::file_size(journalPath) > 2 * 1024 * 1024) return false;
					std::ifstream input(journalPath, std::ios::binary);
					content["original_journal"] = std::string(std::istreambuf_iterator<char>(input), {});
				}
				const auto bytes = content.dump();
				HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (f == INVALID_HANDLE_VALUE) return false;
				DWORD written = 0;
				const bool ok =
					WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(f);
				CloseHandle(f);
				if (ok) LOG_INFO("Profile", "conflict recovery copies saved: %ls", path.c_str());
				return ok;
			}
			catch (...)
			{
				return false;
			}
		}

		bool LoadJournal()
		{
			if (journalLoaded) return true;
			const auto hash = challenges::Hash(
				std::string_view(reinterpret_cast<const char*>(remoteOrigin.data()), remoteOrigin.size() * sizeof(wchar_t)));
			journalPath = g_root / (L"profile_sync_" + std::to_wstring(g_xuid) + L"_" + std::to_wstring(hash) + L".json");
			try
			{
				if (std::filesystem::exists(journalPath))
				{
					if (std::filesystem::file_size(journalPath) > 2 * 1024 * 1024) throw std::runtime_error("journal size");
					std::ifstream stream(journalPath, std::ios::binary);
					profile_store::Json wrapper;
					stream >> wrapper;
					const auto payload = wrapper.at("payload").get<std::string>();
					if (wrapper.at("checksum") != std::to_string(challenges::Hash(payload))) throw std::runtime_error("journal checksum");
					const auto data = profile_store::Json::parse(payload);
					const auto ack = data.at("acknowledged").get<std::string>(), saved = data.at("pending").get<std::string>(),
							   token = data.at("token").get<std::string>();
					if (data.at("schema") != 1 || data.at("owner") != std::to_string(g_xuid) || !data.at("revision").is_number_unsigned() ||
						data.at("revision").get<uint64_t>() > INT64_MAX ||
						(!ack.empty() && !profile_store::Valid(g_xuid, ack, &g_header)) ||
						(!saved.empty() && (!profile_store::Valid(g_xuid, saved, &g_header) || !chat::Hex(token, 32))))
						throw std::runtime_error("journal profile");
					resolution = data.value("resolution", 0);
					selectedLocal = data.value("selected", std::string{});
					if (resolution < 0 || resolution > 2 || (resolution && !profile_store::Valid(g_xuid, selectedLocal, &g_header)))
						throw std::runtime_error("resolution intent");
					if (resolution)
					{
					if (saved_encoding() != selectedLocal)
						{
							remoteConflict = true;
							return false;
						}
						journalLoaded = true;
						return true;
					}
					// Resume exact pending bytes/token after lost response or restart.
					// Also retain a newer local atomic snapshot that was not queued yet.
					if (!saved.empty() || (!ack.empty() && !g_lastSaved.empty() && saved_encoding() != ack))
					{
						remoteRevision = data.at("revision").get<uint64_t>();
						remoteImport = true;
						acknowledged = ack;
						upload = saved;
						uploadToken = token;
						uploadOffset = 0;
					}
				}
				journalLoaded = true;
				return true;
			}
			catch (const std::exception& error)
			{
				remoteConflict = true;
				LOG_ERR("Profile", "pending journal unreadable: %s; local/server copies preserved", error.what());
				return false;
			}
		}

		void RemotePump()
		{
			if (g_source != 0 || !frontend_backend::Required()) return;
			const auto origin = frontend_backend::Origin();
			if (origin != remoteOrigin)
			{
				remoteOrigin = origin;
				++remoteEpoch;
				remoteReady = remotePending = remoteConflict = remoteImport = false;
				remoteRevision = remoteNext = 0;
				downloaded.clear();
				upload.clear();
				uploadToken.clear();
				acknowledged.clear();
				loadBaseline.clear();
				uploadOffset = 0;
				journalLoaded = false;
				resolution = 0;
				selectedLocal.clear();
			}
			if (!frontend_backend::Authenticated() || remotePending || remoteConflict || GetTickCount64() < remoteNext) return;
			if (loadBaseline.empty() && !Snapshot(loadBaseline)) return;
			if (!LoadJournal()) return;
			const bool writing = remoteReady || remoteImport;
			if (writing && upload.empty())
			{
				if (g_lastSaved.empty()) return;
				const auto& candidate = saved_encoding();
				if (candidate == acknowledged) return;
				std::array<uint8_t, 16> random{};
				if (BCryptGenRandom(nullptr, random.data(), ULONG(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return;
				const auto token = profile_store::Encode(random);
				if (!SaveJournal(remoteRevision, acknowledged, candidate, token))
				{
					remoteNext = GetTickCount64() + 2000;
					return;
				}
				upload = candidate;
				uploadToken = token;
				uploadOffset = 0;
			}
			const auto generation = remoteEpoch;
			const auto nonce = "profile:" + std::to_string(++remoteSerial);
			const auto offset = writing ? uploadOffset : downloaded.size();
			profile_store::Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "profile" }, { "nonce", nonce },
				{ "action", writing ? "save" : "fetch" }, { "revision", remoteRevision }, { "offset", offset } };
			if (writing)
				request.update(
					{ { "upload", uploadToken }, { "total", upload.size() }, { "chunk", upload.substr(offset, profile_store::Chunk) } });
			remotePending = true;
			remoteNext = GetTickCount64() + 250;
			if (!social_runtime::SubmitFrontend(request.dump(),
					[generation, nonce, writing, offset](unsigned status, std::string bytes)
					{
						if (generation != remoteEpoch) return;
						remotePending = false;
						remoteNext = GetTickCount64() + 250;
						try
						{
							const auto reply = profile_store::Json::parse(bytes);
							if (!frontend_backend::Authenticated() || !frontend_service::Validate(reply, std::to_string(g_xuid), nonce))
								throw std::runtime_error("identity/envelope");
							if (status == 409)
							{
								if (writing && profile_store::RestartUpload(reply.at("profile"), remoteRevision, offset))
								{
									// Server recycling loses staging, not the saved revision.
									// Replay the same durable journal/token under the same CAS.
									uploadOffset = 0;
									LOG_INFO("Profile", "server upload staging expired; replaying preserved journal");
									return;
								}
								if (writing)
								{
									remoteConflict = true;
									remoteReady = false;
									LOG_WARN("Profile", "server revision conflict; choose a recovery copy in Client Settings");
								}
								else
								{
									downloaded.clear();
									remoteRevision = 0;
								}
								return;
							}
							if (status != 200) throw std::runtime_error("profile request");
							const auto& profile = reply.at("profile");
							const auto revision = profile.at("revision").get<uint64_t>();
							if (!profile.at("revision").is_number_unsigned() || revision > INT64_MAX || revision < remoteRevision)
								throw std::runtime_error("revision");
							if (writing)
							{
								const auto position = profile.at("offset").get<size_t>();
								if (!profile.at("offset").is_number_unsigned() || position < offset || position > upload.size())
									throw std::runtime_error("save acknowledgement");
								if (profile.at("complete").get<bool>())
								{
									if (position != upload.size() || revision != remoteRevision + 1)
										throw std::runtime_error("commit acknowledgement");
									// A late save ACK updates only the acknowledged copy. It
									// must never overwrite a newer live loadout selection.
									if (!SaveJournal(revision, upload, {}, {})) throw std::runtime_error("ack journal persistence");
									acknowledged = std::move(upload);
									upload.clear();
									remoteRevision = revision;
									remoteReady = true;
									remoteImport = false;
									LOG_INFO("Profile", "server saved six TU19 groups revision=%llu", revision);
								}
								else
								{
									if (revision != remoteRevision || position != std::min(upload.size(), offset + profile_store::Chunk))
										throw std::runtime_error("chunk acknowledgement");
									uploadOffset = position;
								}
							}
							else
							{
								const auto total = profile.at("total").get<size_t>(), position = profile.at("offset").get<size_t>();
								const auto chunk = profile.at("chunk").get<std::string>();
								if (position != offset || total > profile_store::Maximum || offset > total ||
									chunk.size() > total - offset || chunk.size() > profile_store::Chunk ||
									!chat::Hex(chunk, chunk.size()) || (!offset ? false : revision != remoteRevision))
									throw std::runtime_error("download chunk");
								if (!revision)
								{
									if (total || offset || !chunk.empty()) throw std::runtime_error("missing profile");
									if (resolution == 1)
									{
										remoteConflict = true;
										resolution = 0;
										LOG_WARN("Profile", "no saved server profile exists; local recovery copies retained");
										return;
									}
									resolution = 0;
									selectedLocal.clear();
									remoteImport = true;
									return;
								}
								if (chunk.empty() && offset < total) throw std::runtime_error("empty chunk");
								remoteRevision = revision;
								downloaded += chunk;
								if (downloaded.size() == total)
								{
									auto data = profile_store::Decode(downloaded);
									if (resolution)
									{
										std::vector<uint8_t> current;
										if (!Snapshot(current) || profile_store::Encode(current) != selectedLocal)
										{
											remoteConflict = true;
											throw std::runtime_error("profile changed during resolution");
										}
										if (!ValidNative(data) || !ArchiveConflict(selectedLocal, downloaded))
											throw std::runtime_error("conflict archive");
										if (resolution == 2)
										{
											// Explicit local choice rebases on the fetched revision, still CAS.
											std::array<uint8_t, 16> random{};
											if (BCryptGenRandom(
													nullptr, random.data(), ULONG(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
												throw std::runtime_error("random");
											const auto token = profile_store::Encode(random);
											if (!SaveJournal(revision, downloaded, selectedLocal, token))
												throw std::runtime_error("resolution journal");
											acknowledged = std::move(downloaded);
											downloaded.clear();
											upload = std::move(selectedLocal);
											selectedLocal.clear();
											uploadToken = token;
											uploadOffset = 0;
											remoteImport = true;
											resolution = 0;
											return;
										}
									}
									if (!RestoreRemote(data)) throw std::runtime_error("native restore");
									if (!SaveJournal(revision, downloaded, {}, {})) throw std::runtime_error("restore journal persistence");
									acknowledged = std::move(downloaded);
									downloaded.clear();
									remoteReady = true;
									resolution = 0;
									selectedLocal.clear();
									local_playercard::MarkDirty();
									LOG_INFO("Profile", "server profile restored revision=%llu groups=6", revision);
								}
							}
						}
						catch (const std::exception& error)
						{
							remoteNext = GetTickCount64() + 2000;
							LOG_WARN("Profile", "request pending http=%u reason=%s; existing data retained", status, error.what());
						}
					}))
				remotePending = false;
		}
	}

	void Prepare(HMODULE module, uint64_t xuid)
	{
		wchar_t path[MAX_PATH]{};
		if (!GetModuleFileNameW(module, path, MAX_PATH)) return;
		g_root = std::filesystem::path(path).parent_path() / L"players";
		g_xuid = xuid;
	}

	bool Initialize(uintptr_t base, int source)
	{
		if (g_ready) return true;
		if (g_root.empty()) return false;
		g_base = base;
		g_source = source;
		if (!BindingsMatch()) return false;
		g_header.xuid = g_xuid;
		g_header.source = source;
		g_path = g_root / (L"stats_" + std::to_wstring(g_xuid) + (source == 0 ? L"_online.bin" : L"_offline.bin"));
		for (int group = 0; group < 6; ++group)
		{
			Context context{};
			if (!GetContext(group, context) || context.size <= 0 || context.size > 65536) return false;
			uint16_t version = 0;
			uint64_t schema = 0;
			memcpy(&version, context.definition + 8, sizeof(version));
			memcpy(&schema, context.definition + 24, sizeof(schema));
			g_header.groups[group] = { static_cast<uint32_t>(context.size), version, schema };
		}
		std::vector<uint8_t> restored;
		const auto primary = Read(g_path, restored);
		auto result = primary;
		if (result != ReadResult::Valid)
		{
			result = Read(g_path.wstring() + L".bak", restored);
			if (result == ReadResult::Valid && primary == ReadResult::Invalid)
			{
				// Retain corrupt input for diagnosis; never rotate it over a good backup.
				const auto rejected = g_path.wstring() + L".rejected." + std::to_wstring(GetTickCount64());
				g_writable = MoveFileExW(g_path.c_str(), rejected.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			else if (primary == ReadResult::Invalid || result == ReadResult::Invalid)
			{
				g_writable = false;
				LOG_ERR("Stats", "local stats invalid or schema differs; preserved files, autosave disabled: %ls", g_path.c_str());
			}
		}
		if (result == ReadResult::Valid)
		{
			// All six groups were validated before any live byte changes. Restore
			// before menu consumers, then let native code invalidate cached contexts
			// and establish fresh per-process obfuscation for each group.
			std::array<Context, 6> contexts{};
			for (int group = 0; group < 6; ++group)
				if (!GetContext(group, contexts[group])) return false;
			size_t offset = sizeof(stats_file::Header);
			for (int group = 0; group < 6; ++group)
			{
				memcpy(contexts[group].buffer, restored.data() + offset, contexts[group].size);
				Native<void (*)(int, int, int, bool, uint32_t)>(0x12a3ee0)(0, group, source, false, 0);
				Native<void (*)(int, int, int)>(0x12a2860)(0, group, source);
				offset += contexts[group].size;
			}
			g_lastSaved = restored;
			g_saved_encoding.clear();
			LOG_INFO("Stats", "restored six local DDL groups: %ls%s", g_path.c_str(), primary == ReadResult::Valid ? "" : " (backup)");
		}
		else
			LOG_INFO("Stats", "no compatible local stats restored; keeping initialized defaults");
		g_ready = true;
		return true;
	}

	void Pump()
	{
		if (!g_ready || !g_writable || !Native<bool (*)()>(0x12cf280)()) return;
		RemotePump();
		if (GetTickCount64() < g_nextPoll) return;
		g_nextPoll = GetTickCount64() + 2000;
		// Sys_IsMainThread (12CF280), not a frontend predicate. Native DDL contexts
		// are accessed on the main thread; the two-second gate batches gameplay XP.
		if (!Native<bool (*)()>(0x12cf280)()) return;
		const auto fetched = g_base + 0xd2f36b8 + 74624 * g_source + 74620;
		if (!*reinterpret_cast<const uint8_t*>(fetched)) return;
		std::vector<uint8_t> snapshot;
		if (!Snapshot(snapshot))
		{
			LOG_WARN("Stats", "local snapshot validation failed; existing save retained");
			g_nextPoll += 8000;
			return;
		}
		if (snapshot == g_lastSaved) return;
		if (Save(snapshot))
		{
			g_lastSaved = std::move(snapshot);
			g_saved_encoding.clear();
			// Operator/loadout editors write DDL without the calling-card editor's
			// SaveChanges callback. Repack on the normal main-thread card pump so
			// the native party cache reflects these changes too.
			local_playercard::MarkDirty();
			LOG_INFO("Stats", "saved local loadouts/operators/stats (%zu bytes)", g_lastSaved.size());
		}
		else
		{
			LOG_ERR("Stats", "atomic local stats save failed: %ls", g_path.c_str());
		}
	}

	void SaveNow()
	{
		g_nextPoll = 0;
		Pump();
	}

	bool RemoteReady()
	{
		return !frontend_backend::Required() || g_source != 0 || remoteReady;
	}

	SyncView SyncStatus()
	{
		return { RemoteReady(), remotePending || (resolution != 0 && !remoteConflict), remoteConflict || !g_writable,
			!g_writable ? "storage_error"
				: remoteConflict ? "conflict"
				: !frontend_backend::Authenticated() ? "disconnected"
				: resolution ? "resolving"
				: remoteReady ? "synced"
				: "loading" };
	}

	bool ResolveConflict(bool keepLocal)
	{
		if (!g_ready || (!remoteConflict && g_writable) || remotePending || g_source != 0 || !frontend_backend::Authenticated() ||
			!Native<bool (*)()>(0x12cf280)() || !Native<bool (*)()>(game::kIsInFrontEndRVA)())
			return false;
		std::vector<uint8_t> current;
		if (!Snapshot(current)) return false;
		const auto chosen = profile_store::Encode(current);
		if (!g_writable)
		{
			// Preserve both unreadable original files before explicit recovery.
			// Never offer to upload initialized defaults as the user's local copy.
			if (keepLocal) return false;
			for (const auto& path : { g_path, std::filesystem::path(g_path.wstring() + L".bak") })
				if (std::filesystem::exists(path))
				{
					const auto archive = path.wstring() + L".recovery." + std::to_wstring(GetTickCount64());
					if (!CopyFileW(path.c_str(), archive.c_str(), TRUE)) return false;
				}
			const auto origin = frontend_backend::Origin();
			if (remoteOrigin != origin)
			{
				remoteOrigin = origin;
				++remoteEpoch;
			}
			const auto hash =
				challenges::Hash(std::string_view(reinterpret_cast<const char*>(origin.data()), origin.size() * sizeof(wchar_t)));
			journalPath = g_root / (L"profile_sync_" + std::to_wstring(g_xuid) + L"_" + std::to_wstring(hash) + L".json");
		}
		if (!ArchiveConflict(chosen, downloaded) || !Save(current) || !SaveJournal(0, {}, {}, {}, keepLocal ? 2 : 1, chosen)) return false;
		g_writable = true;
		++remoteEpoch;
		resolution = keepLocal ? 2 : 1;
		selectedLocal = chosen;
		g_lastSaved = current;
		g_saved_encoding.clear();
		loadBaseline = current;
		remoteConflict = remoteReady = remotePending = remoteImport = false;
		remoteRevision = remoteNext = 0;
		upload.clear();
		downloaded.clear();
		uploadToken.clear();
		acknowledged.clear();
		uploadOffset = 0;
		journalLoaded = true;
		return true;
	}
}
