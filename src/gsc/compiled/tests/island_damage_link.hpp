#pragma once

namespace
{
	std::uint8_t* island_damage_linked_code{};
	bool island_damage_relocate{};

	void island_damage_native_link(void*, gsc::ScriptFile* script)
	{
		island_damage_linked_code = script->bytecode_;
		linked_input.assign(script->bytecode_, script->bytecode_ + script->bytecode_len_);
		if (island_damage_relocate)
			script->bytecode_[1] = 0x5A;
	}

	void island_damage_link_checks()
	{
		using namespace game::addresses::gsc;
		namespace abi = gsc::island_damage_native_abi;
		const auto source = island_damage_source();
		auto expected = source;
		for (const auto& site : abi::calls)
			expected[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id);
		const auto native_size = (source.size() + 7) & ~std::size_t{ 7 };
		island_resident.assign(native_size + gsc::probe_code_capacity, 0);
		std::copy(source.begin(), source.end(), island_resident.begin());
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		gsc::get_script_program_range = IslandProgramRange;
		gsc::original_link_file = island_damage_native_link;
		Check(VirtualAlloc(game::at(kServerContextRva & ~std::uintptr_t(0xFFF)), 0xD000,
			MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit damage linker context");
		ClearContext();
		auto* server = game::at(kServerContextRva);
		auto** map = game::at<const char*>(0x5cc7538);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 1;
		gsc::link_epoch = 65;
		gsc::installed.store(true);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		char packed = 'S';
		gsc::ScriptFile script{};
		script.name_ = abi::source_module.data();
		script.buffer_ = &packed;
		script.compressed_len_ = 1;
		script.len_ = 1;
		script.bytecode_ = island_resident.data();
		script.bytecode_len_ = static_cast<int>(source.size());
		const auto excluded = [&](void* context)
		{
			gsc::LinkScriptFile(context, &script);
			Check(linked_input == source && island_damage_linked_code == script.bytecode_ &&
				gsc::island_damage_rebound_code.empty(), "damage ABI escaped Island dedicated script load scope");
		};
		gameplay_fixture = false;
		excluded(server);
		gameplay_fixture = true;
		std::array<std::uint8_t,16> client_context{};
		excluded(client_context.data());
		*map = "mp_shipment";
		excluded(server);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 0;
		excluded(server);
		gsc::canonical_delta = 1;
		gsc::link_epoch = 0;
		excluded(server);
		gsc::link_epoch = 65;
		Context<std::uint8_t>(kLoadingScriptsOffset) = 0;
		excluded(server);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		Context<std::uint32_t>(kVMFrameCountOffset) = 1;
		excluded(server);
		Context<std::uint32_t>(kVMFrameCountOffset) = 0;

		script.name_ = "scripts/mp/gametypes/war.gsc";
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == source && gsc::island_damage_rebound_code.empty(),
			"damage ABI translated a different ScriptFile");
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		script.name_ = abi::source_module.data();
		island_damage_relocate = true;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == expected && island_damage_linked_code != script.bytecode_ &&
			gsc::island_damage_rebound_code.size() == 1,
			"damage ABI did not translate the selected private compiler copy");
		Check(std::equal(source.begin(), source.end(), script.bytecode_) &&
			script.buffer_ == &packed && packed == 'S' && script.bytecode_len_ == static_cast<int>(source.size()),
			"damage ABI modified native registered ScriptFile");
		const auto first_copy = island_damage_linked_code;
		gsc::LinkScriptFile(server, &script);
		auto relocated = expected;
		relocated[1] = 0x5A;
		Check(island_damage_linked_code == first_copy && linked_input == relocated &&
			gsc::island_damage_rebound_code.size() == 1,
			"damage ABI retranslated linker-relocated script on repeat");
		island_damage_relocate = false;
		gsc::link_epoch = 66;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == expected && gsc::island_damage_rebound_code.size() == 1,
			"damage ABI retained stale script identity after new map epoch");
		gsc::ResetCompactCode();
		Check(gsc::island_damage_rebound_code.empty(), "damage ABI retained state across zone reset");
		gsc::installed.store(false);
		gsc::canonical_delta = 0;
		gsc::link_epoch = 0;
		*map = nullptr;
		ClearContext();
		gsc::get_script_program_range = FakeProgramRange;
		std::cout << "Island damage ABI owner, opcodes, fingerprints, private copy, reload and scope checks passed\n";
	}
}
