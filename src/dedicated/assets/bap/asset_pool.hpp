#pragma once
#include "engine/addresses.hpp"

#include "dedicated/assets/bap/types.hpp"
namespace bap::detail
{
	extern std::atomic<AllocateFn> g_allocate;

	extern std::atomic<FreeFn> g_free;

	extern std::atomic<FindAssetFn> g_findAsset;

	extern std::atomic<EntryFn> g_addToTable;

	extern std::atomic<EntryFn> g_removeFromTable;

	extern std::atomic<StashFn> g_addToStashTail;

	extern std::atomic<StashFn> g_removeStashedAsset;

	extern std::atomic<StashFn> g_makeFirstStashedEntry;

	extern AllocateHeaderFn g_allocateHeader;

	extern FreeHeaderFn g_freeHeader;

	extern std::unique_ptr<State> g_state;

	std::uint64_t Mask(std::uint32_t index);

	bool IndexOf(const Entry* entry, std::uint32_t& index);

	Entry* StockPool();

	bool StockIndexOf(const Entry* entry, std::uint32_t& index);

	std::uint64_t* StockPlaneWord(std::size_t planeOffset, std::uint32_t index);

	bool StockTest(std::size_t planeOffset, std::uint32_t index);

	void StockSet(std::size_t planeOffset, std::uint32_t index);

	void StockClear(std::size_t planeOffset, std::uint32_t index);

	bool Test(Plane plane, std::uint32_t index);

	void Set(Plane plane, std::uint32_t index);

	void Clear(Plane plane, std::uint32_t index);

	std::uint32_t HashName(const char* name, unsigned type);

	const char* EntryName(const Entry* entry);

	bool AnyZoneLocal(const Entry* entry);

	bool UsePendingLocal(const Entry* entry);

	bool MarkUseOnceLocal(Entry* entry);

	void MarkLifecycleLocal(Entry* entry);

	void ClearLifecycleLocal(Entry* entry);

	void MarkZonePairLocal(Entry* entry, unsigned group);

	void MarkHeadResolvedLocal(Entry* entry);

	__declspec(noinline) void MarkLifecycleAware(Entry* entry);

	__declspec(noinline) void ClearLifecycleAware(Entry* entry);

	__declspec(noinline) void MarkZonePairAware(Entry* entry, unsigned group);

	__declspec(noinline) void MarkTransientHeadAware(Entry* entry);

	__declspec(noinline) Entry* NextStashAware(Entry* entry, uintptr_t caller);

	__declspec(noinline) void MarkResolvedHeadAware(Entry* entry);

	__declspec(noinline) bool __fastcall AnyZoneDetour(Entry* entry);

	__declspec(noinline) bool __fastcall TestUseOnceAware(Entry* entry);

	__declspec(noinline) bool __fastcall UsePendingDetour(Entry* entry);

	__declspec(noinline) std::uint64_t __fastcall UseTransitionOnceDetour(Entry* entry);

	__declspec(noinline) std::uint64_t __fastcall LifecycleMarkDetour(Entry* entry, int type, std::uint64_t value, char transformValue);

	bool InitializeState();

	__declspec(noinline) Entry* __fastcall Allocate(void* glob, unsigned type, std::uint16_t zoneIndex);

	__declspec(noinline) void __fastcall Free(void* glob, Entry* entry);

	__declspec(noinline) Entry* __fastcall FindAsset(void* glob, const char* name, unsigned type);

	__declspec(noinline) std::uint64_t __fastcall AddToTable(void* glob, Entry* entry);

	__declspec(noinline) std::uint64_t __fastcall RemoveFromTable(void* glob, Entry* entry);

	__declspec(noinline) std::uint64_t __fastcall AddToStashTail(void* glob, Entry* entry, Entry* head);

	__declspec(noinline) std::uint64_t __fastcall RemoveStashedAsset(void* glob, Entry* entry, Entry* previous);

	__declspec(noinline) std::uint64_t __fastcall MakeFirstStashedEntry(void* glob, Entry* entry, Entry* head);
}
