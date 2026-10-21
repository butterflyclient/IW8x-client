#pragma once

// Production allocation/fixup/free hooks around the Replay preload contract.
namespace island_builtins
{
	void install() {}
}

namespace
{
	struct IslandZone
	{
		gsc::ZoneAllocation allocations[4]{};
		void* temporary{};
	};
	struct IslandTemporary
	{
		std::uint8_t prefix[0x540]{};
		gsc::BlockLayout blocks[8]{};
	};
	static_assert(offsetof(IslandZone, temporary) == 0x60);
	static_assert(offsetof(IslandTemporary, blocks) == 0x540);

	std::vector<std::uint8_t> island_preload, island_resident;
	bool island_preloading{};
	std::uint64_t island_allocated_size{};
	unsigned island_fixups{}, island_frees{};

	std::uint8_t* IslandProgramRange(std::uint32_t* size)
	{
		*size = static_cast<std::uint32_t>(island_resident.size());
		return island_resident.data();
	}

	void IslandAllocate(const std::uint64_t* sizes, void*, void* owner, void* raw_layouts)
	{
		auto& zone = *static_cast<IslandZone*>(owner);
		auto* layouts = static_cast<gsc::BlockLayout*>(raw_layouts);
		auto& storage = island_preloading ? island_preload : island_resident;
		island_allocated_size = sizes[6];
		Check(sizes[6] <= storage.size(), "test allocation overflow");
		zone.allocations[island_preloading ? 2 : 1] = { 0, 0, storage.data(), sizes[6] };
		layouts[6] = { reinterpret_cast<std::uintptr_t>(storage.data()), sizes[6] };
	}

	void IslandFixup(void* owner, const char*)
	{
		++island_fixups;
		auto& zone = *static_cast<IslandZone*>(owner);
		auto& block = static_cast<IslandTemporary*>(zone.temporary)->blocks[6];
		// Native 11A9160 transfers the entire preloaded block through the
		// internal allocator, bypassing the outer 11A8C20 allocation hook.
		std::memcpy(island_resident.data(), reinterpret_cast<void*>(block.address), block.size);
		zone.allocations[1] = { 0, 0, island_resident.data(), block.size };
		block.address = reinterpret_cast<std::uintptr_t>(island_resident.data());
	}

	void IslandFree(void*)
	{
		++island_frees;
	}

	void IslandReservationChecks()
	{
		island_preload.resize(gsc::probe_code_capacity + 64, 0xA5);
		island_resident.resize(island_preload.size(), 0xCD);
		gsc::get_script_program_range = IslandProgramRange;
		gsc::original_alloc_zone_memory = IslandAllocate;
		gsc::original_fixup_preload = IslandFixup;
		gsc::original_free_zone = IslandFree;
		gsc::canonical_delta = 1;
		SetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", "1");
		IslandTemporary temporary;
		IslandZone owner;
		owner.temporary = &temporary;
		std::array<std::uint64_t, 8> sizes{};
		sizes[6] = 32;
		char zone_name[] = "magma_island_scripts";
		gsc::ScriptFile script{};
		script.bytecode_len_ = 16;
		script.bytecode_ = island_resident.data();

		island_preloading = true;
		gsc::AllocZoneMemory(sizes.data(), zone_name, &owner, temporary.blocks);
		Check(sizes[6] == 32 && island_allocated_size == 32 + gsc::probe_code_capacity,
			"reservation changed original sizes or reserved more than once");
		Check(!gsc::probe_code_begin && !gsc::CompactCompilerCode(script, 1),
			"temporary preload memory was admitted as linked bytecode");
		gsc::FixupPreloadScriptMemory(&owner, zone_name);
		Check(island_fixups == 1 && gsc::probe_code_begin == island_resident.data() + 32,
			"native promotion did not publish the relocated reservation");
		Check(gsc::CompactCompilerCode(script, 1) && script.bytecode_ == island_resident.data() + 32,
			"client first script still fails after native preload promotion");
		Check(std::all_of(script.bytecode_, script.bytecode_ + 16, [](auto value) { return value == 0xA5; }),
			"promoted bytecode contents changed");
		script.bytecode_ = island_resident.data();
		Check(gsc::CompactCompilerCode(script, 1) && gsc::probe_code_used == 16,
			"repeat link consumed additional reservation");
		script.bytecode_ = island_resident.data();
		island_resident[0] = 0xBB;
		Check(gsc::CompactCompilerCode(script, 2) && script.bytecode_[0] == 0xBB && gsc::probe_code_used == 16,
			"full reload reused stale compact code");
		script.bytecode_ = island_resident.data();
		script.bytecode_len_ = static_cast<int>(gsc::probe_code_capacity + 1);
		Check(!gsc::CompactCompilerCode(script, 2), "signed24 reservation overflow was accepted");

		IslandZone unrelated;
		gsc::FreeZoneMemory(&unrelated);
		Check(gsc::probe_code_begin != nullptr, "unrelated zone release invalidated Island code");
		gsc::FreeZoneMemory(&owner);
		script.bytecode_len_ = 16;
		Check(!gsc::probe_code_owner && !gsc::probe_code_begin && !gsc::CompactCompilerCode(script, 3) && island_frees == 2,
			"released zone retained compact code while heap memory remained writable");

		island_preloading = false;
		gsc::AllocZoneMemory(sizes.data(), zone_name, &owner, temporary.blocks);
		Check(gsc::probe_code_begin == island_resident.data() + 32 && gsc::CompactCompilerCode(script, 4),
			"direct dedicated allocation regressed");
		gsc::FreeZoneMemory(&owner);
		char stock_name[] = "common_mp";
		gsc::AllocZoneMemory(sizes.data(), stock_name, &owner, temporary.blocks);
		Check(island_allocated_size == 32 && !gsc::probe_code_owner, "stock zone allocation was expanded");
		SetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", nullptr);
		gsc::AllocZoneMemory(sizes.data(), zone_name, &owner, temporary.blocks);
		Check(island_allocated_size == 32 && !gsc::probe_code_owner, "disabled probe expanded a zone");

		Check(VirtualAlloc(game::at(0x5cc7000), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit collision map slot");
		auto** map = game::at<const char*>(0x5cc7538);
		*map = "mp_sm_island_1";
		gameplay_fixture = false;
		Check(!gsc::IslandScriptLoad(), "preloaded Island redirected frontend scripts");
		Check(!gsc::DiskOverrideAllowed("scripts/mp/gametypes/br_quest_util.gsc"),
			"Island quest override touched frontend scripts");
		gameplay_fixture = true;
		Check(gsc::IslandScriptLoad(), "Island gameplay compaction disabled");
		Check(gsc::DiskOverrideAllowed("scripts/mp/gametypes/br_quest_util.gsc"),
			"Island quest override was not enabled for Island server load");
		*map = "mp_shipment";
		Check(!gsc::IslandScriptLoad(), "stock gameplay scripts redirected into Island code");
		Check(!gsc::DiskOverrideAllowed("scripts/mp/gametypes/br_quest_util.gsc") &&
			gsc::DiskOverrideAllowed("scripts/mp/gametypes/br_black_market_quest.gsc"),
			"Island quest override gate affected stock maps or unrelated scripts");
		gsc::canonical_delta = 0;
		*map = "mp_sm_island_1";
		Check(!gsc::IslandScriptLoad(), "disabled Island probe redirected scripts");
		Check(!gsc::DiskOverrideAllowed("scripts/mp/gametypes/br_quest_util.gsc"),
			"disabled Island probe still selected converted quest script");
		*map = nullptr;
		gsc::get_script_program_range = FakeProgramRange;
		std::cout << "Island preload promotion, direct load, reload, release, capacity and frontend scope checks passed\n";
	}
}
