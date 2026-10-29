#include "client/loader/component_loader.hpp"
#include "client/match_client_capacity.hpp"
#include "client/mp_dvar_registration.hpp"
#include "common/console.h"
#include "common/state.h"
#include "dedicated/dedicated.h"
#include "diagnostics/night/night_files.h"
#include "engine/safemem.h"
#include "network/backend/native_matchmaking.h"

#include <atomic>
#include <cstring>
#include <intrin.h>

namespace mp_dvar_registration_component
{
	namespace
	{
		constexpr uintptr_t allocate_rva = 0x15E8CC0;
		constexpr uintptr_t register_rva = 0x15EBA30;
		constexpr uintptr_t throttle_type_rva = 0xEF2E350;
		constexpr uint8_t allocate_bytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57,
			0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x09, 0x00, 0x00 };
		constexpr uint8_t register_bytes[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
			0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57 };
		using allocate_fn = void (*)(unsigned);
		std::atomic<allocate_fn> original{ nullptr };
		uintptr_t base = 0;

		bool registered()
		{
			uintptr_t dvar = 0;
			return safemem::ReadBytes(reinterpret_cast<const void*>(base + throttle_type_rva), &dvar, sizeof(dvar)) && dvar;
		}

		void allocate(unsigned flags)
		{
			// Replay IDA: CL_MainMP_ShutdownGameMode deregisters the MP dvar
			// group. A later CL_ConnectAndPreloadMap can reach GameAllocate
			// without InitAllClients. Its ClActiveClientMP allocator reads
			// cl_adjust_time_throttle_type->current at 15DF329 unconditionally.
			// Restore the native group before any client hunk allocation; a
			// synthetic throttle value would leave the other MP globals absent.
			if (!client::mp_dvar_registration::ensure(registered, []
				{
					LOG_WARN("ClientDvars", "restoring native MP dvar registration before client allocation");
					reinterpret_cast<void (*)()>(base + register_rva)();
				}))
			{
				// ERR_DROP uses the engine's normal join recovery before any
				// partial client allocation has been published.
				reinterpret_cast<void (*)(int, const char*, ...)>(base + 0x12AB4A0)(
					1, "MP client dvar registration unavailable before allocation");
				return;
			}
			// The received party capacity must survive frontend shutdown and MP
			// registration before GameAllocate reads ui_maxclients for its buffers.
			const auto capacity = client::match_client_capacity::prepare(base, flags,
				native_matchmaking::Enabled() && night_files::authenticated_island_client_match());
			if (capacity.state != client::match_client_capacity::status::skipped)
			{
				const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
				LOG_INFO("ClientCapacity", "Island allocation: status=%s flags=%u party=%d ui_before=%d allocated_local=%d cached=%d caller_RVA=0x%llX",
					client::match_client_capacity::status_name(capacity.state), flags, capacity.party_capacity,
					capacity.ui_before, capacity.allocated_clients, capacity.cached_capacity,
					static_cast<unsigned long long>(caller >= base ? caller - base : 0));
			}
			if (!capacity.success())
			{
				reinterpret_cast<void (*)(int, const char*, ...)>(base + 0x12AB4A0)(1,
					"Island client capacity could not be prepared: %s (party=%d, allocated=%d)",
					client::match_client_capacity::status_name(capacity.state), capacity.party_capacity, capacity.cached_capacity);
				return;
			}
			original.load(std::memory_order_acquire)(flags);
		}
	}

	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			if (dedicated::Enabled()) return hook::Status::Installed;
			base = state::moduleBase.load();
			uint8_t actual[sizeof(register_bytes)]{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(base + register_rva), actual, sizeof(actual)) ||
				std::memcmp(actual, register_bytes, sizeof(actual))) return hook::Status::NotReady;
			if (!client::match_client_capacity::validate(base)) return hook::Status::NotReady;
			return hook::Install(reinterpret_cast<void*>(base + allocate_rva), &allocate,
				allocate_bytes, sizeof(allocate_bytes), original);
		}
		component_priority priority() override { return component_priority::client_runtime; }
	};
}
REGISTER_COMPONENT(mp_dvar_registration_component::component)
