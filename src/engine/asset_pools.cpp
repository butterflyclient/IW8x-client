#include "engine/asset_pools.hpp"
#include "engine/addresses.hpp"
#include "engine/safemem.h"

#include <atomic>

namespace asset_pools
{
	namespace
	{
		std::atomic<int> prepared{};
		std::atomic<bool> ports_ready{};
		static constexpr uintptr_t physics_pool_slot = game::addresses::runtime::bap_physics_asset_pool_size;

		static constexpr uintptr_t detail_pool_slot = game::addresses::runtime::bap_detail_collision_pool_size;
		static constexpr uintptr_t vfx_pool_slot = game::addresses::runtime::bap_native_pool_size_table + sizeof(std::uint32_t) * 44;
		static constexpr uintptr_t script_reserve_slot = game::addresses::runtime::db_script_reserve_size;
		static constexpr std::uint64_t stock_script_reserve = 6ull * 1024 * 1024;
		static constexpr std::uint64_t port_script_reserve = 64ull * 1024 * 1024;

		static constexpr std::uint32_t port_physics_capacity = 96000;
		static constexpr std::uint32_t stock_physics_capacity = 15000;

		static constexpr std::uint32_t reference_physics_capacity = 64000;

		static constexpr std::uint32_t stock_detail_capacity = 16312;

		static constexpr std::uint32_t detail_capacity = 96000;
		static constexpr std::uint32_t stock_vfx_capacity = 3072;
		static constexpr std::uint32_t port_vfx_capacity = 8192;
	}

	bool prepare(uintptr_t module_base, bool port_requested)
	{
		const int status = prepared.load(std::memory_order_acquire);
		if (status != 0) return status > 0 && (!port_requested || port_ready());
		if (!module_base)
		{
			prepared.store(-1, std::memory_order_release);
			return false;
		}

		auto* const physics_slot = reinterpret_cast<std::uint32_t*>(module_base + physics_pool_slot);
		auto* const detail_slot = reinterpret_cast<std::uint32_t*>(module_base + detail_pool_slot);
		auto* const vfx_slot = reinterpret_cast<std::uint32_t*>(module_base + vfx_pool_slot);
		auto* const script_slot = reinterpret_cast<std::uint64_t*>(module_base + script_reserve_slot);
		std::uint32_t physics_current = 0;
		std::uint32_t detail_current = 0;
		std::uint32_t vfx_current = 0;
		std::uint64_t script_current = 0;
		if (!safemem::ReadBytes(physics_slot, &physics_current, sizeof(physics_current)) ||
			!safemem::ReadBytes(detail_slot, &detail_current, sizeof(detail_current)) ||
			(port_requested && (!safemem::ReadBytes(vfx_slot, &vfx_current, sizeof(vfx_current)) ||
				!safemem::ReadBytes(script_slot, &script_current, sizeof(script_current)))))
		{
			prepared.store(-1, std::memory_order_release);
			return false;
		}

		const auto physics_capacity = port_requested ? port_physics_capacity : reference_physics_capacity;
		const bool physics_already = physics_current == physics_capacity;
		const bool detail_already = detail_current == detail_capacity;
		const bool vfx_already = !port_requested || vfx_current == port_vfx_capacity;
		const bool script_already = !port_requested || script_current == port_script_reserve;
		if (physics_already && detail_already && vfx_already && script_already)
		{
			prepared.store(1, std::memory_order_release);
			if (port_requested) ports_ready.store(true, std::memory_order_release);
			return true;
		}

		// Validate all requested stock values before mutating any one. Unsupported builds
		// or unexpected loader state therefore fail without a partial resize.
		if ((!physics_already && physics_current != stock_physics_capacity) ||
			(!detail_already && detail_current != stock_detail_capacity) ||
			(!vfx_already && vfx_current != stock_vfx_capacity) ||
			(!script_already && script_current != stock_script_reserve))
		{
			prepared.store(-1, std::memory_order_release);
			return false;
		}

		// DB_MemoryInit (11A9550) reads this QWORD into the native script
		// allocator. Resize its virtual reserve before initialization; changing
		// it after allocation would leave the real heap and its bounds unchanged.
		if (!script_already)
		{
			std::uint8_t initialized{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(module_base + game::addresses::runtime::db_memory_initialized),
				&initialized, sizeof(initialized)) || initialized)
			{
				prepared.store(-1, std::memory_order_release);
				return false;
			}
		}
		DWORD script_protect{};
		if (port_requested && !VirtualProtect(script_slot, sizeof(*script_slot), PAGE_READWRITE, &script_protect))
		{
			prepared.store(-1, std::memory_order_release);
			return false;
		}
		auto* const first = physics_slot;
		const SIZE_T span = reinterpret_cast<std::uint8_t*>(detail_slot + 1) - reinterpret_cast<std::uint8_t*>(first);
		DWORD old_protect = 0;
		if (!VirtualProtect(first, span, PAGE_READWRITE, &old_protect))
		{
			DWORD ignored{};
			if (port_requested) VirtualProtect(script_slot, sizeof(*script_slot), script_protect, &ignored);
			prepared.store(-1, std::memory_order_release);
			return false;
		}
		*physics_slot = physics_capacity;
		*detail_slot = detail_capacity;
		if (port_requested)
		{
			*vfx_slot = port_vfx_capacity;
			*script_slot = port_script_reserve;
		}
		DWORD ignored = 0;
		const BOOL restored = VirtualProtect(first, span, old_protect, &ignored);
		const bool script_restored = !port_requested || VirtualProtect(script_slot, sizeof(*script_slot), script_protect, &ignored);

		std::uint32_t physics_verify = 0;
		std::uint32_t detail_verify = 0;
		std::uint32_t vfx_verify = 0;
		std::uint64_t script_verify = 0;
		const bool ok = restored && script_restored && safemem::ReadBytes(physics_slot, &physics_verify, sizeof(physics_verify)) &&
			safemem::ReadBytes(detail_slot, &detail_verify, sizeof(detail_verify)) && physics_verify == physics_capacity &&
			detail_verify == detail_capacity && (!port_requested ||
				(safemem::ReadBytes(vfx_slot, &vfx_verify, sizeof(vfx_verify)) && vfx_verify == port_vfx_capacity &&
					safemem::ReadBytes(script_slot, &script_verify, sizeof(script_verify)) && script_verify == port_script_reserve));
		prepared.store(ok ? 1 : -1, std::memory_order_release);
		if (ok && port_requested) ports_ready.store(true, std::memory_order_release);
		return ok;
	}

	bool port_ready()
	{
		return ports_ready.load(std::memory_order_acquire);
	}
}
