#include "engine/camo_changer.h"
#include "network/commerce/camo_state.h"
#include "engine/safemem.h"
#include "engine/game.h"
#include "engine/commands.h"
#include "common/console.h"
#include "dedicated/dedicated.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_challenges.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "network/profile/progression_backend.h"
#include <charconv>
#include <mutex>
#include <set>
namespace camo_changer
{
	namespace
	{
		using Json = nlohmann::json;
#include "engine/camo_bindings.inc"
		uintptr_t base = 0;
		bool initialized = false;
		std::mutex stateMutex, permissionMutex;
		camo120::States states;

		struct Permission
		{
			uint64_t owner = 0, until = 0;
			std::set<int> items;
		};

		Permission permission;
		// The UI view and request state belong exclusively to the game/UI thread.
		Json view = Json::object();
		bool local = false, pending = false;
		uint64_t owner = 0, epoch = 0, sequence = 0, next = 0, demand = 0, viewAt = 0;
		std::wstring origin;
#ifdef REPLAY_CAMO_FIXTURE
		template <class T>
		T native(uintptr_t rva)
		{
			return reinterpret_cast<T>(::CamoTestNative(rva));
		}
#else
		template <class T>
		T native(uintptr_t rva)
		{
			return reinterpret_cast<T>(base + rva);
		}
#endif
		template <class T>
		bool read(uintptr_t at, T& value)
		{
			return safemem::ReadBytes(reinterpret_cast<void*>(at), &value, sizeof(value));
		}

		template <class T>
		T value_impl(uintptr_t at)
		{
			T v{};
			read(at, v);
			return v;
		}

		std::string arg(int index)
		{
			const auto* p = native<const char* (*)(int)>(game::kSVCmdArgvRVA)(index);
			char text[80]{};
			const auto n = safemem::ReadString(p, text, sizeof(text));
			return n < sizeof(text) - 1 ? std::string(text, n) : std::string{};
		}

		uint64_t identity(int slot, uintptr_t& connection)
		{
			if (slot < 0 || slot >= 200) return 0;
			connection = value_impl<uintptr_t>(base + 0xE17F690 + 8ull * slot);
			if (!connection || value_impl<uint8_t>(connection + 8) < 3) return 0;
			char guid[32]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(connection + 214724), guid, sizeof(guid))) return 0;
			const auto* end = std::find(guid, guid + sizeof(guid), '\0');
			if (end == guid + sizeof(guid)) return 0;
			uint64_t id = 0;
			const auto result = std::from_chars(guid, end, id);
			return result.ec == std::errc{} && result.ptr == end ? id : 0;
		}

		bool capture(int slot, uintptr_t connection, camo120::Context& context, std::string& weapon)
		{
			const auto entities = value_impl<uintptr_t>(base + game::kGEntitiesPtrRVA);
			if (!entities) return false;
			const auto ps = value_impl<uintptr_t>(entities + slot * game::kGEntityStride + game::kGEntityClientOffset);
			if (!ps) return false;
			const auto pm = value_impl<int>(ps + game::kGClientPmTypeOffset);
			if (pm != game::kPmTypeNormal && pm != game::kPmTypeNoclip) return false;
			const auto map = native<uintptr_t (*)()>(0xFF74C0)();
			if (!map || value_impl<uintptr_t>(map) != base + 0x240EE90 || value_impl<uint8_t>(map + 16)) return false;
			const auto capacity = value_impl<uint16_t>(base + 0x5C80AE0), handle = value_impl<uint16_t>(ps + 1784);
			if (capacity < 2 || capacity > 400 || !handle || handle >= capacity) return false;
			const auto entries = value_impl<uintptr_t>(map + 8);
			if (!entries || value_impl<uint16_t>(entries + 62ull * handle) != handle) return false;
			context.player = ps;
			context.connection = connection;
			context.map = map;
			if (!read(entries + 62ull * handle + 2, context.weapon) || !value_impl<uint16_t>(entries + 62ull * handle + 2)) return false;
			const auto* name = native<const char* (*)(const camo120::Weapon*)>(0x1157470)(&context.weapon);
			char text[96]{};
			if (!safemem::ReadString(name, text, sizeof(text))) return false;
			weapon = camo120::Canonical(text);
			return std::any_of(std::begin(camo120::catalog), std::end(camo120::catalog),
				[&](const auto& row)
				{
					return weapon == row.weapon;
				});
		}

		int authorized(uint64_t id, int item)
		{
			if (item <= 0) return 0;
#ifndef IW8X_CLIENT_ONLY
			if (dedicated::Enabled()) return progression_backend::TryOwns(id, item);
#endif
			std::unique_lock lock(permissionMutex, std::try_to_lock);
			if (!lock.owns_lock()) return -1;
			return permission.owner == id && GetTickCount64() <= permission.until ? (permission.items.contains(item) ? 1 : 0) : -1;
		}

		bool has_map_room(uintptr_t map, const camo120::Weapon& weapon)
		{
			const auto count = value_impl<uint16_t>(base + 0x5C80AE0);
			const auto entries = value_impl<uintptr_t>(map + 8);
			if (!entries || count < 2 || count > 400 || value_impl<uint8_t>(map + 16)) return false;
			for (uint16_t i = 1; i < count; ++i)
			{
				if (value_impl<uint16_t>(entries + 62ull * i) != i) return true;
				camo120::Weapon other{};
				if (read(entries + 62ull * i + 2, other) && other == weapon) return true;
			}
			return false;
		}

		bool apply(camo120::State& state, uint8_t index, uint64_t now)
		{
			auto after = state.context.weapon;
			after[camo120::CamoByte] = index;
			if (after == state.context.weapon)
			{
				state.result = "unchanged";
				return true;
			}
			const auto ps = state.context.player, map = state.context.map;
			const int slot = native<int (*)(uintptr_t, uintptr_t, const camo120::Weapon*)>(0x1137AF0)(map, ps, &state.context.weapon);
			if (slot < 0 || slot >= 15 || !has_map_room(map, after)) return false;
			camo120::Clips clips{};
			uint32_t flags = 0;
			if (!read(ps + 2896, clips) || !read(ps + 1796, flags) || !camo120::Rebind(clips, state.context.weapon, after)) return false;
			// Check the complete destination before making the native handle change.
			MEMORY_BASIC_INFORMATION region{};
			if (!VirtualQuery(reinterpret_cast<void*>(ps), &region, sizeof(region)) ||
				!safemem::RegionWritable(reinterpret_cast<void*>(ps)) ||
				reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize < ps + 4308)
				return false;
			native<void (*)(uintptr_t, uintptr_t, const camo120::Weapon*)>(0xFF7830)(map, ps + 1452 + 4ull * slot, &after);
			const auto handler = native<uintptr_t (*)()>(0xE89820)();
			native<void (*)(uintptr_t, uintptr_t, const camo120::Weapon*, uintptr_t)>(0x1139890)(map, ps, &after, handler);
			flags ^= 0x100000;
			// These fields belong to this simulation command. No HTTP/UI writes.
			safemem::WriteBytes(reinterpret_cast<void*>(ps + 2896), &clips, sizeof(clips));
			safemem::WriteBytes(reinterpret_cast<void*>(ps + 1796), &flags, sizeof(flags));
			states.Changed(state, after, now);
			return true;
		}

		Json published(const camo120::State* state)
		{
			if (!state) return { { "result", "Spawn with a supported weapon, then refresh." } };
			return { { "token", std::to_string(state->token) }, { "weapon", state->weapon },
				{ "camo", state->context.weapon[camo120::CamoByte] }, { "original", state->original }, { "result", state->result } };
		}

		void permissions(uint64_t now)
		{
			Permission fresh;
			if (local && frontend_backend::Authenticated())
			{
				fresh.owner = owner;
				fresh.until = now + 1000;
				const auto weapon = view.value("weapon", std::string{});
				for (const auto& row : camo120::catalog)
					if (weapon == row.weapon && local_challenges::Owns(row.item)) fresh.items.insert(row.item);
			}
			std::lock_guard lock(permissionMutex);
			permission = std::move(fresh);
		}
	}

	bool Initialize(uintptr_t image)
	{
		if (initialized) return base == image;
		base = image;
		for (const auto& binding : camoBindings)
		{
			uint8_t actual[64]{};
			if (!safemem::RegionExecutable(reinterpret_cast<void*>(base + binding.rva)) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), actual, binding.size) ||
				memcmp(actual, binding.bytes, binding.size))
				return false;
		}
		uint8_t actual[sizeof(game::kSLFindStringPrologue)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kSLFindStringRVA), actual, sizeof(actual)) ||
			memcmp(actual, game::kSLFindStringPrologue, sizeof(actual)))
			return false;
		initialized = true;
		LOG_INFO("Camo120", "TU19 weapon/clip adapters verified; native client-command execution only");
		return true;
	}

	void ClientCommand(int slot)
	{
		if (!initialized) return;
		uintptr_t connection = 0;
		const auto id = identity(slot, connection);
		if (!id) return;
		const auto action = arg(1), token = arg(2), camo = arg(3);
		if (!arg(4).empty()) return;
		std::unique_lock lock(stateMutex, std::try_to_lock);
		if (!lock.owns_lock()) return;
		camo120::Context context;
		std::string weapon;
		const auto now = GetTickCount64();
		if (!capture(slot, connection, context, weapon))
		{
			states.Erase(id);
			return;
		}
		if (action == "inspect" && token.empty() && camo.empty())
		{
			states.Capture(id, context, weapon, now);
			return;
		}
		if (!camo120::Token(token) || (action != "apply" && action != "restore")) return;
		auto* state = states.Check(id, context, token, now);
		if (!state) return;
		unsigned index = state->original;
		if (action == "apply")
		{
			const auto owned = authorized(id, camo120::Item(weapon, camo));
			if (owned != 1)
			{
				state->result = owned < 0 ? "Account busy; refresh and retry." : "Camo is not owned for this weapon.";
				return;
			}
			const auto string = native<uint32_t (*)(const char*)>(game::kSLFindStringRVA)(camo.c_str());
			if (!string)
			{
				state->result = "Camo asset unavailable.";
				return;
			}
			index = native<unsigned (*)(uint32_t)>(0xCA3980)(string);
		}
		else if (!camo.empty())
			return;
		if (index > 255 || index >= native<unsigned (*)()>(0xCA3970)())
		{
			state->result = "Camo asset unavailable.";
			return;
		}
		if (!apply(*state, uint8_t(index), now)) state->result = "Weapon changed or weapon map is full; refresh and retry.";
	}

	Json ServerStatus(uint64_t id)
	{
		std::lock_guard lock(stateMutex);
		return published(states.Find(id, GetTickCount64()));
	}

	void Pump()
	{
		if (!initialized || dedicated::Enabled()) return;
		const auto now = GetTickCount64();
		const auto id = offlineauth::GetIdentity().xuid;
		const auto provider = frontend_backend::Origin();
		const bool inFrontend = native<bool (*)()>(game::kIsInFrontEndRVA)();
		const bool hosted = !inFrontend && native<bool (*)()>(game::kIsAnyLocalServerRunningRVA)();
		if (owner != id || origin != provider || local != hosted || inFrontend)
		{
			++epoch;
			owner = id;
			origin = provider;
			local = hosted;
			pending = false;
			view = Json::object();
			viewAt = 0;
			if (inFrontend)
			{
				std::lock_guard lock(stateMutex);
				states.Clear();
			}
		}
		if (inFrontend || !owner || !frontend_backend::Authenticated())
		{
			std::lock_guard lock(permissionMutex);
			permission = {};
			return;
		}
		if (now > demand || now < next) return;
		next = now + 500;
		if (local)
		{
			view = ServerStatus(owner);
			viewAt = now;
			permissions(now);
		}
		commands::Queue(base, "cmd mw_camo inspect\n");
		if (local || pending) return;
		const auto generation = epoch, account = owner;
		const auto requestedOrigin = origin;
		const auto nonce = "camo:" + std::to_string(++sequence);
		const Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "camo" }, { "nonce", nonce } };
		pending = true;
		if (!social_runtime::SubmitFrontend(request.dump(),
				[generation, account, requestedOrigin, nonce](unsigned status, std::string bytes)
				{
					if (generation != epoch || account != owner || requestedOrigin != frontend_backend::Origin()) return;
					pending = false;
					try
					{
						const auto reply = Json::parse(bytes);
						if (status != 200 || !frontend_backend::Authenticated() ||
							!frontend_service::Validate(reply, std::to_string(account), nonce))
							throw 0;
						const auto candidate = reply.at("camo");
						if (!candidate.is_object() || candidate.dump().size() > 512) throw 0;
						view = candidate;
						viewAt = GetTickCount64();
					}
					catch (...)
					{
						view = Json::object();
						viewAt = 0;
					}
				}))
			pending = false;
	}

	std::string Status()
	{
		const auto now = GetTickCount64();
		demand = now + 2000;
		if (!viewAt || now < viewAt || now - viewAt > 1500) return "Waiting for the current weapon.";
		return view.value("weapon", std::string{}) + " | " + view.value("result", std::string{});
	}

	std::string Catalog()
	{
		if (!viewAt || GetTickCount64() - viewAt > 1500) return {};
		std::string result;
		const auto weapon = view.value("weapon", std::string{});
		for (const auto& row : camo120::catalog)
			if (weapon == row.weapon)
			{
				result += row.camo;
				result += local_challenges::Owns(row.item) ? "|1\n" : "|0\n";
			}
		return result;
	}

	bool Submit(std::string_view action, std::string_view camo)
	{
		if (!initialized || !frontend_backend::Authenticated()) return false;
		const auto now = GetTickCount64();
		demand = now + 2000;
		if (action == "inspect") return commands::Queue(base, "cmd mw_camo inspect\n");
		if ((action != "apply" && action != "restore") || !viewAt || now < viewAt || now - viewAt > 1500) return false;
		const auto token = view.value("token", std::string{});
		if (!camo120::Token(token)) return false;
		if (action == "apply" && !local_challenges::Owns(camo120::Item(view.value("weapon", std::string{}), camo))) return false;
		if (action == "restore" && !camo.empty()) return false;
		const auto command = "cmd mw_camo " + std::string(action) + " " + token + (action == "apply" ? " " + std::string(camo) : "") + "\n";
		return commands::Queue(base, command.c_str());
	}
}
