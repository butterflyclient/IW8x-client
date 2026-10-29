#pragma once

namespace
{
	std::uint8_t* plunder_validation_linked_code{};
	bool plunder_validation_relocate{};

	void plunder_validation_native_link(void*, gsc::ScriptFile* script)
	{
		plunder_validation_linked_code = script->bytecode_;
		linked_input.assign(script->bytecode_, script->bytecode_ + script->bytecode_len_);
		if (plunder_validation_relocate) script->bytecode_[1] = 0x5A;
	}

	void island_plunder_validation_link_checks()
	{
		using namespace game::addresses::gsc;
		namespace abi = gsc::island_plunder_validation_native_abi;
		const std::array sources{ plunder_validation_source(0), plunder_validation_source(1) };
		std::array<std::vector<std::uint8_t>, 2> expected = sources;
		for (std::size_t i = 0; i < abi::source_profiles.size(); ++i)
			for (const auto& site : abi::source_profiles[i].calls)
				expected[i][site.offset + 1] = static_cast<std::uint8_t>(site.replay_id);
		const auto second_offset = (sources[0].size() + 7) & ~std::size_t{ 7 };
		const auto native_size = second_offset + ((sources[1].size() + 7) & ~std::size_t{ 7 });
		island_resident.assign(native_size + gsc::probe_code_capacity, 0);
		std::copy(sources[0].begin(), sources[0].end(), island_resident.begin());
		std::copy(sources[1].begin(), sources[1].end(), island_resident.begin() + second_offset);
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		gsc::get_script_program_range = IslandProgramRange;
		gsc::original_link_file = plunder_validation_native_link;
		Check(VirtualAlloc(game::at(kServerContextRva & ~std::uintptr_t(0xFFF)), 0xD000,
			MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit Plunder validation native linker context");
		ClearContext();
		auto* server = game::at(kServerContextRva);
		auto** map = game::at<const char*>(0x5cc7538);
		*map = "mp_sm_island_1";
		gsc::canonical_delta = 1;
		gsc::link_epoch = 75;
		gsc::installed.store(true);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		char packed = 'S';
		std::array<gsc::ScriptFile, 2> scripts{};
		for (std::size_t i = 0; i < scripts.size(); ++i)
		{
			auto& script = scripts[i];
			script.name_ = abi::source_profiles[i].module.data();
			script.buffer_ = &packed;
			script.compressed_len_ = 1;
			script.len_ = 1;
			script.bytecode_ = island_resident.data() + (i == 0 ? 0 : second_offset);
			script.bytecode_len_ = static_cast<int>(sources[i].size());
		}
		const auto excluded = [&](void* context)
		{
			for (std::size_t i = 0; i < scripts.size(); ++i)
			{
				gsc::LinkScriptFile(context, &scripts[i]);
				Check(linked_input == sources[i] && plunder_validation_linked_code == scripts[i].bytecode_ &&
					gsc::island_plunder_validation_rebound_code.empty(),
					"Plunder validation ABI escaped the isolated Island server load gate");
			}
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
		gsc::link_epoch = 75;
		Context<std::uint8_t>(kLoadingScriptsOffset) = 0;
		excluded(server);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		Context<std::uint32_t>(kVMFrameCountOffset) = 1;
		excluded(server);
		Context<std::uint32_t>(kVMFrameCountOffset) = 0;

		// A stock or unrelated numeric module must not be mistaken for Source
		// validation even if the private bytecode happens to resemble this input.
		scripts[0].name_ = "scripts/mp/validation.gsc";
		gsc::LinkScriptFile(server, &scripts[0]);
		Check(linked_input == sources[0] && gsc::island_plunder_validation_rebound_code.empty(),
			"Plunder validation ABI patched a stock or unrelated script identity");
		scripts[0].name_ = abi::source_profiles[0].module.data();
		gsc::ResetCompactCode();
		gsc::probe_code_begin = island_resident.data() + native_size;
		plunder_validation_relocate = true;
		std::array<std::uint8_t*, 2> first_copies{};
		for (std::size_t i = 0; i < scripts.size(); ++i)
		{
			gsc::LinkScriptFile(server, &scripts[i]);
			first_copies[i] = plunder_validation_linked_code;
			Check(linked_input == expected[i] && first_copies[i] != scripts[i].bytecode_ &&
				gsc::island_plunder_validation_rebound_code.size() == i + 1,
				"Plunder validation ABI failed to translate the private compiler copy");
			Check(std::equal(sources[i].begin(), sources[i].end(), scripts[i].bytecode_) &&
				scripts[i].buffer_ == &packed && packed == 'S' &&
				scripts[i].bytecode_len_ == static_cast<int>(sources[i].size()),
				"Plunder validation ABI overwrote a native ScriptFile or registered source");
		}
		for (std::size_t i = 0; i < scripts.size(); ++i)
		{
			gsc::LinkScriptFile(server, &scripts[i]);
			auto relocated = expected[i];
			relocated[1] = 0x5A;
			Check(plunder_validation_linked_code == first_copies[i] && linked_input == relocated &&
				gsc::island_plunder_validation_rebound_code.size() == 2,
				"Plunder validation ABI retranslated a relocated active script");
		}
		plunder_validation_relocate = false;
		gsc::link_epoch = 76;
		for (std::size_t i = 0; i < scripts.size(); ++i)
		{
			gsc::LinkScriptFile(server, &scripts[i]);
			Check(linked_input == expected[i] && gsc::island_plunder_validation_rebound_code.size() == i + 1,
				"Plunder validation ABI kept stale code identity across script epoch");
		}
		gsc::ResetCompactCode();
		Check(gsc::island_plunder_validation_rebound_code.empty(),
			"Plunder validation ABI retained stale translations across zone unload");
		gsc::installed.store(false);
		gsc::canonical_delta = 0;
		gsc::link_epoch = 0;
		*map = nullptr;
		ClearContext();
		gsc::get_script_program_range = FakeProgramRange;
		std::cout << "Island Plunder validation ABI owner, private compiler copies, reload, stock exclusion and scope checks passed\n";
	}
}
