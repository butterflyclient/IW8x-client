#include "bap.h"
#include "engine/asset_pools.hpp"

#include "engine/addresses.hpp"

#include "console.h"

#include "game.h"
#include "safemem.h"

#include "state.h"
#include "thirdparty/minhook/include/MinHook.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>

#include "dedicated/assets/bap/internal.hpp"
#include "diagnostics/night/night_files.h"
namespace bap::detail
{
	static constexpr unsigned kPhysicsAssetType = 3;

	// Replay 1.20.4.7623265 native XAsset pool sizing.  The allocation-failure path
	// reads type limits through sub_+0xF5F3A0 from this table.  The backing allocator
	// at +0xF5E620 uses the same table to allocate elementSize * poolSize bytes, and
	// the free-list constructor at +0xF5F660 consumes it again. Replay's own
	// name/size tables prove type 3 = physicsasset (88 bytes) and type 101 =
	// xmodeldetailcollision (40 bytes).
	//
	// Reference startup writes 64000 for type 3 and writes 96000 for type 101 when
	// -bap is enabled, all before DB_InitXAssetPools. Replay ships with 15000 and
	// 16312 respectively; both stock limits were observed at runtime.
	static constexpr uintptr_t kNativePoolSizeTableRva = game::addresses::runtime::bap_native_pool_size_table;

	static constexpr uintptr_t kPhysicsAssetPoolSizeRva = game::addresses::runtime::bap_physics_asset_pool_size;

	static constexpr uintptr_t kDetailCollisionPoolSizeRva = game::addresses::runtime::bap_detail_collision_pool_size;

	static constexpr std::uint32_t kNightPhysicsAssetPoolSize = 96000;
	static constexpr std::uint32_t kReplayPhysicsAssetPoolSize = 15000;

	static constexpr std::uint32_t kReferencePhysicsAssetPoolSize = 64000;

	static constexpr std::uint32_t kReplayDetailCollisionPoolSize = 16312;

	static constexpr std::uint32_t kBapDetailCollisionPoolSize = 96000;

	// Replay 1.20.4.7623265. These are the stock DB_AssetEntryPool/Table methods
	// structurally mapped from DB_LinkXAssetEntry and verified in the Replay bytes.
	static constexpr uintptr_t kAllocateRva = game::addresses::runtime::bap_allocate;

	static constexpr uintptr_t kFreeRva = game::addresses::runtime::bap_free;

	static constexpr uintptr_t kAddToStashTailRva = game::addresses::runtime::bap_add_to_stash_tail;

	static constexpr uintptr_t kAddToTableRva = game::addresses::runtime::bap_add_to_table;

	static constexpr uintptr_t kFindAssetRva = game::addresses::runtime::bap_find_asset;

	static constexpr uintptr_t kMakeFirstStashedEntryRva = game::addresses::runtime::bap_make_first_stashed_entry;

	static constexpr uintptr_t kRemoveFromTableRva = game::addresses::runtime::bap_remove_from_table;

	static constexpr uintptr_t kRemoveStashedAssetRva = game::addresses::runtime::bap_remove_stashed_asset;

	static constexpr uintptr_t kAllocateHeaderRva = game::addresses::runtime::bap_allocate_header;

	static constexpr uintptr_t kFreeHeaderRva = game::addresses::runtime::bap_free_header;

	static constexpr uint8_t kAllocateBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
		0x18 };

	static constexpr uint8_t kFreeBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B,
		0xFA };

	static constexpr uint8_t kAddToStashTailBytes[] = { 0x48, 0x2B, 0xD1, 0x48, 0xB8, 0x67, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x48,
		0xF7, 0xEA };

	static constexpr uint8_t kAddToTableBytes[] = { 0x48, 0x8B, 0x02, 0x4C, 0x8B, 0xD2, 0x44, 0x0F, 0xB6, 0x42, 0x11, 0x4C, 0x8B, 0xD9,
		0x4C, 0x8B };

	static constexpr uint8_t kFindAssetBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18,
		0x0F };

	static constexpr uint8_t kMakeFirstBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0xCA, 0x48, 0xB8, 0x67, 0x66, 0x66, 0x66, 0x66,
		0x66 };

	static constexpr uint8_t kRemoveFromTableBytes[] = { 0x48, 0x8B, 0x02, 0x4C, 0x8B, 0xD2, 0x44, 0x0F, 0xB6, 0x4A, 0x11, 0x4C, 0x8B, 0xD9,
		0x4C, 0x8B };

	static constexpr uint8_t kRemoveStashedBytes[] = { 0x8B, 0x42, 0x0C, 0x41, 0x89, 0x40, 0x0C, 0x4C, 0x8D, 0x81, 0x08, 0xEF, 0x83, 0x00,
		0xC7, 0x42 };

	uintptr_t g_base = 0;

	static std::atomic<int> g_enabled{ -1 };

	std::atomic<bool> g_coreReady{ false };

	std::atomic<bool> g_lifecycleReady{ false };

	// 0 = not attempted, 1 = exact pre-init resize applied/verified, -1 = failed.
	std::atomic<int> g_nativePoolsPrepared{ 0 };

	static bool g_installed = false;

	bool g_corePrepared = false;

	std::array<uintptr_t, 8> g_coreTargets{};

	bool g_lifecyclePrepared = false;

	std::array<uintptr_t, 4> g_lifecycleTargets{};

	bool ContainsBapFlag(const char* commandLine)
	{
		if (!commandLine) return false;
		constexpr char needle[] = "-bap";
		for (const char* p = commandLine; *p; ++p)
		{
			std::size_t i = 0;
			for (; needle[i]; ++i)
			{
				char c = p[i];
				if (!c) break;
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
				if (c != needle[i]) break;
			}
			if (!needle[i]) return true;
		}
		return false;
	}
}

namespace bap
{
	using namespace detail;

	bool Enabled()
	{
		int cached = g_enabled.load(std::memory_order_acquire);
		if (cached >= 0) return cached != 0;
		const bool enabled = ContainsBapFlag(GetCommandLineA());
		g_enabled.store(enabled ? 1 : 0, std::memory_order_release);
		return enabled;
	}

	bool PrepareNativePools(uintptr_t moduleBase, bool nightRequested)
	{
		if (!Enabled() && !nightRequested) return true;
		const bool ready = asset_pools::prepare(moduleBase, nightRequested);
		g_nativePoolsPrepared.store(ready ? 1 : -1, std::memory_order_release);
		return ready;
	}

	bool RequiredMap(const char* mapName)
	{
		return mapName && (_stricmp(mapName, "mp_donetsk") == 0 || _stricmp(mapName, "mp_don4") == 0);
	}

	bool Ready()
	{
		return Enabled() && g_coreReady.load(std::memory_order_acquire) && g_lifecycleReady.load(std::memory_order_acquire);
	}

	hook::Status InstallCore(uintptr_t moduleBase)
	{
		if (!Enabled()) return hook::Status::Installed;

		if (g_nativePoolsPrepared.load(std::memory_order_acquire) != 1)
		{
			LOG_ERR("BAP", "native physicsasset/xmodeldetailcollision pools were not resized before DB pool initialization");
			return hook::Status::Failed;
		}

		// Converted-map mode allocates the larger, verified 96000-entry physics
		// pool before native DB initialization. BAP can share that capacity:
		// both paths require the same 96000 detail-collision entries, and the
		// converted-map physics pool is strictly larger than BAP's 64000.
		// Only accept 96000 after asset_pools::prepare verified its full set.
		const auto expectedPhysicsPoolSize = asset_pools::port_ready()
			? kNightPhysicsAssetPoolSize : kReferencePhysicsAssetPoolSize;
		std::uint32_t physicsPoolSize = 0;
		std::uint32_t detailPoolSize = 0;
		if (!safemem::ReadBytes(
				reinterpret_cast<const void*>(moduleBase + kPhysicsAssetPoolSizeRva), &physicsPoolSize, sizeof(physicsPoolSize)) ||
			!safemem::ReadBytes(
				reinterpret_cast<const void*>(moduleBase + kDetailCollisionPoolSizeRva), &detailPoolSize, sizeof(detailPoolSize)) ||
			physicsPoolSize != expectedPhysicsPoolSize || detailPoolSize != kBapDetailCollisionPoolSize)
		{
			LOG_ERR("BAP",
				"native BAP pool sizes changed before install (physicsasset expected=%u actual=%u, detail expected=%u actual=%u)",
					expectedPhysicsPoolSize, physicsPoolSize, kBapDetailCollisionPoolSize, detailPoolSize);
			return hook::Status::Failed;
		}
		LOG_INFO("BAP", "native physicsasset XAsset pool resized before DB init: %u -> %u headers", kReplayPhysicsAssetPoolSize,
			expectedPhysicsPoolSize);
		LOG_INFO("BAP", "native xmodeldetailcollision XAsset pool resized before DB init: %u -> %u headers", kReplayDetailCollisionPoolSize,
			kBapDetailCollisionPoolSize);

		std::lock_guard<std::mutex> lock(hook::detail::Mutex());
		if (g_installed) return hook::Status::Installed;

		g_base = moduleBase;
		if (!InitializeState())
		{
			LOG_ERR("BAP", "could not allocate auxiliary 100000-entry DB pool");
			return hook::Status::Failed;
		}
		if (!safemem::RegionExecutable(reinterpret_cast<void*>(g_base + kAllocateHeaderRva)) ||
			!safemem::RegionExecutable(reinterpret_cast<void*>(g_base + kFreeHeaderRva)))
		{
			LOG_ERR("BAP", "native XAsset header allocator/free dispatch is not executable");
			return hook::Status::Failed;
		}
		g_allocateHeader = reinterpret_cast<AllocateHeaderFn>(g_base + kAllocateHeaderRva);
		g_freeHeader = reinterpret_cast<FreeHeaderFn>(g_base + kFreeHeaderRva);

		void* trampolines[8]{};
		HookSpec hooks[] = {
			{ kAllocateRva, reinterpret_cast<void*>(&Allocate), kAllocateBytes, sizeof(kAllocateBytes), &trampolines[0], "Allocate" },
			{ kFreeRva, reinterpret_cast<void*>(&Free), kFreeBytes, sizeof(kFreeBytes), &trampolines[1], "Free" },
			{ kFindAssetRva, reinterpret_cast<void*>(&FindAsset), kFindAssetBytes, sizeof(kFindAssetBytes), &trampolines[2], "FindAsset" },
			{ kAddToTableRva, reinterpret_cast<void*>(&AddToTable), kAddToTableBytes, sizeof(kAddToTableBytes), &trampolines[3],
				"AddToTable" },
			{ kRemoveFromTableRva, reinterpret_cast<void*>(&RemoveFromTable), kRemoveFromTableBytes, sizeof(kRemoveFromTableBytes),
				&trampolines[4], "RemoveFromTable" },
			{ kAddToStashTailRva, reinterpret_cast<void*>(&AddToStashTail), kAddToStashTailBytes, sizeof(kAddToStashTailBytes),
				&trampolines[5], "AddToStashTail" },
			{ kRemoveStashedAssetRva, reinterpret_cast<void*>(&RemoveStashedAsset), kRemoveStashedBytes, sizeof(kRemoveStashedBytes),
				&trampolines[6], "RemoveStashedAsset" },
			{ kMakeFirstStashedEntryRva, reinterpret_cast<void*>(&MakeFirstStashedEntry), kMakeFirstBytes, sizeof(kMakeFirstBytes),
				&trampolines[7], "MakeFirstStashedEntry" },
		};

		std::size_t prepared = 0;
		for (auto& spec : hooks)
		{
			const auto status =
				hook::detail::Prepare(reinterpret_cast<void*>(g_base + spec.rva), spec.detour, spec.bytes, spec.size, spec.trampoline);
			if (status != hook::Status::Installed)
			{
				LOG_ERR("BAP", "could not prepare DB_%s hook at RVA 0x%llX", spec.name, static_cast<unsigned long long>(spec.rva));
				for (std::size_t i = 0; i < prepared; ++i)
					MH_RemoveHook(reinterpret_cast<void*>(g_base + hooks[i].rva));
				g_traceAuxBase.store(0, std::memory_order_release);
				g_state.reset();
				return status;
			}
			++prepared;
		}

		g_allocate.store(reinterpret_cast<AllocateFn>(trampolines[0]), std::memory_order_release);
		g_free.store(reinterpret_cast<FreeFn>(trampolines[1]), std::memory_order_release);
		g_findAsset.store(reinterpret_cast<FindAssetFn>(trampolines[2]), std::memory_order_release);
		g_addToTable.store(reinterpret_cast<EntryFn>(trampolines[3]), std::memory_order_release);
		g_removeFromTable.store(reinterpret_cast<EntryFn>(trampolines[4]), std::memory_order_release);
		g_addToStashTail.store(reinterpret_cast<StashFn>(trampolines[5]), std::memory_order_release);
		g_removeStashedAsset.store(reinterpret_cast<StashFn>(trampolines[6]), std::memory_order_release);
		g_makeFirstStashedEntry.store(reinterpret_cast<StashFn>(trampolines[7]), std::memory_order_release);

		// Do not enable the core set in isolation. Once Allocate begins returning
		// auxiliary entries, every downstream stock-pool index/bitset access must be
		// auxiliary-aware too. InstallLifecycle will atomically queue this set with
		// the lifecycle detours/inline replacements when that mapping is complete.
		for (std::size_t i = 0; i < std::size(hooks); ++i)
			g_coreTargets[i] = g_base + hooks[i].rva;
		g_corePrepared = true;
		g_coreReady.store(false, std::memory_order_release);
		g_installed = true;
		LOG_INFO("BAP", "core auxiliary DB pool prepared: type=101 entries=%zu buckets=%zu", kCapacity, kCapacity);
		LOG_INFO("BAP", "core detours staged; waiting for lifecycle byte validation and auxiliary-aware inline routing");
		return hook::Status::Installed;
	}
}
