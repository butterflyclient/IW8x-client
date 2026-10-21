#include "bap.h"
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
namespace bap::detail
{
	std::atomic<AllocateFn> g_allocate{ nullptr };

	std::atomic<FreeFn> g_free{ nullptr };

	std::atomic<FindAssetFn> g_findAsset{ nullptr };

	std::atomic<EntryFn> g_addToTable{ nullptr };

	std::atomic<EntryFn> g_removeFromTable{ nullptr };

	std::atomic<StashFn> g_addToStashTail{ nullptr };

	std::atomic<StashFn> g_removeStashedAsset{ nullptr };

	std::atomic<StashFn> g_makeFirstStashedEntry{ nullptr };

	AllocateHeaderFn g_allocateHeader = nullptr;

	FreeHeaderFn g_freeHeader = nullptr;

	std::unique_ptr<State> g_state;

	std::uint64_t Mask(std::uint32_t index)
	{
		return 0x8000000000000000ull >> (index & 63u);
	}

	bool IndexOf(const Entry* entry, std::uint32_t& index)
	{
		if (!g_state || !entry) return false;
		const auto base = reinterpret_cast<std::uintptr_t>(g_state->entries.get());
		const auto address = reinterpret_cast<std::uintptr_t>(entry);
		const auto bytes = sizeof(Entry) * kCapacity;
		if (address < base || address >= base + bytes) return false;
		const auto delta = address - base;
		if ((delta % sizeof(Entry)) != 0) return false;
		index = static_cast<std::uint32_t>(delta / sizeof(Entry));
		return index < kCapacity;
	}

	Entry* StockPool()
	{
		return reinterpret_cast<Entry*>(g_base + game::kDBAssetEntryStockPoolRVA);
	}

	bool StockIndexOf(const Entry* entry, std::uint32_t& index)
	{
		if (!g_base || !entry) return false;
		const auto base = g_base + game::kDBAssetEntryStockPoolRVA;
		const auto address = reinterpret_cast<std::uintptr_t>(entry);
		const auto bytes = sizeof(Entry) * game::kDBAssetEntryStockCount;
		if (address < base || address >= base + bytes) return false;
		const auto delta = address - base;
		if ((delta % sizeof(Entry)) != 0) return false;
		index = static_cast<std::uint32_t>(delta / sizeof(Entry));
		return index < game::kDBAssetEntryStockCount;
	}

	std::uint64_t* StockPlaneWord(std::size_t planeOffset, std::uint32_t index)
	{
		const auto address = g_base + game::kDBAssetEntryStockPoolRVA + planeOffset + sizeof(std::uint64_t) * (index >> 6);
		return reinterpret_cast<std::uint64_t*>(address);
	}

	bool StockTest(std::size_t planeOffset, std::uint32_t index)
	{
		return (*StockPlaneWord(planeOffset, index) & Mask(index)) != 0;
	}

	void StockSet(std::size_t planeOffset, std::uint32_t index)
	{
		*StockPlaneWord(planeOffset, index) |= Mask(index);
	}

	void StockClear(std::size_t planeOffset, std::uint32_t index)
	{
		*StockPlaneWord(planeOffset, index) &= ~Mask(index);
	}

	bool Test(Plane plane, std::uint32_t index)
	{
		return (g_state->planes[plane][index >> 6] & Mask(index)) != 0;
	}

	void Set(Plane plane, std::uint32_t index)
	{
		g_state->planes[plane][index >> 6] |= Mask(index);
	}

	void Clear(Plane plane, std::uint32_t index)
	{
		g_state->planes[plane][index >> 6] &= ~Mask(index);
	}

	std::uint32_t HashName(const char* name, unsigned type)
	{
		std::uint32_t hash = type;
		if (!name) return hash % static_cast<std::uint32_t>(kCapacity);
		for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
			hash = static_cast<std::uint32_t>(static_cast<std::int8_t>(*p)) + 31u * hash;
		return hash % static_cast<std::uint32_t>(kCapacity);
	}

	const char* EntryName(const Entry* entry)
	{
		if (!entry || !entry->header) return nullptr;
		return *reinterpret_cast<const char* const*>(entry->header);
	}

	bool AnyZoneLocal(const Entry* entry)
	{
		std::uint32_t index = 0;
		if (!IndexOf(entry, index)) return false;
		return Test(Zone0Primary, index) || Test(Zone1Primary, index) || Test(Zone2Primary, index);
	}

	bool UsePendingLocal(const Entry* entry)
	{
		std::uint32_t index = 0;
		return IndexOf(entry, index) && AnyZoneLocal(entry) && !Test(UseOnce, index);
	}

	bool MarkUseOnceLocal(Entry* entry)
	{
		std::uint32_t index = 0;
		if (!IndexOf(entry, index) || Test(UseOnce, index)) return false;
		Set(UseOnce, index);
		return true;
	}

	void MarkLifecycleLocal(Entry* entry)
	{
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) Set(Lifecycle, index);
	}

	void ClearLifecycleLocal(Entry* entry)
	{
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) Clear(Lifecycle, index);
	}

	void MarkZonePairLocal(Entry* entry, unsigned group)
	{
		std::uint32_t index = 0;
		if (!IndexOf(entry, index) || group >= 3) return;
		const auto primary = static_cast<Plane>(static_cast<unsigned>(Zone0Primary) + group * 2);
		const auto secondary = static_cast<Plane>(static_cast<unsigned>(primary) + 1);
		Set(primary, index);
		Set(secondary, index);
	}

	void MarkHeadResolvedLocal(Entry* entry)
	{
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) Set(HeadResolved, index);
	}

	__declspec(noinline) void MarkLifecycleAware(Entry* entry)
	{
		RecordEvent("MarkLifecycleAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			Set(Lifecycle, index);
			return;
		}
		if (StockIndexOf(entry, index)) StockSet(game::kDBAssetEntryStockLifecyclePlaneOffset, index);
	}

	__declspec(noinline) void ClearLifecycleAware(Entry* entry)
	{
		RecordEvent("ClearLifecycleAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			Clear(Lifecycle, index);
			return;
		}
		if (StockIndexOf(entry, index)) StockClear(game::kDBAssetEntryStockLifecyclePlaneOffset, index);
	}

	__declspec(noinline) void MarkZonePairAware(Entry* entry, unsigned group)
	{
		RecordEvent("MarkZonePairAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry, "enter", nullptr, group);
		if (group >= 3) return;
		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			MarkZonePairLocal(entry, group);
			return;
		}
		if (!StockIndexOf(entry, index)) return;
		const auto stride = game::kDBAssetEntryStockZoneGroupStride * group;
		StockSet(game::kDBAssetEntryStockZonePrimaryOffset + stride, index);
		StockSet(game::kDBAssetEntryStockZoneSecondaryOffset + stride, index);
	}

	__declspec(noinline) void MarkTransientHeadAware(Entry* entry)
	{
		RecordEvent("MarkTransientHeadAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			Set(Unknown, index);
			return;
		}
		if (StockIndexOf(entry, index)) StockSet(game::kDBAssetEntryStockTransientHeadPlaneOffset, index);
	}

	__declspec(noinline) Entry* NextStashAware(Entry* entry, uintptr_t caller)
	{
		// The preserving bridge forwards Replay's return PC, not its own code address.
		RecordEvent("NextStashAware", caller, entry);
		if (!entry || !entry->stashNext) return nullptr;
		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			if (entry->stashNext >= kCapacity) return nullptr;
			return &g_state->entries[entry->stashNext];
		}
		if (StockIndexOf(entry, index))
		{
			if (entry->stashNext >= game::kDBAssetEntryStockCount) return nullptr;
			return &StockPool()[entry->stashNext];
		}
		return nullptr;
	}

	__declspec(noinline) void MarkResolvedHeadAware(Entry* entry)
	{
		RecordEvent("MarkResolvedHeadAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		if (!entry) return;

		std::uint32_t index = 0;
		if (IndexOf(entry, index))
		{
			Entry* head = entry;
			if (!Test(TableHead, index))
			{
				const char* name = EntryName(entry);
				if (name)
				{
					Entry* found = FindAsset(StockPool(), name, entry->type);
					if (found) head = found;
				}
			}
			MarkHeadResolvedLocal(head);
			return;
		}

		if (!StockIndexOf(entry, index)) return;
		Entry* head = entry;
		if (!StockTest(game::kDBAssetEntryStockTableHeadPlaneOffset, index))
		{
			const char* name = EntryName(entry);
			const auto originalFind = g_findAsset.load(std::memory_order_acquire);
			if (name && originalFind)
			{
				Entry* found = originalFind(StockPool(), name, entry->type);
				if (found) head = found;
			}
		}
		if (StockIndexOf(head, index)) StockSet(game::kDBAssetEntryStockResolvedHeadPlaneOffset, index);
	}

	__declspec(noinline) bool __fastcall AnyZoneDetour(Entry* entry)
	{
		RecordEvent("AnyZone", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) return AnyZoneLocal(entry);
		const auto original = g_anyZone.load(std::memory_order_acquire);
		return original ? original(entry) : false;
	}

	__declspec(noinline) bool __fastcall TestUseOnceAware(Entry* entry)
	{
		RecordEvent("TestUseOnceAware", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) return Test(UseOnce, index);
		if (StockIndexOf(entry, index)) return StockTest(game::kDBAssetEntryStockUseOncePlaneOffset, index);
		return false;
	}

	__declspec(noinline) bool __fastcall UsePendingDetour(Entry* entry)
	{
		RecordEvent("UsePending", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (IndexOf(entry, index)) return UsePendingLocal(entry);
		const auto original = g_usePending.load(std::memory_order_acquire);
		return original ? original(entry) : false;
	}

	__declspec(noinline) std::uint64_t __fastcall UseTransitionOnceDetour(Entry* entry)
	{
		RecordEvent("UseTransitionOnce", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		std::uint32_t index = 0;
		if (!IndexOf(entry, index))
		{
			const auto original = g_useTransitionOnce.load(std::memory_order_acquire);
			return original ? original(entry) : 0;
		}

		if (!MarkUseOnceLocal(entry)) return 1;
		const char* name = EntryName(entry);
		if (!name || !g_registeredPredicate) return 0;
		if (!g_registeredPredicate(name, entry->type)) return 0;
		if (g_registeredFollowup) g_registeredFollowup(name, entry->type);
		return 1;
	}

	__declspec(noinline) std::uint64_t __fastcall LifecycleMarkDetour(Entry* entry, int type, std::uint64_t value, char transformValue)
	{
		RecordEvent("LifecycleMark", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry, transformValue ? "transform" : "enter", nullptr,
			static_cast<unsigned>(type), value);
		// The direct stock-plane OR in the original body is replaced with NOPs during
		// installation.  This wrapper performs the matching stock or auxiliary write
		// first, then lets Replay execute the rest of the native transition unchanged.
		MarkLifecycleAware(entry);
		const auto original = g_lifecycleMark.load(std::memory_order_acquire);
		return original ? original(entry, type, value, transformValue) : 0;
	}

	bool InitializeState()
	{
		auto state = std::unique_ptr<State>(new (std::nothrow) State{});
		if (!state) return false;
		state->entries.reset(new (std::nothrow) Entry[kCapacity]{});
		state->buckets.reset(new (std::nothrow) std::uint32_t[kCapacity]{});
		if (!state->entries || !state->buckets) return false;
		for (auto& plane : state->planes)
		{
			plane.reset(new (std::nothrow) std::uint64_t[kBitWords]{});
			if (!plane) return false;
		}

		// Entry index zero is the null/sentinel index, matching the reference BAP pool.
		// The reference uses an intrusive self-sentinel; nullptr is equivalent inside
		// this private pool and avoids exposing a non-Entry sentinel pointer.
		for (std::size_t i = 1; i + 1 < kCapacity; ++i)
			state->entries[i].freeNext = &state->entries[i + 1];
		state->entries[kCapacity - 1].freeNext = nullptr;
		state->freeHead = &state->entries[1];
		g_state = std::move(state);
		g_traceAuxBase.store(reinterpret_cast<uintptr_t>(g_state->entries.get()), std::memory_order_release);
		return true;
	}

	__declspec(noinline) Entry* __fastcall Allocate(void* glob, unsigned type, std::uint16_t zoneIndex)
	{
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
		RecordEvent("Allocate", caller, nullptr, "enter", nullptr, type, zoneIndex);
		if (!HandlesType(type))
		{
			Entry* result = g_allocate.load(std::memory_order_acquire)(glob, type, zoneIndex);
			RecordEvent("Allocate", caller, result, "return", nullptr, type, zoneIndex);
			return result;
		}
		if (!g_state || !g_allocateHeader)
		{
			RecordEvent("Allocate", caller, nullptr, "unavailable", nullptr, type, zoneIndex);
			return nullptr;
		}
		void* header = g_allocateHeader(type);
		if (!header)
		{
			RecordEvent("Allocate", caller, nullptr, "header-failed", nullptr, type, zoneIndex);
			LOG_ERR("BAP", "native XAsset header allocation failed for type %u", type);
			return nullptr;
		}
		Entry* entry = g_state->freeHead;
		RecordEvent("Allocate", caller, entry, "free-head", nullptr, type, zoneIndex);
		if (!entry)
		{
			LOG_ERR("BAP", "auxiliary XAsset entry pool exhausted (%zu entries)", kCapacity);
			if (g_freeHeader) g_freeHeader(type, header);
			return nullptr;
		}
		g_state->freeHead = entry->freeNext;
		entry->header = header;
		entry->hashNext = 0;
		entry->stashNext = 0;
		entry->flags = 0;
		entry->type = static_cast<std::uint8_t>(type);
		entry->zoneIndex = zoneIndex;
		std::uint32_t index = 0;
		IndexOf(entry, index);
		Set(Allocation, index);
		RecordEvent("Allocate", caller, entry, "return", nullptr, type, zoneIndex);
		return entry;
	}

	__declspec(noinline) void __fastcall Free(void* glob, Entry* entry)
	{
		RecordEvent("Free", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		if (!entry)
		{
			g_free.load(std::memory_order_acquire)(glob, entry);
			return;
		}

		std::uint32_t index = 0;
		if (!IndexOf(entry, index))
		{
			// Entry-based DB methods route by pool ownership, not by type alone.
			// A stock type-101 entry may legitimately exist before BAP activation.
			g_free.load(std::memory_order_acquire)(glob, entry);
			return;
		}
		if (g_freeHeader && entry->header) g_freeHeader(entry->type, entry->header);
		Clear(Allocation, index);
		entry->hashNext = 0;
		entry->stashNext = 0;
		entry->flags = 0;
		entry->freeNext = g_state->freeHead;
		g_state->freeHead = entry;
	}

	__declspec(noinline) Entry* __fastcall FindAsset(void* glob, const char* name, unsigned type)
	{
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
		RecordEvent("FindAsset", caller, nullptr, "enter", nullptr, type, reinterpret_cast<uintptr_t>(name));
		if (!HandlesType(type))
		{
			Entry* result = g_findAsset.load(std::memory_order_acquire)(glob, name, type);
			RecordEvent("FindAsset", caller, result, "return", nullptr, type, reinterpret_cast<uintptr_t>(name));
			return result;
		}
		if (!g_state || !name)
		{
			RecordEvent("FindAsset", caller, nullptr, "unavailable", nullptr, type);
			return nullptr;
		}
		for (std::uint32_t index = g_state->buckets[HashName(name, type)]; index;)
		{
			if (index >= kCapacity)
			{
				RecordEvent("FindAsset", caller, nullptr, "bad-index", nullptr, type, index);
				LOG_ERR("BAP", "corrupt auxiliary hash chain index=%u", index);
				return nullptr;
			}
			Entry* entry = &g_state->entries[index];
			RecordEvent("FindAsset", caller, entry, "candidate", nullptr, type);
			if (entry->type == type && Test(Allocation, index))
			{
				const char* entryName = EntryName(entry);
				if (entryName && std::strcmp(entryName, name) == 0)
				{
					RecordEvent("FindAsset", caller, entry, "return", nullptr, type);
					return entry;
				}
			}
			index = entry->hashNext;
		}
		RecordEvent("FindAsset", caller, nullptr, "return", nullptr, type);
		return nullptr;
	}

	__declspec(noinline) std::uint64_t __fastcall AddToTable(void* glob, Entry* entry)
	{
		RecordEvent("AddToTable", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		if (!entry) return g_addToTable.load(std::memory_order_acquire)(glob, entry);

		std::uint32_t index = 0;
		if (!IndexOf(entry, index)) return g_addToTable.load(std::memory_order_acquire)(glob, entry);
		const char* name = EntryName(entry);
		const std::uint32_t bucket = HashName(name, entry->type);
		entry->hashNext = g_state->buckets[bucket];
		g_state->buckets[bucket] = index;
		Set(TableHead, index);
		Set(TableMember, index);
		return Mask(index);
	}

	__declspec(noinline) std::uint64_t __fastcall RemoveFromTable(void* glob, Entry* entry)
	{
		RecordEvent("RemoveFromTable", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry);
		if (!entry) return g_removeFromTable.load(std::memory_order_acquire)(glob, entry);

		std::uint32_t index = 0;
		if (!IndexOf(entry, index)) return g_removeFromTable.load(std::memory_order_acquire)(glob, entry);
		Clear(TableHead, index);
		Clear(TableMember, index);
		const std::uint32_t bucket = HashName(EntryName(entry), entry->type);
		std::uint32_t* link = &g_state->buckets[bucket];
		while (*link)
		{
			if (*link >= kCapacity) return 0;
			Entry* current = &g_state->entries[*link];
			if (current == entry)
			{
				*link = entry->hashNext;
				entry->hashNext = 0;
				return Mask(index);
			}
			link = &current->hashNext;
		}
		return 0;
	}

	__declspec(noinline) std::uint64_t __fastcall AddToStashTail(void* glob, Entry* entry, Entry* head)
	{
		RecordEvent("AddToStashTail", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry, "enter", head);
		if (!entry) return g_addToStashTail.load(std::memory_order_acquire)(glob, entry, head);

		std::uint32_t index = 0;
		if (!IndexOf(entry, index)) return g_addToStashTail.load(std::memory_order_acquire)(glob, entry, head);

		std::uint32_t headIndex = 0;
		if (!head || !IndexOf(head, headIndex))
		{
			LOG_ERR("BAP", "auxiliary type-101 stash head is outside the auxiliary pool: %p", head);
			return 0;
		}
		Entry* tail = head;
		while (tail->stashNext)
		{
			if (tail->stashNext >= kCapacity) return 0;
			tail = &g_state->entries[tail->stashNext];
		}
		tail->stashNext = index;
		Set(TableMember, index);
		return Mask(index);
	}

	__declspec(noinline) std::uint64_t __fastcall RemoveStashedAsset(void* glob, Entry* entry, Entry* previous)
	{
		RecordEvent("RemoveStashedAsset", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry, "enter", previous);
		if (!entry) return g_removeStashedAsset.load(std::memory_order_acquire)(glob, entry, previous);

		std::uint32_t index = 0;
		if (!IndexOf(entry, index)) return g_removeStashedAsset.load(std::memory_order_acquire)(glob, entry, previous);

		std::uint32_t previousIndex = 0;
		if (!previous || !IndexOf(previous, previousIndex))
		{
			LOG_ERR("BAP", "auxiliary type-101 stash predecessor is outside the auxiliary pool: %p", previous);
			return 0;
		}
		previous->stashNext = entry->stashNext;
		entry->stashNext = 0;
		Clear(TableMember, index);
		return Mask(index);
	}

	__declspec(noinline) std::uint64_t __fastcall MakeFirstStashedEntry(void* glob, Entry* entry, Entry* head)
	{
		RecordEvent("MakeFirstStashedEntry", reinterpret_cast<uintptr_t>(_ReturnAddress()), entry, "enter", head);
		if (!entry) return g_makeFirstStashedEntry.load(std::memory_order_acquire)(glob, entry, head);

		std::uint32_t entryIndex = 0;
		if (!IndexOf(entry, entryIndex)) return g_makeFirstStashedEntry.load(std::memory_order_acquire)(glob, entry, head);

		std::uint32_t headIndex = 0;
		if (!head || !IndexOf(head, headIndex))
		{
			LOG_ERR("BAP", "auxiliary type-101 promotion head is outside the auxiliary pool: %p", head);
			return 0;
		}
		if (head->stashNext == entryIndex) return entryIndex;

		Entry* previous = head;
		while (previous->stashNext)
		{
			if (previous->stashNext >= kCapacity) return 0;
			Entry* current = &g_state->entries[previous->stashNext];
			if (current == entry)
			{
				previous->stashNext = entry->stashNext;
				entry->stashNext = head->stashNext;
				head->stashNext = entryIndex;
				return entryIndex;
			}
			previous = current;
		}
		return 0;
	}
}

namespace bap
{
	using namespace detail;

	bool HandlesType(unsigned type)
	{
		return Enabled() && type == kDetailCollisionType;
	}
}
