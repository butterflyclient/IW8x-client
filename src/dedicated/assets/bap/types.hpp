#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace bap::detail
{
#pragma pack(push, 1)
	struct Entry
	{
		union
		{
			void* header;
			Entry* freeNext;
		};

		std::uint32_t hashNext;
		std::uint32_t stashNext;
		std::uint8_t flags;
		std::uint8_t type;
		std::uint16_t zoneIndex;
	};
#pragma pack(pop)
	static_assert(sizeof(Entry) == 20);

	static_assert(offsetof(Entry, hashNext) == 8 && offsetof(Entry, stashNext) == 12);

	static_assert(offsetof(Entry, flags) == 16 && offsetof(Entry, type) == 17 && offsetof(Entry, zoneIndex) == 18);

	enum Plane : std::size_t
	{
		Allocation = 0,
		TableHead = 1,
		TableMember = 2,
		Lifecycle = 3,
		Zone0Primary = 4,
		Zone0Secondary = 5,
		Zone1Primary = 6,
		Zone1Secondary = 7,
		Zone2Primary = 8,
		Zone2Secondary = 9,
		UseOnce = 10,
		Unknown = 11,
		HeadResolved = 12,
		PlaneCount = 13,
	};

	struct State
	{
		std::unique_ptr<Entry[]> entries;
		std::unique_ptr<std::uint32_t[]> buckets;
		std::array<std::unique_ptr<std::uint64_t[]>, PlaneCount> planes;
		Entry* freeHead = nullptr;
	};

	using AllocateFn = Entry*(__fastcall*)(void* glob, unsigned type, std::uint16_t zoneIndex);

	using FreeFn = void(__fastcall*)(void* glob, Entry* entry);

	using FindAssetFn = Entry*(__fastcall*)(void* glob, const char* name, unsigned type);

	using EntryFn = std::uint64_t(__fastcall*)(void* glob, Entry* entry);

	using StashFn = std::uint64_t(__fastcall*)(void* glob, Entry* entry, Entry* headOrPrevious);

	using AllocateHeaderFn = void*(__fastcall*)(unsigned type);

	using FreeHeaderFn = void(__fastcall*)(unsigned type, void* header);

	using EntryPredicateFn = bool(__fastcall*)(Entry* entry);

	using UseTransitionOnceFn = std::uint64_t(__fastcall*)(Entry* entry);

	using LifecycleMarkFn = std::uint64_t(__fastcall*)(Entry* entry, int type, std::uint64_t value, char transformValue);

	using RegisteredPredicateFn = bool(__fastcall*)(const char* name, unsigned type);

	using RegisteredFollowupFn = void(__fastcall*)(const char* name, unsigned type);

	struct EntrySnapshot
	{
		uintptr_t address = 0;
		const char* pool = "neither";
		std::uint32_t index = ~0u;
		Entry fields{};
		bool readable = false;
	};

	struct BapEvent
	{
		std::uint64_t sequence = 0;
		const char* hook = nullptr;
		const char* phase = nullptr;
		uintptr_t caller = 0;
		DWORD tid = 0;
		EntrySnapshot entry;
		EntrySnapshot related; // Stash head/predecessor, when present.
		std::uint64_t arg0 = 0;
		std::uint64_t arg1 = 0;
	};

	struct EventSlot
	{
		SRWLOCK lock = SRWLOCK_INIT;
		BapEvent event;
	};

	struct HookSpec
	{
		uintptr_t rva;
		void* detour;
		const std::uint8_t* bytes;
		std::size_t size;
		void** trampoline;
		const char* name;
	};

	inline constexpr unsigned kCollisionTileLimit = 1024;

	using CollisionBits = std::array<std::uint32_t, kCollisionTileLimit / 32>;

	struct CollisionState
	{
		bool loadRequestedAll;
		bool loadedAll;
		bool setupRequestedAll;
		bool setupAll;
		std::uint8_t reserved04[0x40];
		int mode;
		const char* mapName;
		std::uint8_t reserved50[0x10];
		unsigned numTiles;
		bool closing;
		bool paused;
		std::uint8_t reserved66[0xA];
		CollisionBits loadRequested;
		CollisionBits loaded;
		CollisionBits setupRequested;
		CollisionBits setup;
		std::uint16_t transientIndices[kCollisionTileLimit];
		std::uint16_t fileIndices[kCollisionTileLimit];
	};

	static_assert(offsetof(CollisionState, mode) == 0x44 && offsetof(CollisionState, mapName) == 0x48);

	static_assert(offsetof(CollisionState, numTiles) == 0x60 && offsetof(CollisionState, paused) == 0x65);

	static_assert(offsetof(CollisionState, loadRequested) == 0x70 && offsetof(CollisionState, loaded) == 0xF0);

	static_assert(offsetof(CollisionState, setupRequested) == 0x170 && offsetof(CollisionState, setup) == 0x1F0);

	static_assert(offsetof(CollisionState, transientIndices) == 0x270 && offsetof(CollisionState, fileIndices) == 0xA70);

	static_assert(sizeof(CollisionState) == 0x1270);

	using CollisionProcessFn = void (*)(bool force);

	using CollisionLoadFn = void (*)(std::uint16_t fileIndex, unsigned priority);

	using CollisionTileFn = void (*)(unsigned tile);

	using CollisionUpdateFn = void (*)(unsigned tile, bool force);

	using GlassPieceStateChangedFn = void(__fastcall*)(unsigned pieceIndex, int oldState, const float* damagePos, const float* damageDir);

	using GlassGetSystemForInitialPieceFn = void*(__fastcall*)(unsigned pieceIndex, unsigned* outLocalPieceIndex);
}
