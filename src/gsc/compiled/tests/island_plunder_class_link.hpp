#pragma once

namespace
{
	std::uint8_t* island_plunder_class_linked_code{};
	bool island_plunder_class_relocate{};

	void island_plunder_class_native_link(void*, gsc::ScriptFile* script)
	{
		island_plunder_class_linked_code = script->bytecode_;
		linked_input.assign(script->bytecode_, script->bytecode_ + script->bytecode_len_);
		if (island_plunder_class_relocate) script->bytecode_[1] = 0x5A;
	}

	void island_plunder_class_link_checks()
	{
		using namespace game::addresses::gsc;
		namespace abi = gsc::island_plunder_class_native_abi;
		const auto source = island_plunder_class_source();
		auto translated = source;
		for (const auto& call : abi::read_loops)
			translated[call.offset + 1] = static_cast<std::uint8_t>(call.replay_count);
		const auto native_size = (source.size() + 7) & ~std::size_t{ 7 };
		island_resident.assign(native_size + gsc::probe_code_capacity, 0);
		std::copy(source.begin(), source.end(), island_resident.begin());
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		gsc::get_script_program_range = IslandProgramRange;
		gsc::original_link_file = island_plunder_class_native_link;
		Check(VirtualAlloc(game::at(kServerContextRva & ~std::uintptr_t(0xFFF)), 0xD000,
			MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit class linker context");
		ClearContext();
		auto* server = game::at(kServerContextRva);
		auto** map = game::at<const char*>(0x5cc7538);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 1;
		gsc::link_epoch = 70;
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
			Check(linked_input == source && island_plunder_class_linked_code == script.bytecode_ &&
				gsc::island_plunder_class_rebound_code.empty(),
				"class ABI escaped its Island dedicated linker boundary");
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
		gsc::link_epoch = 70;
		Context<std::uint8_t>(kLoadingScriptsOffset) = 0;
		excluded(server);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		Context<std::uint32_t>(kVMFrameCountOffset) = 1;
		excluded(server);
		Context<std::uint32_t>(kVMFrameCountOffset) = 0;

		script.name_ = "scripts/mp/class.gsc";
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == source && gsc::island_plunder_class_rebound_code.empty(),
			"class ABI translated a name not present in the installed source namespace");
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		script.name_ = abi::source_module.data();
		island_plunder_class_relocate = true;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == translated && island_plunder_class_linked_code != script.bytecode_ &&
			gsc::island_plunder_class_rebound_code.size() == 1,
			"class ABI did not translate the selected private compiler copy");
		Check(std::equal(source.begin(), source.end(), script.bytecode_) &&
			script.buffer_ == &packed && packed == 'S' &&
			script.bytecode_len_ == static_cast<int>(source.size()),
			"class ABI modified the registered source asset");
		const auto first_copy = island_plunder_class_linked_code;
		gsc::LinkScriptFile(server, &script);
		auto relocated = translated;
		relocated[1] = 0x5A;
		Check(linked_input == relocated && island_plunder_class_linked_code == first_copy &&
			gsc::island_plunder_class_rebound_code.size() == 1,
			"repeat class link retranslated relocated bytecode");
		island_plunder_class_relocate = false;
		gsc::link_epoch = 71;
		gsc::LinkScriptFile(server, &script);
		Check(linked_input == translated && gsc::island_plunder_class_rebound_code.size() == 1,
			"new map epoch retained stale class translation state");
		gsc::ResetCompactCode();
		Check(gsc::island_plunder_class_rebound_code.empty(),
			"class translation state survived a full zone reset");
		gsc::installed.store(false);
		gsc::canonical_delta = 0;
		gsc::link_epoch = 0;
		*map = nullptr;
		ClearContext();
		gsc::get_script_program_range = FakeProgramRange;
		std::cout << "Island Plunder class ABI private copy, reload, exclusion and scope checks passed\n";
	}
}
