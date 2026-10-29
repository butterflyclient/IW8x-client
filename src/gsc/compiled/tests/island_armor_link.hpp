#pragma once

namespace
{
	std::uint8_t* island_armor_linked_code{};
	bool island_armor_relocate_link{};

	void island_armor_native_link(void*, gsc::ScriptFile* script)
	{
		island_armor_linked_code = script->bytecode_;
		linked_input.assign(script->bytecode_, script->bytecode_ + script->bytecode_len_);
		if (island_armor_relocate_link)
			script->bytecode_[1] = 0x5A;
	}

	void island_armor_one_link_check(const std::string_view name,
		const std::vector<std::uint8_t>& source, const std::size_t target_offset,
		const std::uint8_t replacement, std::unordered_set<std::uintptr_t>& translated_set)
	{
		using namespace game::addresses::gsc;
		auto translated = source;
		translated[target_offset] = replacement;
		const auto native_size = (source.size() + 7) & ~std::size_t{ 7 };
		island_resident.assign(native_size + gsc::probe_code_capacity, 0);
		std::copy(source.begin(), source.end(), island_resident.begin());
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		gsc::get_script_program_range = IslandProgramRange;
		gsc::original_link_file = island_armor_native_link;
		Check(VirtualAlloc(game::at(kServerContextRva & ~std::uintptr_t(0xFFF)), 0xD000,
			MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit armor linker context");
		ClearContext();
		auto* server = game::at(kServerContextRva);
		auto** map = game::at<const char*>(0x5cc7538);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 1;
		gsc::link_epoch = 84;
		gsc::installed.store(true);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		char packed = 'S';
		gsc::ScriptFile script{};
		const auto selected_name = std::string(name);
		script.name_ = selected_name.c_str();
		script.buffer_ = &packed;
		script.compressed_len_ = 1;
		script.len_ = 1;
		script.bytecode_ = island_resident.data();
		script.bytecode_len_ = static_cast<int>(source.size());

		const auto excluded = [&](void* context)
		{
			gsc::LinkScriptFile(context, &script);
			Check(linked_input == source && island_armor_linked_code == script.bytecode_ &&
				translated_set.empty(), "armor ABI escaped verified Island/dedicated scope");
		};
		gameplay_fixture = false;
		excluded(server);
		gameplay_fixture = true;
		std::array<std::uint8_t, 16> client_context{};
		excluded(client_context.data());
		*map = "mp_shipment";
		excluded(server);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 0;
		excluded(server);
		gsc::canonical_delta = 1;
		gsc::link_epoch = 0;
		excluded(server);
		gsc::link_epoch = 84;
		Context<std::uint8_t>(kLoadingScriptsOffset) = 0;
		excluded(server);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		Context<std::uint32_t>(kVMFrameCountOffset) = 1;
		excluded(server);
		Context<std::uint32_t>(kVMFrameCountOffset) = 0;

		script.name_ = "scripts/mp/gametypes/war.gsc";
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == source && translated_set.empty(),
			"armor native adapter rewrote script other than numeric verified source");
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		script.name_ = selected_name.c_str();
		island_armor_relocate_link = true;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == translated && island_armor_linked_code != script.bytecode_ &&
			translated_set.size() == 1,
			"armor ABI did not translate private selected compiler view");
		Check(std::equal(source.begin(), source.end(), script.bytecode_) &&
			script.buffer_ == &packed && packed == 'S' &&
			script.bytecode_len_ == static_cast<int>(source.size()),
			"armor ABI modified registered stock/native ScriptFile");
		const auto copied_ptr = island_armor_linked_code;
		gsc::LinkScriptFile(server, &script);
		auto relocated = translated;
		relocated[1] = 0x5A;
		Check(linked_input == relocated && island_armor_linked_code == copied_ptr &&
			translated_set.size() == 1,
			"armor ABI reapplied native operand changes to linked bytecode");
		island_armor_relocate_link = false;
		gsc::link_epoch = 85;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == translated && translated_set.size() == 1,
			"armor ABI retained stale translation after new map epoch");
		gsc::ResetCompactCode();
		Check(translated_set.empty(), "armor ABI retained translation across zone reset");
		gsc::installed.store(false);
		gsc::canonical_delta = 0;
		gsc::link_epoch = 0;
		*map = nullptr;
		ClearContext();
		gsc::get_script_program_range = FakeProgramRange;
	}

	void island_armor_link_checks()
	{
		island_armor_one_link_check(gsc::island_armor_native_abi::source_module,
			island_armor_source(), gsc::island_armor_native_abi::source_method_opcode + 1,
			static_cast<std::uint8_t>(gsc::island_armor_native_abi::replay_method_id),
			gsc::island_armor_rebound_code);
		island_armor_one_link_check(gsc::island_armor_switch_native_abi::source_module,
			island_armor_switch_source(), gsc::island_armor_switch_native_abi::method_opcode + 1,
			static_cast<std::uint8_t>(gsc::island_armor_switch_native_abi::replay_id),
			gsc::island_armor_switch_rebound_code);
		std::cout << "Island armor two GSC modules: selected identity, private copy, reload, exclusion, rollback scope passed\n";
	}
}
