// Exercise the production registry, file hooks and dependency scheduler.
#include "diagnostics/night/night_files.cpp"
#include "client/frontend/menu_art1645.cpp"
#include "client/frontend/custom_map_names.cpp"
#include "diagnostics/fastfile.cpp"
#include "engine/asset_pools.cpp"
#include "diagnostics/night/night_entry_pool.cpp"
#include "diagnostics/night/night_animtrees.cpp"
#include "dedicated/assets/bap/collision.cpp"
#include "diagnostics/island/island_loot.cpp"
#include "diagnostics/island/island_operator_table.hpp"
#include "network/client_playlist_map.hpp"

#include <iostream>
#include <stdexcept>

namespace console { void Print(const char*, ...) {} }

namespace
{
	unsigned checks{}, open_calls{}, loose_calls{}, package_calls{}, signature_calls{}, translate_calls{};
	const char* translate(const char* key) { ++translate_calls; return key; }
	std::string opened;
	unsigned opened_flags{};
	const menu_art1645::ZoneInfo* received{};
	std::vector<menu_art1645::ZoneInfo> scheduled;
	struct pending_zone
	{
		std::string base, actual;
		uint32_t flags, failure_mode, priority;
		uint8_t allow_missing;
	};
	std::vector<pending_zone> pending_zones;
	std::vector<std::string> packages;
	unsigned collision_forwarded{}, collision_loaded{}, collision_setup{};
	const char* tree_name{};
	void* tree_context{};
	unsigned asset_lookup_calls{}, asset_lookup_type{};
	const char* asset_lookup_name{};
	night_animtrees::asset_info* asset_lookup_output{};
	night_animtrees::asset_info asset_lookup_value{};
	bool asset_lookup_found{ true };
	bool read_asset_info(unsigned int type, const char* name, night_animtrees::asset_info* info)
	{
		++asset_lookup_calls;
		asset_lookup_type = type;
		asset_lookup_name = name;
		asset_lookup_output = info;
		if (asset_lookup_found && info) *info = asset_lookup_value;
		return asset_lookup_found;
	}
	const char* read_tree(void* context, const char* name)
	{
		tree_context = context;
		tree_name = name;
		return "native tree buffer";
	}
	void collision_native(bool) { ++collision_forwarded; }
	void collision_load(uint16_t index, unsigned priority)
	{
		if (index >= 101 && index <= 102 && priority == 10000) ++collision_loaded;
	}
	void collision_update(unsigned tile, bool)
	{
		bap::detail::g_collision->loaded[0] |= 0x80000000u >> tile;
	}
	void collision_world(unsigned) { ++collision_setup; }
	void collision_models(unsigned tile)
	{
		bap::detail::g_collision->setup[0] |= 0x80000000u >> tile;
	}

	void expect(bool pass, const char* message)
	{
		++checks;
		if (!pass) throw std::runtime_error(message);
	}

	void* open(void*, unsigned, const char* name, unsigned flags)
	{
		++open_calls;
		opened = name;
		opened_flags = flags;
		return reinterpret_cast<void*>(0x1234);
	}
	bool exists(void*, unsigned, const char*) { ++open_calls; return false; }
	HANDLE loose(unsigned, const char* name, unsigned flags)
	{
		++loose_calls;
		opened = name;
		opened_flags = flags;
		return INVALID_HANDLE_VALUE;
	}
	HANDLE open_path(const char* name, unsigned flags)
	{
		opened = name;
		opened_flags = flags;
		return CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	}
	bool read_header(HANDLE file)
	{
		if (file == INVALID_HANDLE_VALUE) return false;
		char header[8]{};
		DWORD count{};
		const bool pass = ReadFile(file, header, sizeof(header), &count, nullptr) && count == sizeof(header) &&
			std::memcmp(header, "IWffc100", sizeof(header)) == 0;
		CloseHandle(file);
		return pass;
	}
	bool load_packages(const char*) { ++package_calls; return false; }
	unsigned find_package(const char* name)
	{
		return std::find(packages.begin(), packages.end(), name) != packages.end() ? 1 : 128;
	}
	void add_package(unsigned, const char* name) { packages.emplace_back(name); }
	bool signature(const void*, const void*) { ++signature_calls; return false; }
	bool verify_pack(const void*, const void* header)
	{
		const auto* bytes = static_cast<const uint8_t*>(header);
		return fastfile::VerifySignature(bytes + 24, bytes + 56);
	}
	uint64_t __fastcall schedule(const menu_art1645::ZoneInfo* zones, unsigned count, uint8_t mode, uint8_t missing)
	{
		received = zones;
		scheduled.assign(zones, zones + count);
		expect(mode == 7 && missing == 9, "native scheduling parameters changed");
		return 42;
	}
	void __fastcall pending_enqueue(const char* base, const char* actual,
		uint32_t flags, uint8_t allowMissing, uint32_t failureMode, uint32_t priority)
	{
		pending_zones.push_back({ base, actual, flags, failureMode, priority, allowMissing });
	}

	struct temporary_directory
	{
			inline static std::atomic<unsigned> serial{ 0 };
		std::filesystem::path path = std::filesystem::temp_directory_path() /
				("iw8x-fastfile-tests-" + std::to_string(GetCurrentProcessId()) + "-" +
					std::to_string(GetTickCount64()) + "-" + std::to_string(serial.fetch_add(1)));
		temporary_directory() { std::filesystem::create_directories(path); }
		~temporary_directory() { std::error_code error; std::filesystem::remove_all(path, error); }
	};
}

int main(int argc, char** argv)
{
	try
	{
		if (argc == 3 && std::string_view(argv[1]) == "--verify-xmas-empty-srv")
		{
			// Validate the exact five user-installed files through the real
			// production registry and intercepted loose-file path. This test
			// never launches Replay or changes the installed files.
			using namespace custom_fastfiles;
			const std::filesystem::path root(argv[2]);
			const auto& spec = night_files::xmas_empty_srv_poc_files;
			const auto& bundle = night_files::xmas_empty_srv_poc_bundle;
			const auto installed = root / bundle.folder;
			expect(std::size(spec) == 5 && std::string_view(bundle.zone) == "mp_jup_shipment_xmas" &&
				std::string_view(bundle.folder) == "magma/maps/mp_jup_shipment_xmas", "Christmas PoC bundle identity changed");
			expect(night_files::xmas_empty_srv_poc_dedicated_command_line(
				L"\"C:\\Game Folder\\game_dx12_ship_replay.exe\" --dedi --map mp_shipment --mode war") &&
				!night_files::xmas_empty_srv_poc_dedicated_command_line(L"server.exe --dedicated --map mp_jup_shipment_xmas") &&
				!night_files::xmas_empty_srv_poc_dedicated_command_line(L"server.exe \"--dedi\"oops") &&
				!night_files::xmas_empty_srv_poc_dedicated_command_line(L"server.exe --dedi \"unclosed"),
				"Christmas PoC CLI opt-in failed dedicated exact-token guard");
			registry original;
			expect(original.initialize(root, { &bundle, 1 }), "installed Christmas PoC bytes do not match production hash/header gate");
			expect(original.map_enabled("mp_jup_shipment_xmas") && !original.map_enabled("mp_shipment") &&
				original.before("mp_jup_shipment_xmas").empty() && original.cache_packages("mp_jup_shipment_xmas").empty(),
				"Christmas PoC leaked dependencies/caches or stock map registration");
			for (const auto& item : spec)
			{
				const int index = original.find(item.name);
				expect(index >= 0 && original.header(unsigned(index)) &&
					std::memcmp(original.header(unsigned(index))->data(), "IWffc100", 8) == 0 &&
					std::filesystem::equivalent(original.path(unsigned(index)), installed / item.name),
					"exact physical Christmas PoC FF was not mapped to native file registry");
			}
			for (const char* unknown : { "mp_shipment.ff", "../srv_mp_jup_shipment_xmas.ff", "srv_mp_jup_shipment_xmas.ff.extra" })
				expect(original.find(unknown) < 0, "unrelated/traversal native FF lookup incorrectly admitted");
			night_files::files = std::move(original);
			fastfile::g_open.store(&open);
			fastfile::g_exists.store(&exists);
			fastfile::g_loose.store(&loose);
			fastfile::g_openPath.store(&open_path);
			const char* srv = "srv_mp_jup_shipment_xmas.ff";
			expect(fastfile::Find(srv) >= fastfile::kNightFileBase && fastfile::Exists(nullptr, 5, srv),
				"native custom provider does not recognize hash-pinned Christmas srv");
			fastfile::Open(nullptr, 5, srv, 0x21);
			expect(opened == std::string(fastfile::kAlias) + srv, "native CASC custom-file alias missing");
			expect(read_header(fastfile::Loose(5, srv, 0)) &&
				std::filesystem::equivalent(std::filesystem::path(opened), installed / srv),
				"native loose-file provider failed to open user-installed Christmas srv");
			expect(read_header(fastfile::Loose(5, "__magma120_converted_srv_mp_jup_shipment_xmas.ff", 0x19)),
				"native CASC alias failed to open user-installed Christmas srv");
			// Unit-control the existing DB_LoadFastfiles hook with a fake native
			// queue; the real game loader is *not* called by this test.
			char priorLoadEnv[8]{};
			const DWORD priorLoadEnvLength = GetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", priorLoadEnv,
				DWORD(std::size(priorLoadEnv)));
			SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", "1");
			night_files::xmas_empty_srv_poc_ready = true;
			menu_art1645::g_original.store(&schedule);
			menu_art1645::g_xmas_srv_poc_scheduled.store(false);
			const menu_art1645::ZoneInfo ordinary[]{ { "mp_shipment", 0x21, 1, 3, 5 } };
			expect(menu_art1645::load_fastfiles(ordinary, 1, 7, 9) == 42 && received == ordinary &&
				scheduled.size() == 1, "empty Christmas srv injected into an unrelated native map batch");
			const menu_art1645::ZoneInfo realServer[]{ { "srv_mp_shipment", 0x88, 2, 5, 17 } };
			expect(menu_art1645::load_fastfiles(realServer, 1, 7, 9) == 42 && scheduled.size() == 2 &&
				std::string_view(scheduled[0].name) == "srv_mp_shipment" &&
				std::string_view(scheduled[1].name) == "srv_mp_jup_shipment_xmas" &&
				scheduled[1].flags == realServer[0].flags &&
				scheduled[1].failureMode == realServer[0].failureMode &&
				scheduled[1].priority == realServer[0].priority && scheduled[1].reserved == realServer[0].reserved,
				"one-shot Christmas srv scheduling changed native server descriptor flags, lifetime, or order");
			expect(menu_art1645::load_fastfiles(realServer, 1, 7, 9) == 42 && received == realServer &&
				scheduled.size() == 1, "one-shot Christmas srv request repeated on a native restart");
			menu_art1645::g_xmas_srv_poc_scheduled.store(false);
			expect(menu_art1645::authentic_native_srv_companion("mp_backlot2", "srv_mp_backlot2") &&
				menu_art1645::authentic_native_srv_companion("mp_shipment", "srv_mp_shipment") &&
				menu_art1645::authentic_native_srv_companion("mp_jup_shipment_xmas", "srv_mp_jup_shipment_xmas"),
				"native signed F65870 actual base/derived server-zone relationship rejected");
			expect(menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_backlot2", 4, true, false) &&
				menu_art1645::xmas_native_enqueue_eligible("mp_shipment", "srv_mp_shipment", 0x787, true, false),
				"authenticated stock srv pair with spare native DB capacity incorrectly blocked");
			expect(!menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_backlot2", 0x788, true, false) &&
				!menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_backlot2", 0x789, true, false) &&
				!menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_backlot2", 4, false, false) &&
				!menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_backlot2", 4, true, true) &&
				!menu_art1645::xmas_native_enqueue_eligible("mp_jup_shipment_xmas", "srv_mp_jup_shipment_xmas", 4, true, false) &&
				!menu_art1645::xmas_native_enqueue_eligible("mp_backlot2", "srv_mp_shipment", 4, true, false),
				"Christmas native pending-row probe admitted self-injection, duplicate, capacity overflow or wrong source");
			for (const auto& pair : {
				std::pair{ "mp_backlot2", "ww_mp_backlot2" },
				std::pair{ "mp_backlot2", "srv_mp_shipment" },
				std::pair{ "mp_backlot2", "srv_mp_backlot2.ff" },
				std::pair{ "srv_mp_backlot2", "srv_srv_mp_backlot2" },
				std::pair{ "mp_", "srv_mp_" },
				std::pair{ "mp_jup_shipment_xmas", "srv_mp_jup_shipment_xmas_suffix" },
				std::pair{ "mp_backlot2", "srv_mp_backlot2\\evil" },
					std::pair{ "mp_../evil", "srv_mp_../evil" },
					std::pair{ "mp_backlot2;evil", "srv_mp_backlot2;evil" },
					std::pair{ "mp_backlot2-sub", "srv_mp_backlot2-sub" },
			})
				expect(!menu_art1645::authentic_native_srv_companion(pair.first, pair.second),
					"native server row gate accepted wrong family/zone/traversal");
			menu_art1645::g_native_enqueue_original.store(&pending_enqueue);
			pending_zones.clear();
			expect(menu_art1645::load_fastfiles(realServer, 1, 7, 9) == 42 && received == realServer &&
				scheduled.size() == 1, "upper scheduler still forged srv_srv_* after genuine pending row hook became active");
			SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", "0");
			menu_art1645::native_pending_enqueue("mp_backlot2", "srv_mp_backlot2", 0x88, 9, 2, 5);
			expect(pending_zones.size() == 1 && pending_zones.front().base == "mp_backlot2" &&
				pending_zones.front().actual == "srv_mp_backlot2" && pending_zones.front().flags == 0x88 &&
				pending_zones.front().allow_missing == 9 && pending_zones.front().failure_mode == 2 &&
				pending_zones.front().priority == 5 && !menu_art1645::g_xmas_srv_poc_scheduled.load(),
				"disabled pending-zone hook changed native stock request arguments");
			SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", "0");
			expect(menu_art1645::load_fastfiles(realServer, 1, 7, 9) == 42 && received == realServer &&
				scheduled.size() == 1, "disabled Christmas srv load trigger still injected a zone");
			SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", "1");
			night_files::xmas_empty_srv_poc_ready = false;
			expect(menu_art1645::load_fastfiles(realServer, 1, 7, 9) == 42 && received == realServer &&
				scheduled.size() == 1, "unverified Christmas srv could be scheduled without fivezone registry");
			menu_art1645::g_xmas_srv_poc_scheduled.store(false);
			if (priorLoadEnvLength > 0 && priorLoadEnvLength < std::size(priorLoadEnv))
				SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", priorLoadEnv);
			else SetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", nullptr);
			night_files::files = {};

			temporary_directory temp;
			const auto staged = temp.path / bundle.folder;
			std::filesystem::create_directories(staged);
			for (const auto& item : spec)
				std::filesystem::copy_file(installed / item.name, staged / item.name);
			registry hostile;
			expect(hostile.initialize(temp.path, { &bundle, 1 }), "isolated copied PoC fixture failed legitimate hashes");
			const auto srv_path = staged / srv;
			{
				std::fstream mutated(srv_path, std::ios::binary | std::ios::in | std::ios::out);
				char byte{};
				mutated.seekg(160);
				mutated.get(byte);
				mutated.seekp(160);
				mutated.put(static_cast<char>(byte ^ 0x80));
			}
			expect(!hostile.initialize(temp.path, { &bundle, 1 }) && !hostile.enabled(),
				"same-size mutated Christmas srv passed production SHA gate");
			std::filesystem::copy_file(installed / srv, srv_path, std::filesystem::copy_options::overwrite_existing);
			expect(hostile.initialize(temp.path, { &bundle, 1 }), "Christmas PoC failed to recover after restoring original srv");
			std::filesystem::remove(staged / "ww_mp_jup_shipment_xmas.ff");
			expect(!hostile.initialize(temp.path, { &bundle, 1 }) && !hostile.enabled(),
				"missing Christmas worldwide sidecar passed registry admission");
			std::filesystem::copy_file(installed / "ww_mp_jup_shipment_xmas.ff", staged / "ww_mp_jup_shipment_xmas.ff");
			expect(hostile.initialize(temp.path, { &bundle, 1 }), "restored fivezone PoC did not recover");
			std::cout << checks << " checks passed: installed Christmas fivezone native registry/loose provider and hostile failures; not DB_LoadFastfile\n";
			return 0;
		}
#ifdef IW8X_ISLAND_PORT_PROBE
		if (argc == 3 && std::string_view(argv[1]) == "--verify-island-image-overlay")
		{
			// Test the real complete Island bundle without touching the
			// installed game: caller provides a read-only stage with hardlinks
			// to the original assets plus staged 34-image FF and merged XPAK.
			// The opt-in profile must require the two together and schedule the
			// image FF before both ordinary Island dependencies and map.
			using namespace custom_fastfiles;
			registry candidate;
				expect(std::size(night_files::island_image_files) == std::size(night_files::island_probe_files) + 5,
				"Island original image profile changed unrelated bundle sizes");
			expect(std::string_view(night_files::island_image_dependencies[0]) ==
				"magma_island_34_verified_source_images", "original images not loaded before Island dependencies");
			expect(candidate.initialize(argv[2], { &night_files::island_image_bundle, 1 }),
				"staged source-image pair did not satisfy strict Island SHA/header/XPAK registration");
			expect(candidate.find("magma_island_34_verified_source_images.ff") >= 0 &&
				candidate.header(candidate.find("magma_island_34_verified_source_images.ff")) &&
				candidate.find_cache("cache_magma_island164_common") >= 0,
				"source image FF or registered merged cache was not found");
			expect(candidate.cache_matches(candidate.find_cache("cache_magma_island164_common")),
				"source image merged XPAK no longer hash-matched");
				for (const char* prefix : { "ww_", "eng_", "srv_", "techsets_" })
				{
					const auto zone = std::string(prefix) + "magma_island_34_verified_source_images.ff";
					const auto index = candidate.find(zone);
					expect(index >= 0 && candidate.header(index),
						"source image native locale/server companion missing from exact opt-in manifest");
				}
			expect(candidate.before("mp_sm_island_1").size() == 6 &&
				std::string_view(candidate.before("mp_sm_island_1")[0]) == "magma_island_34_verified_source_images",
				"native Island original 34-image FF dependency ordering is wrong");
			expect(!candidate.map_enabled("mp_shipment") &&
				candidate.find("mp_shipment.ff") < 0 && candidate.before("mp_shipment").empty(),
				"image dependency profile changed unrelated-map routing");
			std::cout << checks << " checks passed: exact staged original Island image FF + merged XPAK and pre-map ordering\n";
			return 0;
		}
#endif
		if (argc == 3 && std::string_view(argv[1]) == "--verify-installed")
		{
			// Integration proof using real SHA256-pinned on-disk bundles.
			// Nothing in the installed game/config is changed by this test.
			temporary_directory temp;
			const auto ini = temp.path / "custom_maps.ini";
			WritePrivateProfileStringW(L"ports", L"custom_maps", L"1", ini.c_str());
			expect(night_files::requested(ini) &&
				night_files::Initialize(argv[2], ini), "custom_maps did not validate installed maps");
			expect(night_files::MapEnabled("mp_escape2") &&
				night_files::MapEnabled("mp_sm_island_1"), "custom_maps did not admit both installed maps");
			expect(night_files::Find("mp_escape2.ff") >= 0 &&
				night_files::Find("techsets_mp_escape2.ff") >= 0 &&
				night_files::Find("eng_mp_escape2.ff") >= 0 &&
				night_files::Find("ww_mp_escape2.ff") >= 0 &&
				night_files::Find("mp_sm_island_1.ff") >= 0,
				"manifest-verified map FF routing is incomplete");
			expect(night_files::Header(static_cast<unsigned>(night_files::Find("mp_escape2.ff"))) &&
				night_files::Header(static_cast<unsigned>(night_files::Find("mp_sm_island_1.ff"))),
				"converted map FF headers missing");
			std::cout << checks << " checks passed: custom_maps=1 admits both installed Escape and Island bundles\n";
			return 0;
		}
			// Client MP lobby may preload Island before the private ScriptFile
			// zone is resident. It MUST keep the genuine native 1.20.4
			// operators.csv; only a confirmed Island dedicated BR process may
			// use the authenticated 39-column server-side projection.
			expect(std::string_view(island_operator_table::resolve("operators.csv", false)) ==
				"operators.csv", "Island client MP lobby was redirected to an unloaded private operators table");
			expect(std::string_view(island_operator_table::resolve("operators.csv", true)) ==
				"magma/island/operators.csv", "original dedicated Island GSC operator projection unavailable");
			expect(std::string_view(island_operator_table::resolve("operatorskins.csv", true)) ==
				"operatorskins.csv", "unrelated native operator skin table was redirected");
			expect(std::string_view(island_operator_table::resolve("mp/loot_item_defs.csv", true)) ==
				"mp/loot_item_defs.csv", "Island operator projection interferes with Replay 1.20 loot");
			expect(island_operator_table::resolve(nullptr, false) == nullptr &&
				island_operator_table::resolve(nullptr, true) == nullptr,
				"null StringTable request changed");
			expect(!night_files::DedicatedIslandBR(), "non-dedicated regression test inherited Island dedicated operator rewrite");
		expect(std::string_view(island_loot::table_name("mp/loot/br/default/loot_item_defs.csv")) == "mp/loot_item_defs.csv",
			"Island did not select the native Donetsk loot root");
		for (const auto* name : { "mp/loot_item_defs.csv", "mp/loot_set_global.csv", "mp/itemsourcetable.csv", "mp/ranktable.csv", "" })
		{
			expect(island_loot::table_name(name) == name, "loot selection changed an unrelated native table request");
		}
		expect(island_loot::table_name(nullptr) == nullptr, "loot selection changed a null request");
			expect(std::string_view(island_loot::table_name("mp/loot/br/default/loot_item_defs.csv", true)) ==
				"mp/loot_item_defs.csv", "legacy source-loot override must never select 1.64.5 loot instead of Replay 1.20.4");
			expect(island_loot::table_name(nullptr, true) == nullptr, "legacy source-loot flag changed null request");
		using namespace custom_fastfiles;
		registry files;
		expect(files.initialize({}, {}) && !files.enabled() && files.find("mp_shipment.ff") < 0, "disabled registry intercepts stock");
		if (argc >= 2 && std::string_view(argv[1]) != "--bindings-only")
		{
			auto escape = night_files::escape_bundle;
			escape.folder = ".";
			expect(files.initialize(argv[1], { &escape, 1 }), "real Escape bundle validation failed");
			expect(files.map_enabled("mp_escape2") && !files.map_enabled("mp_shipment"), "real map scope failed");
			expect(std::string_view(files.map_display_name("mp_escape2")) == "Rebirth Island", "real custom display name missing");
			expect(files.find("srv_mp_escape2.ff") >= 0 && files.before("mp_escape2").size() == 2, "real server/helpers missing");
			expect(files.cache_packages("mp_escape2_00001_tr").size() == 2, "real transient cache ownership failed");
			std::cout << "Real Escape bundle: all 1297 files SHA256/header verified\n";
		}
		temporary_directory temporary;
		const char* names[]{ "map_fixture.ff", "helper.ff", "empty.ff", "eng_fixture.sabs", "fixture_cache.xpak", "map_second.ff", "second_cache.xpak" };
		std::array<std::string, 7> hashes;
		std::array<file_spec, 7> specs{};
		for (unsigned i = 0; i < std::size(names); ++i)
		{
			std::vector<char> bytes(std::string_view(names[i]).ends_with(".xpak") ? 800 : 172, char(i + 1));
			if (std::string_view(names[i]).ends_with(".ff")) std::memcpy(bytes.data(), "IWffc100", 8);
			const auto path = temporary.path / names[i];
			std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
			hashes[i] = image_identity::Digest(path.c_str());
			specs[i] = { names[i], bytes.size(), hashes[i].c_str() };
		}
		const char* cache[]{ "fixture_cache" }, *second_cache[]{ "second_cache" }, *dependencies[]{ "helper" };
		file_alias aliases[]{ { "srv_helper.ff", "empty.ff" } };
		bundle bundles[]{
			{ "map_fixture", ".", { specs.data(), 5 }, cache, dependencies, aliases, "Fixture Island" },
			{ "map_second", ".", { specs.data() + 5, 2 }, second_cache, {}, {} },
		};
		expect(night_files::files.initialize(temporary.path, bundles), "valid fixture failed");
		expect(night_files::MapEnabled("map_fixture") && !night_files::MapEnabled("mp_shipment"), "map isolation failed");
		custom_map_names::original.store(&translate);
		expect(std::string_view(custom_map_names::translate("map_fixture")) == "Fixture Island" && !translate_calls,
			"custom map name reached the native missing-asset lookup");
		for (const auto* key : { "mp_shipment", "map_second", "map_fixture_00001_tr", "MENU/YES", "" })
		{
			expect(custom_map_names::translate(key) == key, "stock/unlabelled name translation changed");
		}
		expect(translate_calls == 5 && !night_files::map_display_name("mp_shipment"), "translation fallback lost");
		custom_map_names::original.store(nullptr);
		expect(night_files::Find("srv_helper.ff") == night_files::Find("empty.ff"), "verified companion alias failed");
		expect(night_files::Find("english/eng_fixture.sabs") >= 0 && night_files::Find("english\\eng_fixture.sabs") >= 0, "localized bank failed");
		for (const auto* invalid : { "../map_fixture.ff", "english/map_fixture.ff", "C:map_fixture.ff", "nested/map_fixture.ff", "mp_shipment.ff" })
		{
			expect(night_files::Find(invalid) < 0, "unknown/path name intercepted");
		}
		expect(night_files::CachePackages("helper").size() == 1 && std::string_view(night_files::CachePackages("helper")[0]) == "fixture_cache", "owner cache isolation failed");
		expect(night_files::CachePackages("mp_shipment").empty() && night_files::Before("helper").empty(), "stock/helper dependencies expanded");
		expect(night_files::Header(night_files::Find("map_fixture.ff")) && !night_files::Header(night_files::Find("eng_fixture.sabs")), "file header typing failed");

		fastfile::g_open.store(&open);
		fastfile::g_exists.store(&exists);
		fastfile::Open(nullptr, 5, "mp_shipment.ff", 0x21);
		expect(opened == "mp_shipment.ff" && opened_flags == 0x21, "stock open modified");
		fastfile::Open(nullptr, 5, "map_fixture.ff", 0x21);
		expect(opened == std::string(fastfile::kAlias) + "map_fixture.ff" && opened_flags == 0x21, "custom native descriptor open failed");
		const auto calls = open_calls;
		expect(!fastfile::Open(nullptr, 5, "map_fixture.fp", 0) && !fastfile::Exists(nullptr, 5, "map_fixture.fp"), "offline-applied delta scheduled");
		expect(open_calls == calls && fastfile::Exists(nullptr, 5, "srv_helper.ff"), "alias/delta hit stock provider");
		fastfile::Open(nullptr, 5, "mp_shipment.fp", 0);
		expect(opened == "mp_shipment.fp", "stock delta suppressed");
		fastfile::g_loose.store(&loose);
		fastfile::g_openPath.store(&open_path);
		expect(fastfile::Loose(5, "mp_shipment.ff", 0x19) == INVALID_HANDLE_VALUE && loose_calls == 1 &&
			opened == "mp_shipment.ff" && opened_flags == 0x19, "stock loose-file request changed");
		expect(read_header(fastfile::Loose(5, "map_fixture.ff", 1)) && loose_calls == 1 && opened_flags == 1,
			"disk provider did not open verified original filename");
		const auto async_handle = fastfile::Loose(5, "map_fixture.ff", 0x19);
		expect(async_handle != INVALID_HANDLE_VALUE && opened_flags == 0x19, "disk provider lost asynchronous/unbuffered flags");
		CloseHandle(async_handle);
		expect(read_header(fastfile::Loose(5, "__magma120_converted_map_fixture.ff", 0x19)) && loose_calls == 1,
			"CASC alias did not open verified fastfile");
		expect(opened_flags == 1, "CASC loose worker received an asynchronous handle");
		expect(read_header(fastfile::Loose(5, "srv_helper.ff", 0)) &&
			std::filesystem::path(opened).lexically_normal() == temporary.path / "empty.ff", "disk companion alias did not resolve");
		const auto cache_handle = fastfile::Loose(5, "fixture_cache.xpak", 0x18);
		const bool cache_opened = cache_handle != INVALID_HANDLE_VALUE;
		if (cache_opened) CloseHandle(cache_handle);
		expect(cache_opened && opened_flags == 0x18 && loose_calls == 1, "disk provider lost XPAK read mode");
		expect(fastfile::Loose(5, "map_fixture.fp", 0) == INVALID_HANDLE_VALUE && loose_calls == 1,
			"disk provider requested offline-applied delta");
		expect(fastfile::Loose(5, "__magma120_converted_unknown.ff", 0) == INVALID_HANDLE_VALUE && loose_calls == 1,
			"unknown private alias reached stock provider");
		fastfile::Loose(5, "mp_shipment.fp", 0);
		expect(loose_calls == 2 && opened == "mp_shipment.fp", "stock disk delta suppressed");

		std::array<uint8_t, 0x110> descriptor{};
		std::memcpy(descriptor.data(), "map_fixture.ff", 15);
		std::memcpy(descriptor.data() + 0x88, night_files::Header(night_files::Find("map_fixture.ff"))->data(), 136);
		expect(fastfile::ConvertedDescriptor(descriptor.data()), "verified descriptor rejected");
		descriptor[0x89] ^= 1;
		expect(!fastfile::ConvertedDescriptor(descriptor.data()), "changed descriptor accepted");

		menu_art1645::g_original.store(&schedule);
		const menu_art1645::ZoneInfo stock[]{ { "mp_shipment", 0x88, 2, 5, 17 } };
		expect(menu_art1645::load_fastfiles(stock, 1, 7, 9) == 42 && received == stock, "stock scheduling request copied/changed");
		const menu_art1645::ZoneInfo custom[]{ { "map_fixture", 0x88, 2, 5, 17 }, { "map_second", 0x10, 3, 6, 19 } };
		menu_art1645::load_fastfiles(custom, 2, 7, 9);
		expect(scheduled.size() == 3 && std::string_view(scheduled[0].name) == "helper" && scheduled[1].name == custom[0].name, "dependency ordering failed");
		expect(scheduled[0].flags == 0x88 && scheduled[0].failureMode == 2 && scheduled[0].priority == 5 && scheduled[0].reserved == 17, "owner descriptor ABI changed");
		const menu_art1645::ZoneInfo duplicate[]{ custom[0], { "helper", 1, 2, 3, 4 }, custom[0] };
		menu_art1645::load_fastfiles(duplicate, 3, 7, 9);
		expect(received == duplicate && scheduled.size() == 3, "explicit dependency duplicated");
		menu_art1645::load_fastfiles(custom, 2, 7, 9);
		expect(scheduled.size() == 3, "same-map restart retained stale dependencies");

		fastfile::g_packages.store(&load_packages);
		fastfile::g_findPack.store(&find_package);
		fastfile::g_addPack.store(&add_package);
		expect(!fastfile::LoadPackages("mp_shipment") && packages.empty() && package_calls == 1, "stock package result overridden");
		expect(fastfile::LoadPackages("map_fixture") && packages == std::vector<std::string>{ "fixture_cache" }, "unrelated bundle caches registered");
		expect(fastfile::LoadPackages("map_fixture") && packages.size() == 1, "cache registered twice");
		expect(fastfile::LoadPackages("map_second") && packages.size() == 2, "second bundle cache omitted");
		fastfile::g_verifySignature.store(&signature);
		fastfile::g_verifyPack.store(&verify_pack);
		const auto header = night_files::CacheHeader(night_files::FindCache("fixture_cache"));
		expect(fastfile::VerifyPack("fixture_cache", header.data()), "verified custom cache rejected");
		expect(!fastfile::VerifySignature(header.data() + 24, header.data() + 56) && signature_calls == 1, "signature override escaped native verification scope");
		auto changed_header = header;
		changed_header[20] ^= 1;
		expect(!fastfile::VerifyPack("fixture_cache", changed_header.data()), "changed cache header accepted");
		std::ofstream(temporary.path / names[4], std::ios::binary | std::ios::app).put('x');
		expect(!fastfile::VerifyPack("fixture_cache", header.data()), "changed cache accepted after admission");
		expect(!files.initialize(temporary.path, bundles) && !files.enabled() && files.find(names[0]) < 0, "partial failed registry published");
		std::filesystem::resize_file(temporary.path / names[4], specs[4].size);
		aliases[0].name = "helper.ff";
		expect(!files.initialize(temporary.path, bundles), "alias overwrote real fastfile");
		aliases[0].name = "srv_helper.ff";
		bundles[1].folder = "../outside";
		expect(!files.initialize(temporary.path, bundles), "bundle escaped root");
		bundles[1].folder = ".";
		expect(files.initialize(temporary.path, bundles), "registry did not recover after failed admission");
		std::fstream corrupted(temporary.path / names[4], std::ios::binary | std::ios::in | std::ios::out);
		corrupted.put('x'); corrupted.close();
		expect(!files.initialize(temporary.path, bundles), "same-size changed digest accepted");

		const auto size = game::addresses::runtime::db_memory_initialized + 0x1000;
		auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		expect(image != nullptr, "pool test image allocation failed");
		const auto base = reinterpret_cast<uintptr_t>(image);
		auto* physics = reinterpret_cast<uint32_t*>(image + asset_pools::physics_pool_slot);
		auto* detail = reinterpret_cast<uint32_t*>(image + asset_pools::detail_pool_slot);
		auto* vfx = reinterpret_cast<uint32_t*>(image + asset_pools::vfx_pool_slot);
		auto* script = reinterpret_cast<uint64_t*>(image + asset_pools::script_reserve_slot);
		*script = asset_pools::stock_script_reserve;
		*physics = 1; *detail = 16312; *vfx = 3072;
		expect(!asset_pools::prepare(base, true) && *physics == 1 && *detail == 16312, "partial pool resize on mismatch");
		asset_pools::prepared.store(0);
		*physics = 15000;
		expect(asset_pools::prepare(base, false) && *vfx == 3072 && *script == asset_pools::stock_script_reserve &&
			!asset_pools::port_ready(), "stock capacities changed without custom content");
		asset_pools::prepared.store(0);
		*physics = 15000; *detail = 16312; *vfx = 1;
		expect(!asset_pools::prepare(base, true) && *physics == 15000 && *detail == 16312 && *vfx == 1, "VFX mismatch caused a partial pool resize");
		asset_pools::prepared.store(0);
		*vfx = 3072;
		*script = 0x100000001ull;
		expect(!asset_pools::prepare(base, true) && *script == 0x100000001ull && *vfx == 3072 && *physics == 15000,
			"64-bit script reserve mismatch caused a partial resize");
		asset_pools::prepared.store(0);
		*script = asset_pools::stock_script_reserve;
		auto* initialized = image + game::addresses::runtime::db_memory_initialized;
		*initialized = 1;
		expect(!asset_pools::prepare(base, true) && *script == asset_pools::stock_script_reserve && *physics == 15000,
			"script reserve changed after database initialization");
		asset_pools::prepared.store(0);
		*initialized = 0;
		expect(asset_pools::prepare(base, true) && *physics == 96000 && *detail == 96000 && *vfx == 8192 &&
			*script == asset_pools::port_script_reserve && asset_pools::port_ready(), "client/full pool preparation failed");
		expect(asset_pools::prepare(base, true), "pool preparation not idempotent");
		VirtualFree(image, 0, MEM_RELEASE);
		if (argc == 3)
		{
			const auto replay_path = std::filesystem::path(argv[2]);
			const auto replay = LoadLibraryExW(replay_path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
			expect(replay != nullptr, "could not map Replay for native guard validation");
			const auto replay_base = reinterpret_cast<uintptr_t>(replay);
			expect(custom_map_names::install(replay_base) == hook::Status::Installed && custom_map_names::original.load(),
				"Replay native localization hook guard failed");
			auto* frontend_test = reinterpret_cast<uint8_t*>(replay_base + game::kIsFrontEndRVA);
			DWORD frontend_protection{};
			expect(VirtualProtect(frontend_test, sizeof(game::kIsFrontEndPrologue), PAGE_EXECUTE_READWRITE, &frontend_protection),
				"could not exercise animation frontend binding guard");
			const auto frontend_byte = *frontend_test;
			*frontend_test ^= 1;
			expect(night_animtrees::Install(replay_base) == hook::Status::Failed && !night_animtrees::original.load() &&
				!night_animtrees::original_asset_info.load(),
				"animation hook accepted an unsupported frontend binding");
			*frontend_test = frontend_byte;
			DWORD ignored{};
			expect(VirtualProtect(frontend_test, sizeof(game::kIsFrontEndPrologue), frontend_protection, &ignored),
				"could not restore mapped frontend binding protection");
			const auto reject_animation_binding = [&](uintptr_t rva, std::size_t length, hook::Status expected_status, const char* message)
			{
				auto* site = reinterpret_cast<uint8_t*>(replay_base + rva);
				DWORD protection{};
				expect(VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &protection), "could not alter optional-animation binding");
				const auto byte = *site;
				*site ^= 1;
				expect(night_animtrees::Install(replay_base) == expected_status && !night_animtrees::original.load() &&
					!night_animtrees::original_asset_info.load() && !night_animtrees::installed.load(), message);
				*site = byte;
				DWORD previous{};
				expect(VirtualProtect(site, length, protection, &previous), "could not restore optional-animation binding protection");
			};
			reject_animation_binding(night_animtrees::optional_leaf_call_rva, night_animtrees::optional_leaf_call_bytes.size(),
				hook::Status::Failed, "animation hook accepted an unsupported optional-leaf caller");
			reject_animation_binding(night_animtrees::asset_info_rva, night_animtrees::asset_info_bytes.size(),
				hook::Status::NotReady, "asset-info guard failure left a partial animation hook batch");
			night_animtrees::original.store(&read_tree);
			night_animtrees::original_asset_info.store(&read_asset_info);
			expect(night_animtrees::Install(replay_base) == hook::Status::Failed && !night_animtrees::installed.load(),
				"retained animation trampolines were mistaken for an installed batch");
			night_animtrees::original.store(nullptr);
			night_animtrees::original_asset_info.store(nullptr);
			expect(night_animtrees::Install(replay_base) == hook::Status::Installed && night_animtrees::original.load() &&
				night_animtrees::original_asset_info.load() && night_animtrees::installed.load(),
				"Replay animation reader/frontend/collision/optional-leaf guards failed");
			expect(night_animtrees::Install(replay_base) == hook::Status::Installed, "animation hook installation not idempotent");
			expect(night_animtrees::Install(replay_base + 0x1000) == hook::Status::Failed, "animation hook accepted a different image after installation");
			asset_pools::prepared.store(0);
			asset_pools::ports_ready.store(false);
			expect(asset_pools::prepare(replay_base, true), "Replay pool table guard failed");
			expect(night_entry_pool::Prepare(replay_base), "Replay entry-table guards/relocations failed");
			expect(night_entry_pool::Prepare(replay_base), "entry-table preparation not idempotent");
			const auto sentinel = reinterpret_cast<uintptr_t>(night_entry_pool::g_storage) + 20ull * night_entry_pool::kEntryCount;
			// Verify the constructor boundary without executing game code.
			expect(!night_entry_pool::Report(), "entry table reported ready before construction");
			*reinterpret_cast<uintptr_t*>(sentinel) = sentinel;
			expect(night_entry_pool::Report(), "relocated entry-table plan failed validation");
			expect(bap::InstallNightCollision(replay_base) == hook::Status::Installed &&
				bap::detail::g_collisionReady.load(), "ordinary client collision installation returned without installing");
			expect(bap::ShouldUseFullCollision("map_fixture") && !bap::ShouldUseFullCollision("mp_shipment"), "collision map scope leaked");
			std::cout << "Mapped Replay: pools, entry relocation, collision and animation reader/frontend/optional-leaf guards verified\n";
			MH_Uninitialize();
			night_animtrees::installed.store(false);
			night_animtrees::original.store(nullptr);
			night_animtrees::original_asset_info.store(nullptr);
			FreeLibrary(replay);
		}
		bap::detail::g_collisionReady.store(true);
		bap::detail::g_nightCollisionEnabled.store(true);
		bap::detail::CollisionState collision{};
		collision.mode = 2;
		collision.mapName = "mp_shipment";
		bap::detail::g_collision = &collision;
		bap::detail::g_collisionProcess.store(&collision_native);
		bap::detail::g_collisionLoad = &collision_load;
		bap::detail::g_collisionUpdate = &collision_update;
		bap::detail::g_collisionSetupWorld = &collision_world;
		bap::detail::g_collisionSetupModels = &collision_models;
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_forwarded == 1 && !collision_loaded, "stock collision bypassed native processor");
		collision.mapName = "map_fixture";
		collision.numTiles = 2;
		collision.fileIndices[0] = 101; collision.fileIndices[1] = 102;
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_loaded == 2 && collision_setup == 2 && collision.setupAll, "numbered collision loading/setup failed");
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_loaded == 2 && collision_setup == 2, "collision loaded/setup twice");
		collision.mode = 0;
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_forwarded == 2, "native collision unload suppressed");
		collision = {};
		collision.mapName = "map_fixture"; collision.mode = 2; collision.numTiles = 2;
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_loaded == 2, "unregistered collision tiles loaded");
		collision.fileIndices[0] = 101; collision.fileIndices[1] = 102;
		bap::detail::ProcessCollisionFastfiles(true);
		expect(collision_loaded == 4 && collision.setupAll, "collision restart retained stale requests");

		temporary_directory tree_directory;
		const char* tree_maps[]{ "mp_escape2", "mp_kstenod" };
		const char* tree_files[]{ "mp_escape2.ff", "mp_kstenod.ff" };
		std::array<std::string, 2> tree_hashes;
		std::array<file_spec, 2> tree_specs{};
		std::array<bundle, 2> tree_bundles{};
		for (unsigned i = 0; i < tree_bundles.size(); ++i)
		{
			std::filesystem::copy_file(temporary.path / names[0], tree_directory.path / tree_files[i]);
			tree_hashes[i] = image_identity::Digest((tree_directory.path / tree_files[i]).c_str());
			tree_specs[i] = { tree_files[i], specs[0].size, tree_hashes[i].c_str() };
			tree_bundles[i] = { tree_maps[i], ".", { &tree_specs[i], 1 }, {}, {}, {} };
			tree_bundles[i].animation_trees = i ? std::span<const asset_alias>(night_files::night_trees)
				: std::span<const asset_alias>(night_files::escape_trees);
		}
		expect(night_files::files.initialize(tree_directory.path, tree_bundles), "animation map fixture failed");
		constexpr uintptr_t frontend_state_rva = 0xC4EBEB0;
		auto* tree_image = static_cast<uint8_t*>(VirtualAlloc(nullptr, frontend_state_rva + 0x1000, MEM_RESERVE, PAGE_NOACCESS));
		expect(tree_image != nullptr, "animation scope image allocation failed");
		for (const auto rva : { frontend_state_rva, night_animtrees::CollisionMapRva })
		{
			expect(VirtualAlloc(tree_image + (rva & ~uintptr_t(0xFFF)), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
				"animation scope page allocation failed");
		}
		night_animtrees::imageBase = reinterpret_cast<uintptr_t>(tree_image);
		night_animtrees::original.store(&read_tree);
		auto& frontend_state = *reinterpret_cast<int*>(tree_image + frontend_state_rva);
		auto& collision_map = *reinterpret_cast<const char**>(tree_image + night_animtrees::CollisionMapRva);
		auto* context = reinterpret_cast<void*>(0x5678);
		const char* trees[]{ "animated_props", "generic_human", "script_model", "scriptables" };
		for (unsigned i = 0; i < std::size(tree_maps); ++i)
		{
			collision_map = tree_maps[i];
			for (const auto state : { 5, 8, 9 })
			{
				frontend_state = state;
				for (const auto* tree : trees)
				{
					const auto name = "animtrees/" + std::string(tree) + ".atr";
					expect(std::string_view(night_animtrees::read(context, name.c_str())) == "native tree buffer" &&
						tree_context == context && tree_name == name.c_str(), "preloaded map replaced a stock frontend tree");
				}
			}
			for (const auto state : { 0, 1, 4 })
			{
				frontend_state = state;
				for (const auto* tree : trees)
				{
					const auto name = "animtrees/" + std::string(tree) + ".atr";
					const auto expected = std::string(i ? "animtrees/magma_night128/" : "animtrees/magma_escape130/") + tree + ".atr";
					expect(std::string_view(night_animtrees::read(context, name.c_str())) == "native tree buffer" &&
						tree_context == context && tree_name == expected, "gameplay private tree selection changed");
				}
			}
		}
		std::filesystem::copy_file(temporary.path / names[0], tree_directory.path / "map_fixture.ff");
		file_spec fixture_tree_file{ "map_fixture.ff", specs[0].size, hashes[0].c_str() };
		asset_alias fixture_trees[]{ { "animtrees/script_model.atr", "animtrees/fixture/script_model.atr" } };
		bundle fixture_tree{ "map_fixture", ".", { &fixture_tree_file, 1 }, {}, {}, {}, nullptr, fixture_trees };
		std::array tree_profiles{ tree_bundles[0], tree_bundles[1], fixture_tree };
		expect(night_files::files.initialize(tree_directory.path, tree_profiles), "generic tree profile failed admission");
		collision_map = "map_fixture";
		frontend_state = 0;
		night_animtrees::read(context, "animtrees/script_model.atr");
		expect(std::string_view(tree_name) == "animtrees/fixture/script_model.atr", "generic map tree alias requires hardcoded map logic");
		frontend_state = 5;
		night_animtrees::read(context, "animtrees/script_model.atr");
		expect(std::string_view(tree_name) == "animtrees/script_model.atr", "generic tree profile changed frontend source");
		frontend_state = 0;
		night_animtrees::read(context, "animtrees/other.atr");
		expect(std::string_view(tree_name) == "animtrees/other.atr", "generic tree profile changed unrelated source");
		fixture_trees[0].target = "animtrees/../script_model.atr";
		expect(!night_files::files.initialize(tree_directory.path, tree_profiles), "tree alias accepted a parent traversal");
		fixture_trees[0].target = "animtrees/fixture/script_model.atr";
		const asset_alias duplicate_trees[]{ fixture_trees[0], fixture_trees[0] };
		tree_profiles[2].animation_trees = duplicate_trees;
		expect(!night_files::files.initialize(tree_directory.path, tree_profiles), "duplicate tree sources accepted");
		expect(night_files::files.initialize(tree_directory.path, tree_bundles), "tree profiles did not recover after invalid aliases");
		for (const auto* map : { "mp_shipment", "mp_escape3", "mp_frontend3", "" })
		{
			collision_map = map;
			night_animtrees::read(context, "animtrees/script_model.atr");
			expect(std::string_view(tree_name) == "animtrees/script_model.atr", "unregistered map received a private tree");
		}
		collision_map = "mp_escape2";
		night_animtrees::read(context, "animtrees/other.atr");
		expect(std::string_view(tree_name) == "animtrees/other.atr", "unrelated animation source changed");
		night_animtrees::read(context, nullptr);
		expect(!tree_name && tree_context == context, "null animation request changed");

		night_animtrees::original_asset_info.store(&read_asset_info);
		const auto optional_caller = night_animtrees::imageBase + 0x13151FF;
		const char* optional_name = "hm_grnd_org_walk_cqb_reload";
		const night_animtrees::asset_info default_transient{ reinterpret_cast<void*>(0x1234), 0, true, true, { 0x12, 0x34, 0x56, 0x78 } };
		const auto verify_optional_lookup = [&](unsigned int type, uintptr_t caller, const night_animtrees::asset_info& native_info,
			bool found, bool prune, const char* message)
		{
			asset_lookup_value = native_info;
			asset_lookup_found = found;
			night_animtrees::asset_info output{ reinterpret_cast<void*>(0x5678), 91, false, true, { 9, 8, 7, 6 } };
			auto expected = found ? native_info : output;
			if (prune) expected.is_transient = false;
			const auto previous_calls = asset_lookup_calls;
			const bool result = night_animtrees::get_asset_info_impl(type, optional_name, &output, caller);
			expect(result == found && asset_lookup_calls == previous_calls + 1 && asset_lookup_type == type &&
				asset_lookup_name == optional_name && asset_lookup_output == &output &&
				std::memcmp(&output, &expected, sizeof(output)) == 0, message);
		};
		frontend_state = 5;
		verify_optional_lookup(7, optional_caller, default_transient, true, false,
			"optional animation lookup changed without an admitted Island bundle");
		expect(night_files::files.initialize({}, {}), "optional animation fixture could not disable custom maps");
		verify_optional_lookup(7, optional_caller, default_transient, true, false,
			"disabled custom maps changed an optional animation lookup");
		std::filesystem::copy_file(temporary.path / names[0], tree_directory.path / "mp_sm_island_1.ff");
		const auto optional_map_hash = image_identity::Digest((tree_directory.path / "mp_sm_island_1.ff").c_str());
		const file_spec optional_map_file{ "mp_sm_island_1.ff", specs[0].size, optional_map_hash.c_str() };
		const bundle optional_map_bundle{ "mp_sm_island_1", ".", { &optional_map_file, 1 }, {}, {}, {} };
		expect(night_files::files.initialize(tree_directory.path, { &optional_map_bundle, 1 }), "optional Island animation fixture failed admission");
		for (const auto state : { 5, 8 })
		{
			frontend_state = state;
			for (const char* name : { "hm_grnd_org_walk_cqb_reload", "hm_grnd_org_run_reload01_ar", "hm_grnd_org_run_reload02_ar" })
			{
				optional_name = name;
				verify_optional_lookup(7, optional_caller, default_transient, true, true,
					"unavailable optional frontend animation was retained or native output fields changed");
			}
		}
		for (const auto state : { 0, 4 })
		{
			frontend_state = state;
			verify_optional_lookup(7, optional_caller, default_transient, true, false, "gameplay optional animation lookup changed");
		}
		frontend_state = 5;
		verify_optional_lookup(7, optional_caller + 1, default_transient, true, false, "unrelated asset-info caller changed");
		verify_optional_lookup(6, optional_caller, default_transient, true, false, "non-XAnim asset-info lookup changed");
		auto resident_transient = default_transient;
		resident_transient.zone_index = 27;
		resident_transient.is_default = false;
		verify_optional_lookup(7, optional_caller, resident_transient, true, false, "resident transient animation was pruned");
		auto ordinary_default = default_transient;
		ordinary_default.is_transient = false;
		verify_optional_lookup(7, optional_caller, ordinary_default, true, false, "ordinary default animation lookup changed");
		verify_optional_lookup(7, optional_caller, default_transient, false, false, "failed native lookup changed its untouched output");
		asset_lookup_found = true;
		asset_lookup_value = default_transient;
		night_animtrees::asset_info wrapper_output{};
		expect(night_animtrees::get_asset_info(7, optional_name, &wrapper_output) &&
			std::memcmp(&wrapper_output, &default_transient, sizeof(wrapper_output)) == 0,
			"asset-info entry wrapper treated a test caller as the optional native caller");
		const auto null_calls = asset_lookup_calls;
		expect(night_animtrees::get_asset_info_impl(7, optional_name, nullptr, optional_caller) &&
			asset_lookup_calls == null_calls + 1 && !asset_lookup_output, "null native output was changed or dereferenced");
		DWORD optional_state_protection{};
		expect(VirtualProtect(tree_image + (frontend_state_rva & ~uintptr_t(0xFFF)), 0x1000, PAGE_NOACCESS, &optional_state_protection),
			"could not make optional animation frontend state unreadable");
		verify_optional_lookup(7, optional_caller, default_transient, true, false, "unreadable frontend state pruned an optional animation");
		DWORD optional_ignored{};
		expect(VirtualProtect(tree_image + (frontend_state_rva & ~uintptr_t(0xFFF)), 0x1000, optional_state_protection, &optional_ignored),
			"could not restore optional animation frontend state");
		night_animtrees::original_asset_info.store(nullptr);
		expect(night_files::files.initialize(tree_directory.path, tree_bundles), "animation tree profiles did not recover after optional lookup tests");
		frontend_state = 0;
		collision_map = nullptr;
		night_animtrees::read(context, "animtrees/script_model.atr");
		expect(std::string_view(tree_name) == "animtrees/script_model.atr", "collision unload retained tree override");
		collision_map = "mp_escape2";
		DWORD old_protect{};
		expect(VirtualProtect(tree_image + (frontend_state_rva & ~uintptr_t(0xFFF)), 0x1000, PAGE_NOACCESS, &old_protect),
			"could not make frontend state unreadable");
		night_animtrees::read(context, "animtrees/script_model.atr");
		expect(std::string_view(tree_name) == "animtrees/script_model.atr", "unreadable frontend state activated tree override");
		VirtualFree(tree_image, 0, MEM_RELEASE);
		night_animtrees::imageBase = 0;
		night_animtrees::original.store(nullptr);
		expect(night_files::files.initialize({}, {}) && !night_files::Enabled(), "registry reset failed");
		custom_map_names::original.store(&translate);
		const auto translation_count = translate_calls;
		expect(std::string_view(custom_map_names::translate("map_fixture")) == "map_fixture" &&
			translate_calls == translation_count + 1, "disabled registry retained a custom display name");
		const auto config = temporary.path / "ports.ini";
		expect(night_files::Initialize(temporary.path, config) && !night_files::Enabled() && !night_files::requested(config), "absent INI activated ports");
			// Legacy per-map toggles are ignored. A single custom_maps opt-in
			// discovers available map bundles from their primary .ff files.
			WritePrivateProfileStringW(L"ports", L"escape130", L"1", config.c_str());
			WritePrivateProfileStringW(L"ports", L"island64_serialization_probe", L"1", config.c_str());
			expect(!night_files::requested(config) && night_files::Initialize(temporary.path, config) &&
				!night_files::Enabled(), "legacy per-map toggle still activates custom files");
			WritePrivateProfileStringW(L"ports", L"custom_maps", L"1", config.c_str());
			expect(night_files::requested(config) && night_files::Initialize(temporary.path, config) &&
				!night_files::Enabled(), "custom_maps should permit a root with no converted maps");
			// A present but corrupted primary FF must fail closed, not silently
			// disappear from the custom-map browser.
			const auto escape_dir = temporary.path / "magma/maps/mp_escape2";
			std::filesystem::create_directories(escape_dir);
			std::ofstream(escape_dir / "mp_escape2.ff", std::ios::binary) << "IWffc100-incomplete";
			expect(!night_files::Initialize(temporary.path, config) && !night_files::Enabled(),
				"present but incomplete converted bundle was silently accepted");
			std::filesystem::remove_all(escape_dir);
			WritePrivateProfileStringW(L"ports", L"custom_maps", L"0", config.c_str());
			expect(night_files::Initialize(temporary.path, config) && !night_files::Enabled(),
				"disabled custom_maps still activates bundles");
		const auto download = temporary.path / "download.ini";
		WritePrivateProfileStringW(L"online_services", L"host", L"backend.fixture", download.c_str());
			WritePrivateProfileStringW(L"ports", L"custom_maps", L"1", download.c_str());
		expect(night_files::preserve_settings(config, download) && !night_files::requested(download), "remote INI changed disabled local port choice");
		wchar_t host[64]{};
		GetPrivateProfileStringW(L"online_services", L"host", L"", host, 64, download.c_str());
		expect(std::wstring_view(host) == L"backend.fixture", "port preservation changed connection settings");
			WritePrivateProfileStringW(L"ports", L"custom_maps", L"1", config.c_str());
		expect(night_files::preserve_settings(config, download) && night_files::requested(download), "remote INI erased enabled local port choice");
		expect(night_files::preserve_settings(temporary.path / "absent.ini", download) && !night_files::requested(download), "remote INI activated a port without attach-time pool preparation");
	#ifdef IW8X_ISLAND_PORT_PROBE
				// Shared BR profile replaces a stock common zone only for a
				// verified Island dedicated launch; it does not prepend another
				// profile, and missing BR companions must not use empty aliases.
			const auto island_fixture_path = temporary.path / "mp_sm_island_1.ff";
			std::vector<char> island_fixture(172, 0);
			std::memcpy(island_fixture.data(), "IWffc100", 8);
			std::ofstream(island_fixture_path, std::ios::binary).write(island_fixture.data(), island_fixture.size());
			const auto island_fixture_sha = image_identity::Digest(island_fixture_path.c_str());
			const custom_fastfiles::file_spec island_spec[]{
					{ "mp_sm_island_1.ff", island_fixture.size(), island_fixture_sha.c_str() }
			};
			const custom_fastfiles::bundle island_fixture_bundle{ "mp_sm_island_1", ".", island_spec, {}, {}, {}, "Island" };
			expect(night_files::files.initialize(temporary.path, { &island_fixture_bundle, 1 }),
				"isolated Island registry fixture failed");
				night_files::island_native_br_ready = false;
				for (const auto* name : { "common_core_mp", "techsets_common_core_mp",
					"ww_common_core_mp", "eng_common_core_mp" })
					expect(night_files::NativeSharedZone(name) == nullptr,
						"unverified Island BR profile substituted a stock companion");
				expect(night_files::island_br_dedicated_command_line(
					L"\"C:\\Games\\game_dx12_ship_replay.exe\" --dedi --playlist 104 --map mp_sm_island_1.ff --mode br") &&
					night_files::island_br_dedicated_command_line(
					L"server.exe --dedi --map mp_sm_island_1 --mode br --min-players 1"),
					"exact Island dedicated BR command-line selection failed");
				for (const auto invalid : {
					L"server.exe --map mp_sm_island_1.ff --mode br",
					L"server.exe --dedi --map mp_donetsk.ff --mode br",
					L"server.exe --dedi --map mp_sm_island_1.ff --mode war",
					L"server.exe --dedi --map mp_sm_island_1.ff --mode br2",
					L"server.exe --dedi --map mp_sm_island_1.ffextra --mode br" })
				{
					expect(!night_files::island_br_dedicated_command_line(invalid),
						"ineligible command-line selected Island BR shared assets");
				}
			expect(night_files::Before("mp_sm_island_1").empty(),
				"unverified native BR asset zone was inserted");
				expect(night_files::NativeSharedZone("common_core_mp") == nullptr,
					"Island shared BR mode changed stock common load without validation");
			expect(night_files::Find("srv_common_br_mp.ff") < 0,
				"unverified BR companion incorrectly mapped to Island empty envelope");
				expect(night_files::Before("mp_sm_island_1").empty() &&
					night_files::Find("srv_common_br_mp.ff") < 0 &&
					night_files::Find("ww_common_br_mp.ff") < 0 &&
					night_files::Find("eng_common_br_mp.ff") < 0,
					"missing BR companion or extra shared-profile dependency was disguised");
			expect(night_files::Before("mp_donetsk").empty() && night_files::Before("mp_shipment").empty(),
				"Island-only native BR dependency escaped to stock maps");
				// Diagnostic-only native 1.20 BR-loot A/B control must schedule
				// every original Island map file unchanged and use only the already
				// hash-verified merged cache; never silently load the 34-image FF.
				expect(std::size(night_files::island_replay120_merged_cache_control_files) ==
					std::size(night_files::island_probe_files),
					"Replay120 baseline comparison changed Island asset inventory count");
				unsigned cacheChanges{};
				for (std::size_t i = 0; i < std::size(night_files::island_probe_files); ++i)
				{
					const auto& original = night_files::island_probe_files[i];
					const auto& baseline = night_files::island_replay120_merged_cache_control_files[i];
					expect(std::string_view(original.name) == baseline.name,
						"Replay120 baseline added or renamed an Island zone asset");
					if (std::string_view(baseline.name) == "cache_magma_island164_common.xpak")
					{
						++cacheChanges;
						expect(baseline.size == night_files::island_image_cache_bytes &&
							std::string_view(baseline.sha) == night_files::island_image_cache_sha,
							"Replay120 A/B baseline failed exact native merged XPAK pin");
					}
					else expect(original.size == baseline.size &&
						std::string_view(original.sha) == baseline.sha,
						"Replay120 A/B baseline modified other Island zone assets");
				}
				expect(cacheChanges == 1, "Replay120 baseline must modify exactly one package hash pin");
				// Correct production approach: substitute BR for core in the
				// original shared zone request instead of loading both and
				// exceeding the native 160 NetConstStrings asset limit.
				night_files::island_native_br_ready = true;
				// Dedicated startup may use the authenticated BR shared profile
				// immediately. A client needs its exact native Island parent lookup
				// before changing common zone families.
				night_files::island_dedicated_br_launch = true;
				night_files::island_client_br_parent_selected.store(false);
				expect(night_files::island_shared_profile_permitted(true, true, true, false) &&
					!night_files::island_shared_profile_permitted(true, true, false, false) &&
					!night_files::island_shared_profile_permitted(false, true, true, true) &&
					!night_files::island_shared_profile_permitted(true, false, true, true),
					"Island common_br_mp selection escaped verification, map, or request guard");
				// Native 0x10B2D00 and 0x10B52F0 consult
				// mp/levelZoneParents.csv before any zone descriptors are sent
				// to DB_LoadFastfiles; prove no broad StringTable spoofing.
				const int actualParentTable = 1, unrelatedTable = 2;
				expect(night_files::override_parent_lookup(&actualParentTable, &actualParentTable, 0,
					"mp_sm_island_1", 1, true), "Island BR parent missing from native StringTable lookup");
				// Frontend asset reloads can legitimately return a different
				// instance of the *same genuine* levelZoneParents StringTable.
				// Do not relax this to arbitrary 2-column string tables.
				expect(night_files::island_parent_table_identity(&actualParentTable,
						&actualParentTable, "", 0, 0),
						"cached Replay native parent table lost exact pointer identity");
				expect(night_files::island_parent_table_identity(&unrelatedTable,
						&actualParentTable, "mp/levelzoneparents.csv", 2, 12),
						"identical Replay native parent asset from client reload was rejected");
				for (const auto badName : {"mp/levelZoneParents.csv", "mp/levelzoneparents.csv.extra",
						"mp/namestable.csv", "operators.csv"})
					expect(!night_files::island_parent_table_identity(&unrelatedTable,
							&actualParentTable, badName, 2, 12),
							"Island BR family routing admitted an unrelated table name");
				expect(!night_files::island_parent_table_identity(&unrelatedTable,
						&actualParentTable, "mp/levelzoneparents.csv", 3, 12) &&
						!night_files::island_parent_table_identity(&unrelatedTable,
						&actualParentTable, "mp/levelzoneparents.csv", 2, 11) &&
						!night_files::island_parent_table_identity(nullptr,
						&actualParentTable, "mp/levelzoneparents.csv", 2, 12),
						"Island BR family routing admitted invalid native StringTable shape");
				for (const auto invalid : {"mp_donetsk", "mp_escape2", "mp_sm_island_1.ff",
					"mp_sm_island_1_suffix", "mp_sm_island_1/core"})
					expect(!night_files::override_parent_lookup(&actualParentTable, &actualParentTable, 0,
						invalid, 1, true), "native parent lookup changed an unrelated map or filename");
				expect(!night_files::override_parent_lookup(&unrelatedTable, &actualParentTable, 0,
					"mp_sm_island_1", 1, true) &&
					!night_files::override_parent_lookup(nullptr, nullptr, 0, "mp_sm_island_1", 1, true) &&
					!night_files::override_parent_lookup(&actualParentTable, &actualParentTable, 1,
						"mp_sm_island_1", 1, true) &&
					!night_files::override_parent_lookup(&actualParentTable, &actualParentTable, 0,
						"mp_sm_island_1", 2, true) &&
					!night_files::override_parent_lookup(&actualParentTable, &actualParentTable, 0,
						"mp_sm_island_1", 1, false),
					"native parent lookup escaped source table, lookup column or enable gate");
				expect(std::string_view(night_files::NativeSharedZone("common_core_mp")) == "common_br_mp" &&
					std::string_view(night_files::NativeSharedZone("techsets_common_core_mp")) == "techsets_common_br_mp" &&
					std::string_view(night_files::NativeSharedZone("ww_common_core_mp")) == "ww_common_br_mp" &&
					std::string_view(night_files::NativeSharedZone("eng_common_core_mp")) == "eng_common_br_mp" &&
					night_files::NativeSharedZone("common_br_mp") == nullptr &&
					night_files::NativeSharedZone("techsets_common_br_mp") == nullptr &&
					night_files::NativeSharedZone("srv_common_core_mp") == nullptr &&
					night_files::NativeSharedZone("common_base_mp") == nullptr &&
					night_files::NativeSharedZone("mp_donetsk") == nullptr,
					"Island shared-zone substitution failed to switch all core companions to BR profile");
				expect(night_files::Before("mp_sm_island_1").empty() &&
					night_files::Find("srv_common_br_mp.ff") < 0,
					"native BR profile concealed missing server assets behind a false empty envelope");
				// Client-side Island join is scoped to the target from the real
				// engine parent lookup, never a global MP/frontend override.
				night_files::island_dedicated_br_launch = false;
				expect(night_files::NativeSharedZone("common_core_mp") == nullptr,
					"client frontend unexpectedly selected Island BR before native parent lookup");
				// A real native matchmaking assignment carries the original
				// Island map even though the Replay playlist's entry still names
				// Donetsk. Only exact authenticated Island/BR on supported
				// playlists may preload the correct NCS shared parent family.
				expect(night_files::island_authenticated_assignment("mp_sm_island_1", "br", 104) &&
					night_files::island_authenticated_assignment("mp_sm_island_1", "br", 71) &&
					!night_files::island_authenticated_assignment("mp_donetsk", "br", 104) &&
					!night_files::island_authenticated_assignment("mp_sm_island_1", "war", 104) &&
					!night_files::island_authenticated_assignment("mp_sm_island_1", "br", 15) &&
					!night_files::island_authenticated_assignment("mp_sm_island_1.ff", "br", 104),
					"native matchmaking Island/BR map identity gate accepts unrelated map/mode/playlist");
				expect(client_playlist_map::AssignedIslandMap("mp_sm_island_1", "br", 104) == "mp_sm_island_1" &&
					client_playlist_map::AssignedIslandMap("mp_sm_island_1", "br", 71) == "mp_sm_island_1" &&
					client_playlist_map::AssignedIslandMap("mp_sm_island_1", "war", 104).empty() &&
					client_playlist_map::AssignedIslandMap("mp_sm_island_1", "br", 15).empty() &&
					client_playlist_map::AssignedIslandMap("mp_donetsk", "br", 104).empty() &&
					client_playlist_map::AssignedIslandMap("mp_sm_island_1.ff", "br", 104).empty() &&
					std::string_view(client_playlist_map::kReplayUiMapnameDvar) == "NSQLTTMRMP",
					"client ui_mapname lookup may override unrelated maps, wrong playlist, or use unobfuscated name");
				// A snapshot may restore stock Donetsk in the native MP/BR lobby,
				// but never pin Island through unrelated modes or into gameplay.
					expect(client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 104,
						true, true, 7, 4) &&
						client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 71,
							true, true, 7, 7) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 104,
							false, true, 7, 4) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 104,
							true, false, 7, 4) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 104,
							true, true, 7, 2) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 104,
							true, true, 10, 7) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_donetsk", "br", 104,
							true, true, 7, 4) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "war", 104,
							true, true, 7, 4) &&
						!client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br", 15,
							true, true, 7, 4),
						"Island lobby ui_mapname synchronization leaked into gameplay, unknown map or unrelated playlist");
				expect(night_files::SelectAuthenticatedIslandClientMatch("mp_sm_island_1", "br", 104) &&
					std::string_view(night_files::NativeSharedZone("common_core_mp")) == "common_br_mp",
					"authentic native Blood Money assignment failed to select Replay BR shared parent");
				night_files::ClearAuthenticatedIslandClientMatch();
				expect(night_files::NativeSharedZone("common_core_mp") == nullptr &&
					!night_files::SelectAuthenticatedIslandClientMatch("mp_donetsk", "br", 104) &&
					night_files::NativeSharedZone("common_core_mp") == nullptr,
					"native Island client profile leaked after cancel or Donetsk assignment");
				night_files::island_client_br_parent_selected.store(true);
				expect(std::string_view(night_files::NativeSharedZone("common_core_mp")) == "common_br_mp" &&
					std::string_view(night_files::NativeSharedZone("ww_common_core_mp")) == "ww_common_br_mp",
					"authenticated Island client map parent did not select native BR profile");
				night_files::island_client_br_parent_selected.store(false);
				expect(night_files::NativeSharedZone("common_core_mp") == nullptr &&
					night_files::NativeSharedZone("techsets_common_core_mp") == nullptr,
					"client kept Island BR shared profile after another map was selected");
				night_files::island_dedicated_br_launch = true;
				menu_art1645::g_original.store(&schedule);
				const menu_art1645::ZoneInfo stock_common[]{ { "common_core_mp", 0x8A, 1, 3, 17 } };
				menu_art1645::load_fastfiles(stock_common, 1, 7, 9);
				expect(scheduled.size() == 1 && std::string_view(scheduled[0].name) == "common_br_mp" &&
					scheduled[0].flags == 0x8A && scheduled[0].failureMode == 1 &&
					scheduled[0].priority == 3 && scheduled[0].reserved == 17,
					"native common BR profile did not retain zone descriptor lifetime and flags");
				const menu_art1645::ZoneInfo stock_core_profile[]{
					{ "techsets_common_core_mp", 0x17, 2, 4, 99 },
					{ "ww_common_core_mp", 0x18, 2, 5, 100 },
					{ "eng_common_core_mp", 0x19, 3, 6, 101 },
					{ "common_core_mp", 0x1A, 3, 7, 102 },
				};
				menu_art1645::load_fastfiles(stock_core_profile, std::size(stock_core_profile), 7, 9);
				const char* expected_br_profile[]{
					"techsets_common_br_mp", "ww_common_br_mp", "eng_common_br_mp", "common_br_mp"
				};
				expect(scheduled.size() == std::size(stock_core_profile),
					"native BR profile unexpectedly loaded two common-zone families");
				for (std::size_t i = 0; i < std::size(stock_core_profile); ++i)
					expect(std::string_view(scheduled[i].name) == expected_br_profile[i] &&
					scheduled[i].flags == stock_core_profile[i].flags &&
					scheduled[i].failureMode == stock_core_profile[i].failureMode &&
					scheduled[i].priority == stock_core_profile[i].priority &&
					scheduled[i].reserved == stock_core_profile[i].reserved,
					"native BR profile changed order, flags, priority or mixed core companions");
				night_files::island_native_br_ready = false;
				night_files::island_dedicated_br_launch = false;
				night_files::island_client_br_parent_selected.store(false);
				menu_art1645::load_fastfiles(stock_common, 1, 7, 9);
				expect(received == stock_common,
					"disabled native BR profile no longer falls through to stock common_core_mp");
	#endif
		std::cout << checks << " checks passed: registry, native routing, stock fallthrough, dependencies, caches, pools\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
