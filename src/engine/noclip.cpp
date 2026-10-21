#include "engine/noclip.h"

#include "common/console.h"

#include "engine/game.h"
#include "engine/safemem.h"

#include "auth/offline_auth.h"
#include "dedicated/dedicated.h"

#include "engine/camo_changer.h"

#include <array>
#include <algorithm>
#include <charconv>
#include <atomic>
#include <cstring>
namespace
{
	uintptr_t g_base = 0;
	using ClientCommandFn = void(__fastcall*)(int clientNum);
	using SVCmdArgvFn = const char*(__fastcall*)(int index);
	std::atomic<ClientCommandFn> g_originalClientCommand{ nullptr };
	std::atomic<bool> g_localServer{ false };
	std::atomic<uint64_t> g_owner{ 0 }, g_authorityAt{ 0 };

	bool Owned(int slot)
	{
		const auto now = GetTickCount64(), at = g_authorityAt.load();
		if (!g_localServer.load() || !at || now < at || now - at > 1000 || slot < 0 || slot >= 200) return false;
		uintptr_t client = 0;
		uint8_t state = 0;
		std::array<char, 32> guid{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xe17f690 + 8ull * slot), &client, sizeof(client)) || !client ||
			!safemem::ReadBytes(reinterpret_cast<void*>(client + 8), &state, sizeof(state)) || state < 3 ||
			!safemem::ReadBytes(reinterpret_cast<void*>(client + 214724), guid.data(), guid.size()))
			return false;
		const auto end = std::find(guid.begin(), guid.end(), '\0');
		if (end == guid.end()) return false;
		uint64_t id = 0;
		const auto result = std::from_chars(guid.data(), guid.data() + (end - guid.begin()), id);
		return id && id == g_owner.load() && result.ec == std::errc{} && result.ptr == guid.data() + (end - guid.begin());
	}

	bool MatchesTarget(uintptr_t rva, const uint8_t* expected, size_t size)
	{
		uint8_t actual[32]{};
		return size <= sizeof(actual) && safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), actual, size) &&
			std::memcmp(actual, expected, size) == 0;
	}

	bool ReadArg(int index, char* out, size_t outSize)
	{
		if (!out || !outSize || !g_base) return false;
		out[0] = '\0';
		const auto argv = reinterpret_cast<SVCmdArgvFn>(g_base + game::kSVCmdArgvRVA);
		const char* value = argv(index);
		return value && safemem::ReadString(value, out, outSize) > 0;
	}

	uintptr_t GetGClient(uintptr_t base, int clientNum)
	{
		if (!base || clientNum < 0 || clientNum >= 200) return 0;

		uintptr_t entities = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kGEntitiesPtrRVA), &entities, sizeof(entities)) || !entities) return 0;

		const uintptr_t ent = entities + static_cast<size_t>(clientNum) * game::kGEntityStride;
		uintptr_t client = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(ent + game::kGEntityClientOffset), &client, sizeof(client)) || !client) return 0;

		return client;
	}

	void __fastcall HookedClientCommand(int clientNum)
	{
		char name[64]{};
		ReadArg(0, name, sizeof(name));
		if (_stricmp(name, "mw_camo") == 0)
		{
			camo_changer::ClientCommand(clientNum);
			return;
		}

		if (_stricmp(name, "noclip") != 0 && _stricmp(name, "mw_noclip") != 0)
		{
			const auto original = g_originalClientCommand.load(std::memory_order_acquire);
			if (original) original(clientNum);
			return;
		}

		char arg1[32]{}, extra[4]{};
		if (!Owned(clientNum))
		{
			LOG_WARN("Noclip", "requires the local player in a locally hosted match");
			return;
		}
		ReadArg(1, arg1, sizeof(arg1));
		ReadArg(2, extra, sizeof(extra));

		if (extra[0] != '\0')
		{
			LOG_WARN("Noclip", "usage: noclip [on|off|1|0]");
			return;
		}

		bool enable = false;
		if (arg1[0] == '\0')
		{
			bool current = false;
			if (!noclip::GetState(g_base, clientNum, &current))
			{
				LOG_WARN("Noclip", "spawn as a player before changing noclip");
				return;
			}
			enable = !current;
		}
		else if (_stricmp(arg1, "on") == 0 || strcmp(arg1, "1") == 0)
		{
			enable = true;
		}
		else if (_stricmp(arg1, "off") == 0 || strcmp(arg1, "0") == 0)
		{
			enable = false;
		}
		else
		{
			LOG_WARN("Noclip", "usage: noclip [on|off|1|0]");
			return;
		}

		if (noclip::SetState(g_base, clientNum, enable))
		{
			LOG_INFO("Noclip", "%s for client %d", enable ? "ON" : "OFF", clientNum);
			OP_INFO("Noclip | client %d | %s", clientNum, enable ? "ON" : "OFF");
		}
		else
		{
			LOG_WARN("Noclip", "spawn as a player before changing noclip");
		}
	}
}

namespace noclip
{
	void Pump()
	{
		if (!g_originalClientCommand.load() || dedicated::Enabled())
		{
			g_localServer = false;
			return;
		}
		const auto identity = offlineauth::GetIdentity();
		g_owner = identity.xuid;
		const bool host = identity.xuid && reinterpret_cast<bool (*)()>(g_base + game::kIsAnyLocalServerRunningRVA)() &&
			!reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)();
		g_localServer = host;
		g_authorityAt = GetTickCount64();
	}

	bool LocalState(bool* enabled)
	{
		if (enabled) *enabled = false;
		for (int slot = 0; slot < 200; ++slot)
			if (Owned(slot)) return GetState(g_base, slot, enabled);
		return false;
	}

	bool GetState(uintptr_t base, int clientNum, bool* outState)
	{
		if (outState) *outState = false;

		const uintptr_t client = GetGClient(base, clientNum);
		if (!client) return false;

		int pmType = -1;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(client + game::kGClientPmTypeOffset), &pmType, sizeof(pmType)) ||
			(pmType != game::kPmTypeNormal && pmType != game::kPmTypeNoclip))
			return false;

		uint32_t flags = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(client + game::kGClientNoclipFlagsOffset), &flags, sizeof(flags))) return false;

		if (outState) *outState = (flags & game::kNoclipFlagBit) != 0;

		return true;
	}

	bool SetState(uintptr_t base, int clientNum, bool enable, bool* outPrevious)
	{
		if (outPrevious) *outPrevious = false;

		const uintptr_t client = GetGClient(base, clientNum);
		if (!client) return false;

		int pmType = -1;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(client + game::kGClientPmTypeOffset), &pmType, sizeof(pmType)) ||
			(pmType != game::kPmTypeNormal && pmType != game::kPmTypeNoclip))
			return false;

		uint32_t flags = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(client + game::kGClientNoclipFlagsOffset), &flags, sizeof(flags))) return false;

		const bool wasEnabled = (flags & game::kNoclipFlagBit) != 0;
		if (outPrevious) *outPrevious = wasEnabled;

		const uint32_t next = enable ? (flags | game::kNoclipFlagBit) : (flags & ~game::kNoclipFlagBit);
		if (!safemem::WriteBytes(reinterpret_cast<void*>(client + game::kGClientNoclipFlagsOffset), &next, sizeof(next))) return false;

		// Synchronize pm_type directly for instantaneous client prediction / simulation response.
		const int newPmType = enable ? game::kPmTypeNoclip : game::kPmTypeNormal;
		if (!safemem::WriteBytes(reinterpret_cast<void*>(client + game::kGClientPmTypeOffset), &newPmType, sizeof(newPmType)))
		{
			safemem::WriteBytes(reinterpret_cast<void*>(client + game::kGClientNoclipFlagsOffset), &flags, sizeof(flags));
			return false;
		}

		return true;
	}

	bool Toggle(uintptr_t base, int clientNum, bool* outState)
	{
		bool current = false;
		if (!GetState(base, clientNum, &current)) return false;

		const bool target = !current;
		if (!SetState(base, clientNum, target)) return false;

		if (outState) *outState = target;

		return true;
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_originalClientCommand.load(std::memory_order_acquire)) return hook::Status::Installed;

		g_base = base;
		if (!camo_changer::Initialize(base)) return hook::Status::NotReady;
		if (!MatchesTarget(game::kIsAnyLocalServerRunningRVA, game::kIsAnyLocalServerRunningPrologue,
				sizeof(game::kIsAnyLocalServerRunningPrologue)) ||
			!MatchesTarget(game::kIsInFrontEndRVA, game::kIsInFrontEndPrologue, sizeof(game::kIsInFrontEndPrologue)))
			return hook::Status::NotReady;

		// Verify engine signatures.
		if (!MatchesTarget(game::kCmdClientCommandRVA, game::kCmdClientCommandPrologue, sizeof(game::kCmdClientCommandPrologue)))
		{
			LOG_ERR("Noclip", "G_CmdsMP_ClientCommand signature mismatch at RVA 0x%llX",
				static_cast<unsigned long long>(game::kCmdClientCommandRVA));
			return hook::Status::NotReady;
		}

		if (!MatchesTarget(game::kNoclipMovementFlagsRVA, game::kNoclipMovementFlagsPrologue, sizeof(game::kNoclipMovementFlagsPrologue)))
		{
			LOG_ERR("Noclip", "NoclipMovementFlags signature mismatch at RVA 0x%llX",
				static_cast<unsigned long long>(game::kNoclipMovementFlagsRVA));
			return hook::Status::NotReady;
		}

		const auto status = hook::Install(reinterpret_cast<void*>(base + game::kCmdClientCommandRVA), &HookedClientCommand,
			game::kCmdClientCommandPrologue, sizeof(game::kCmdClientCommandPrologue), g_originalClientCommand);

		if (status == hook::Status::Installed)
			LOG_INFO("Noclip", "native client-command noclip hook installed (RVA 0x%llX)",
				static_cast<unsigned long long>(game::kCmdClientCommandRVA));

		return status;
	}
}
