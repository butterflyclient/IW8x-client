#include "diagnostics/night/night_files.h"
#include "client/loader/custom_fastfiles.hpp"
#include "common/console.h"
#include "engine/image_identity.h"
#include "engine/safemem.h"

#include <windows.h>
#include <atomic>
#include <cwctype>
#include <string>

namespace night_files
{
	namespace
	{
		using namespace custom_fastfiles;
		constexpr file_spec escape_files[]{
#include "diagnostics/escape/escape_files.inc"
		};
		constexpr const char* escape_packages[]{ "cache_magma_escape130", "cache_magma_escape130_spatial" };
		constexpr const char* escape_dependencies[]{ "magma_escape_helpers", "magma_escape_trees" };
		constexpr asset_alias escape_trees[]{
			{ "animtrees/animated_props.atr", "animtrees/magma_escape130/animated_props.atr" },
			{ "animtrees/generic_human.atr", "animtrees/magma_escape130/generic_human.atr" },
			{ "animtrees/script_model.atr", "animtrees/magma_escape130/script_model.atr" },
			{ "animtrees/scriptables.atr", "animtrees/magma_escape130/scriptables.atr" },
		};
		constexpr asset_alias night_trees[]{
			{ "animtrees/animated_props.atr", "animtrees/magma_night128/animated_props.atr" },
			{ "animtrees/generic_human.atr", "animtrees/magma_night128/generic_human.atr" },
			{ "animtrees/script_model.atr", "animtrees/magma_night128/script_model.atr" },
			{ "animtrees/scriptables.atr", "animtrees/magma_night128/scriptables.atr" },
		};
		// Script-only companions have no server assets. Reuse the verified empty
		// envelope, never a replacement for the real map's srv_ fastfile.
		constexpr file_alias escape_aliases[]{
			{ "srv_magma_escape_helpers.ff", "ww_magma_escape_helpers.ff" },
			{ "srv_magma_escape_trees.ff", "ww_magma_escape_trees.ff" },
		};
		constexpr bundle escape_bundle{ "mp_escape2", "magma/maps/mp_escape2", escape_files,
			escape_packages, escape_dependencies, escape_aliases, "Rebirth Island", escape_trees };
			// These contain one original Christmas SurfaceFx and four empty
			// sidecars, NOT a converted world. Register only in the explicit PoC.
			constexpr file_spec xmas_empty_srv_poc_files[]{
#include "diagnostics/night/xmas_empty_srv_poc_files.inc"
			};
			static_assert(std::size(xmas_empty_srv_poc_files) == 5);
			constexpr bundle xmas_empty_srv_poc_bundle{ "mp_jup_shipment_xmas",
				"magma/maps/mp_jup_shipment_xmas", xmas_empty_srv_poc_files,
				{}, {}, {}, "Christmas Shipment (empty-srv transport PoC)" };
			bool xmas_empty_srv_poc_ready{};

			bool xmas_empty_srv_poc_dedicated_command_line(std::wstring_view line)
			{
				std::vector<std::wstring> args;
				std::wstring current;
				bool quoted = false;
				for (const wchar_t ch : line)
				{
					if (ch == L'"') { quoted = !quoted; continue; }
					if (!quoted && iswspace(ch))
					{
						if (!current.empty()) { args.push_back(std::move(current)); current.clear(); }
						continue;
					}
					current += static_cast<wchar_t>(towlower(ch));
				}
				if (!current.empty()) args.push_back(std::move(current));
				return !quoted && std::find(args.begin(), args.end(), L"--dedi") != args.end();
			}

			bool xmas_empty_srv_poc_requested()
			{
				char enabled[8]{};
				if (GetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_POC", enabled,
					DWORD(std::size(enabled))) != 1 || enabled[0] != '1') return false;
				const wchar_t* commandLine = GetCommandLineW();
				return commandLine && xmas_empty_srv_poc_dedicated_command_line(commandLine);
			}
#ifdef IW8X_ISLAND_PORT_PROBE
		constexpr file_spec island_probe_files[]{
#include "diagnostics/island/island_probe_files.inc"
		};
		constexpr const char* island_probe_dependencies[]{
			"magma_island_common_stream",
			"mp_aianimset_soldier_lw_br_tr", "mp_aianimset_zombie_lw_br_tr",
			"magma_island_trees", "magma_island_scripts"
		};
			// Original 1.64.5 *pixel content*, preserved in the 136-part
			// content-addressed XPAK, with all 34 GfxImage format indices
			// translated through the verified source/Replay1.20 native DXGI
			// table. The unconverted original image FF is intentionally
			// rejected: it triggered a real Replay divide-by-zero in DB linking.
		// This optional Island-only manifest is strictly independent from the
		// original stock-loot probe until explicitly selected and verified.
		constexpr std::uintmax_t island_image_ff_bytes = 22952;
			constexpr const char* island_image_ff_sha = "11a6a54319d353dc09b37e68336704b1936850ae383c0028f29c115995e6aeda";
		// The vehicle dependency adds 56 exact image parts while retaining
		// all 136 existing source-image parts and the parachute geometry.
		constexpr std::uintmax_t island_image_cache_bytes = 71368704;
		constexpr const char* island_image_cache_sha = "25ce890859dd1864846a842af279ccd0f4f7d937e8a944b3733992b31015529b";
		// Real stock Replay custom zones request world-wide, local-language and
		// server companions in addition to the principal nonlocalized data FF.
		// Each has zero localized/server assets for this GfxImage-only zone.
		// Use the *identical* native 32-decoded-byte/192-FF envelope already
		// supplied for five other Island-only companions, not an unrelated FF.
		constexpr std::uintmax_t island_empty_locale_ff_bytes = 192;
		constexpr const char* island_empty_locale_ff_sha = "95013b77c421a33586255d2129fce96e76a9cf4e0b30bb696d5c7dd8f6224611";
		constexpr auto island_image_files = []
		{
			std::array<file_spec, std::size(island_probe_files) + 5> result{};
			for (std::size_t index = 0; index < std::size(island_probe_files); ++index)
			{
				result[index] = island_probe_files[index];
				if (std::string_view(result[index].name) == "cache_magma_island164_common.xpak")
					result[index] = { "cache_magma_island164_common.xpak", island_image_cache_bytes, island_image_cache_sha };
			}
			result[std::size(island_probe_files)] = { "magma_island_34_verified_source_images.ff", island_image_ff_bytes, island_image_ff_sha };
			result[std::size(island_probe_files) + 1] = { "ww_magma_island_34_verified_source_images.ff", island_empty_locale_ff_bytes, island_empty_locale_ff_sha };
			result[std::size(island_probe_files) + 2] = { "eng_magma_island_34_verified_source_images.ff", island_empty_locale_ff_bytes, island_empty_locale_ff_sha };
			result[std::size(island_probe_files) + 3] = { "srv_magma_island_34_verified_source_images.ff", island_empty_locale_ff_bytes, island_empty_locale_ff_sha };
			// Source-only 34 GfxImages add zero shader techniques; still supply
			// the engine-requested, genuine empty Replay techsets companion.
			result[std::size(island_probe_files) + 4] = { "techsets_magma_island_34_verified_source_images.ff", island_empty_locale_ff_bytes, island_empty_locale_ff_sha };
			return result;
		}();
		// Diagnostic A/B: use exactly the existing Island map/GSC dependency
		// closure, with the *already installed* merged and hash-verified XPAK,
		// without scheduling the 34-image FF. This distinguishes a native
		// GfxImage zone load problem from script/BR gameplay regressions and
		// never aliases missing loot/assets to stock unrelated definitions.
		constexpr auto island_replay120_merged_cache_control_files = []
		{
			std::array<file_spec, std::size(island_probe_files)> result{};
			for (std::size_t i = 0; i < result.size(); ++i)
			{
				result[i] = island_probe_files[i];
				if (std::string_view(result[i].name) == "cache_magma_island164_common.xpak")
					result[i] = { "cache_magma_island164_common.xpak", island_image_cache_bytes, island_image_cache_sha };
			}
			return result;
		}();
		constexpr const char* island_image_dependencies[]{
			"magma_island_34_verified_source_images",
			"magma_island_common_stream",
			"mp_aianimset_soldier_lw_br_tr", "mp_aianimset_zombie_lw_br_tr",
			"magma_island_trees", "magma_island_scripts"
		};
			constexpr std::uintmax_t native_common_br_bytes = 27689096;
			constexpr const char* native_common_br_sha = "b5fcd70116db8848b20de60dec4ef4234c4183ad13667aab456de415569c6229";
			constexpr std::uintmax_t native_br_techsets_bytes = 426120;
			constexpr const char* native_br_techsets_sha = "33a19e19c1a0afbf03ad2c62d4dd483d7e1f6f060d662874daec9c635541e325";
			bool island_native_br_ready{};
			bool island_dedicated_br_launch{};
			// On a normal client the shared BR parent must NOT replace the MP
			// frontend or unrelated maps. The verified engine's own
			// levelZoneParents(mp_sm_island_1) request selects Island first.
			std::atomic<bool> island_client_br_parent_selected{false};
			std::atomic<bool> island_native_match_assigned{false};
			bool island_authenticated_assignment(std::string_view map, std::string_view mode, int playlist) noexcept
			{
				return map == "mp_sm_island_1" && mode == "br" && (playlist == 104 || playlist == 71);
			}
			bool island_shared_profile_permitted(bool verified, bool registered,
				bool dedicatedIsland, bool selectedIslandClient) noexcept
			{
				return verified && registered && (dedicatedIsland || selectedIslandClient);
			}
			// Replay 1.20.4 native mp/levelZoneParents.csv parent resolver:
			// 0x10B2D00 and 0x10B52F0 both read the table cached at 0xC4EBE10
			// and call StringTable_Lookup at 0x13E2BA0. DB_LoadFastfiles runs
			// later, so editing only the zone descriptor leaves the *upstream*
			// Devmap task tracker expecting common_core_mp indefinitely.
			constexpr uintptr_t native_parent_lookup_rva = 0x13E2BA0;
			constexpr uintptr_t native_parent_table_rva = 0xC4EBE10;
			constexpr uint8_t native_parent_lookup_bytes[]{
				0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48,
				0x83, 0xEC, 0x28, 0x45, 0x8B, 0xE9, 0x4D, 0x8B
			};
			using NativeParentLookupFn = const char*(__fastcall*)(const void*, int, const char*, int);
			std::atomic<NativeParentLookupFn> original_parent_lookup{};
			std::atomic<bool> reported_parent_override{};
			std::atomic<bool> reported_parent_table_mismatch{};
			uintptr_t parent_game_base{};
			bool override_parent_lookup(const void* table, const void* registeredTable, int lookupColumn,
				std::string_view key, int resultColumn, bool enabled)
			{
				return enabled && table && table == registeredTable && lookupColumn == 0 && resultColumn == 1 &&
					key == "mp_sm_island_1";
			}
			// The native StringTable pool for Replay 1.20.4 has 48-byte
			// entries. The cached levelZoneParents pointer is not guaranteed to
			// be the same DB allocation as a later client frontend lookup.
			// Accept such a pointer only when it independently identifies the
			// genuine stock mp/levelzoneparents.csv (2 columns, 12 rows).
			// Do not infer a global BR profile from an unrelated lookup.
			bool island_parent_table_identity(const void* table, const void* registeredTable,
				std::string_view nativeName, int columns, int rows) noexcept
			{
				return table && (table == registeredTable ||
					(nativeName == "mp/levelzoneparents.csv" && columns == 2 && rows == 12));
			}
			const char* __fastcall native_parent_lookup(const void* table, int lookupColumn, const char* key, int resultColumn)
			{
				auto original = original_parent_lookup.load(std::memory_order_acquire);
				if (island_native_br_ready && table && lookupColumn == 0 && resultColumn == 1)
				{
					const void* registeredTable = nullptr;
					char map[64]{};
					if (parent_game_base &&
						safemem::ReadBytes(reinterpret_cast<const void*>(parent_game_base + native_parent_table_rva),
							&registeredTable, sizeof(registeredTable)) &&
							safemem::ReadString(key, map, sizeof(map)))
					{
							const bool requestingIsland = std::string_view(map) == "mp_sm_island_1";
							bool tableVerified = table == registeredTable;
							if (requestingIsland && !tableVerified)
							{
								// Read-only/fail-closed independent identity; never
								// reinterpret arbitrary engine memory without checking it.
								struct native_stringtable_head
								{
									const char* name;
									int32_t columns;
									int32_t rows;
								};
								native_stringtable_head head{};
								char nativeName[64]{};
								if (safemem::ReadBytes(table, &head, sizeof(head)) &&
										safemem::ReadString(head.name, nativeName, sizeof(nativeName)))
									tableVerified = island_parent_table_identity(table, registeredTable, nativeName,
											head.columns, head.rows);
							}
							if (requestingIsland && !tableVerified)
							{
								if (!reported_parent_table_mismatch.exchange(true))
									LOG_WARN("NightFiles", "Island native parent lookup for actual mp_sm_island_1 used an unverified StringTable identity; leaving original result");
							}
							if (!tableVerified) return original(table, lookupColumn, key, resultColumn);
						// A second, unrelated map query clears a prior Island-only
						// client load. Never inherit the BR profile into MP/War.
						if (!island_dedicated_br_launch && !island_native_match_assigned.load(std::memory_order_acquire) &&
							std::string_view(map).starts_with("mp_"))
							island_client_br_parent_selected.store(std::string_view(map) == "mp_sm_island_1",
								std::memory_order_release);
							if (requestingIsland)
						{
							if (!reported_parent_override.exchange(true))
									LOG_INFO("NightFiles", "native levelZoneParents lookup: mp_sm_island_1 => common_br_mp (genuine native table identity verified)");
								// Exact island map key in the *actual* native parent table.
								// This also runs in a verified client join; the native
								// map-specific parent selection is the source of truth.
							// Never create an empty zone, impersonate another table, or
							// alter the parent choice of Donetsk/other maps.
							return "common_br_mp";
						}
					}
				}
				return original(table, lookupColumn, key, resultColumn);
			}
		constexpr const char* island_probe_packages[]{
			"cache_magma_island164", "cache_magma_island164_models", "cache_magma_island164_spatial",
			"cache_magma_island164_common"
		};
		constexpr asset_alias island_probe_trees[]{
#include "diagnostics/island/island_probe_trees.inc"
		};
		constexpr bundle island_probe_bundle{ "mp_sm_island_1", "magma/maps/mp_sm_island_1", island_probe_files,
			island_probe_packages, island_probe_dependencies, {}, "Island serialization probe", island_probe_trees };
		constexpr bundle island_image_bundle{ "mp_sm_island_1", "magma/maps/mp_sm_island_1", island_image_files,
			island_probe_packages, island_image_dependencies, {}, "Island original GfxImages", island_probe_trees };
		constexpr bundle island_replay120_merged_cache_control_bundle{ "mp_sm_island_1", "magma/maps/mp_sm_island_1",
				island_replay120_merged_cache_control_files, island_probe_packages, island_probe_dependencies,
				{}, "Island Replay 1.20 native-loot image-load A/B control", island_probe_trees };
#endif
#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
		constexpr file_spec night_manifest[]{
#include "diagnostics/night/night_files.inc"
		};
		constexpr const char* night_packages[]{ "cache_magma_night128", "cache_magma_night128_spatial" };
		constexpr const char* night_dependencies[]{ "magma_night_helpers", "magma_night_trees" };
		constexpr file_alias night_aliases[]{
			{ "srv_magma_night_helpers.ff", "ww_magma_night_helpers.ff" },
			{ "srv_magma_night_trees.ff", "ww_magma_night_trees.ff" },
		};
		constexpr bundle night_bundle{ "mp_kstenod", "magma/maps/mp_kstenod", night_manifest,
			night_packages, night_dependencies, night_aliases, nullptr, night_trees };
#endif
		registry files;
			bool island_br_dedicated_command_line(std::wstring_view line)
			{
				std::vector<std::wstring> args;
				std::wstring current;
				bool quoted = false;
				for (const wchar_t ch : line)
				{
					if (ch == L'"') { quoted = !quoted; continue; }
					if (!quoted && iswspace(ch))
					{
						if (!current.empty()) { args.push_back(std::move(current)); current.clear(); }
						continue;
					}
					current += static_cast<wchar_t>(towlower(ch));
				}
				if (!current.empty()) args.push_back(std::move(current));
				if (quoted || std::find(args.begin(), args.end(), L"--dedi") == args.end()) return false;
				bool island = false, br = false;
				for (std::size_t index = 0; index + 1 < args.size(); ++index)
				{
					if (args[index] == L"--map")
						island = args[index + 1] == L"mp_sm_island_1.ff" || args[index + 1] == L"mp_sm_island_1";
					if (args[index] == L"--mode") br = args[index + 1] == L"br";
				}
				return island && br;
			}
			bool island_br_dedicated_command_line()
			{
				const wchar_t* line = GetCommandLineW();
				return line && island_br_dedicated_command_line(std::wstring_view(line));
			}

		bool selected(const std::filesystem::path& config, const wchar_t* key)
		{
			return GetPrivateProfileIntW(L"ports", key, 0, config.c_str()) == 1;
		}
	}

	bool requested(const std::filesystem::path& config)
	{
			return selected(config, L"custom_maps");
	}

	bool Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& config)
	{
		files = {};
			xmas_empty_srv_poc_ready = false;
			const bool xmasPoc = xmas_empty_srv_poc_requested();
			if (xmasPoc && !requested(config))
			{
				LOG_ERR("CustomFastfiles", "Christmas empty-srv PoC requires [ports] custom_maps=1; refusing unregistered fastfile experiment");
				return false;
			}
	#ifdef IW8X_ISLAND_PORT_PROBE
		island_native_br_ready = false;
		const bool native_island_br = island_br_dedicated_command_line();
			island_dedicated_br_launch = native_island_br;
			island_client_br_parent_selected.store(false, std::memory_order_release);
			island_native_match_assigned.store(false, std::memory_order_release);
		char original_images_env[8]{};
		const bool island_original_images = native_island_br &&
			GetEnvironmentVariableA("IW8X_ISLAND_ORIGINAL_IMAGES", original_images_env,
				DWORD(std::size(original_images_env))) == 1 && original_images_env[0] == '1';
			char baseline_env[8]{};
			const bool island_replay120_image_control = native_island_br && !island_original_images &&
				GetEnvironmentVariableA("IW8X_ISLAND_120_LOOT_BASELINE_WITH_MERGED_XPAK", baseline_env,
					DWORD(std::size(baseline_env))) == 1 && baseline_env[0] == '1';
		if (native_island_br && !requested(config))
		{
			LOG_ERR("CustomFastfiles", "dedicated Island BR requires [ports] custom_maps=1; refusing unregistered map and core asset fallback");
			return false;
		}
	#endif
		std::vector<bundle> bundles;
			if (!requested(config)) return files.initialize(gameRoot, bundles);
			// One opt-in enables *installed* converted map bundles. A missing
			// map is harmless; any map whose primary FF is present MUST pass
			// the complete SHA/header/XPAK manifest before being advertised.
			const auto installed = [&](const bundle& candidate)
			{
				std::error_code error;
				return std::filesystem::is_regular_file(
					gameRoot / candidate.folder / (std::string(candidate.zone) + ".ff"), error);
			};
#ifdef IW8X_ISLAND_PORT_PROBE
		if (native_island_br && !installed(island_probe_bundle))
		{
			LOG_ERR("CustomFastfiles", "dedicated Island BR primary fastfile is missing; refusing shared-zone change without a converted map");
			return false;
		}
			if (installed(island_probe_bundle))
		{
				const auto stock = gameRoot / L"zone" / L"common_br_mp.ff";
					const auto techsets = gameRoot / L"zone" / L"techsets_common_br_mp.ff";
				std::error_code io_error;
				const bool stock_verified =
						std::filesystem::file_size(stock, io_error) == native_common_br_bytes && !io_error &&
							image_identity::Digest(stock.c_str()) == native_common_br_sha &&
							std::filesystem::file_size(techsets, io_error) == native_br_techsets_bytes && !io_error &&
							image_identity::Digest(techsets.c_str()) == native_br_techsets_sha;
				if (stock_verified)
			{
				island_native_br_ready = true;
					LOG_INFO("CustomFastfiles", "Island BR native common_br_mp profile verified; dedicated=%u; client selection stays map-specific", unsigned(native_island_br));
			}
			else if (native_island_br)
			{
				LOG_ERR("CustomFastfiles", "Island BR requires verified common_br_mp and techsets_common_br_mp archives; refusing incompatible core profile fallback");
				return false;
			}
				else
				{
					LOG_WARN("CustomFastfiles", "Island client native BR parent not activated: installed stock common_br_mp integrity was not verified");
				}
				// Never insert a second common profile, nor map missing BR
				// companion fastfiles to an unrelated empty envelope. The native
				// loader will request the real companions of the selected profile.
			if (island_original_images)
				LOG_INFO("CustomFastfiles", "Island source image dependency mode requested: native 34-image FF precedes map; merged common XPAK required");
				if (island_replay120_image_control)
					LOG_INFO("CustomFastfiles", "Island Replay 1.20 native BR loot A/B control: verified merged cache, no additional GfxImage fastfile scheduled");
				// Modern Island installs have the *merged* verified 136-packet
				// XPAK, not the old 720896-byte serialization-only placeholder.
				// In particular a normal CLIENT has no IW8X_ISLAND_ORIGINAL_IMAGES
				// environment flag. Sending it through island_probe_bundle caused
				// custom_content/post_unpack to reject the valid merged XPAK and
				// reported an unrelated auth/fence startup failure. When the
				// corrected 34-image FF is present, use the exact hash-pinned
				// image manifest on both client and dedicated Island launches.
				// Preserve the explicit Replay120 A/B mode to isolate image load.
				const auto source_images_present = std::filesystem::is_regular_file(
					gameRoot / island_image_bundle.folder / "magma_island_34_verified_source_images.ff");
				if (island_replay120_image_control)
					bundles.push_back(island_replay120_merged_cache_control_bundle);
				else if (island_original_images || source_images_present)
				{
					LOG_INFO("CustomFastfiles", "Island corrected Replay120 GfxImages and merged native XPAK selected; all files hash-verified");
					bundles.push_back(island_image_bundle);
				}
				else
				{
					LOG_WARN("CustomFastfiles", "Island source image zone absent: legacy serialization-only profile (runtime gameplay incomplete)");
					bundles.push_back(island_probe_bundle);
				}
		}
#endif
			if (installed(escape_bundle))
			bundles.push_back(escape_bundle);
			if (xmasPoc)
			{
				if (!installed(xmas_empty_srv_poc_bundle))
				{
					LOG_ERR("CustomFastfiles", "explicit Christmas PoC requested but its primary FF is missing");
					return false;
				}
				bundles.push_back(xmas_empty_srv_poc_bundle);
			}
#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
			if (installed(night_bundle))
			bundles.push_back(night_bundle);
#endif
			if (bundles.empty())
				LOG_WARN("CustomFastfiles", "custom_maps=1 but no converted map FF was found under magma/maps");
			const bool valid = files.initialize(gameRoot, bundles);
			xmas_empty_srv_poc_ready = valid && xmasPoc && files.map_enabled("mp_jup_shipment_xmas");
			if (xmas_empty_srv_poc_ready)
				LOG_INFO("CustomFastfiles", "Christmas empty-srv PoC registered all five hash-pinned transport files; no Christmas world or gameplay claim");
			return valid;
	}

	bool preserve_settings(const std::filesystem::path& local, const std::filesystem::path& download)
	{
		// Native pool choices are local and have already been read at process
		// attach. A connection-config download must not change them afterward.
		std::array<wchar_t, 32768> section{};
		const auto length = GetPrivateProfileSectionW(L"ports", section.data(), DWORD(section.size()), local.c_str());
		if (length >= section.size() - 2) return false;
		if (!WritePrivateProfileSectionW(L"ports", section.data(), download.c_str())) return false;
		WritePrivateProfileStringW(nullptr, nullptr, nullptr, download.c_str());
		return true;
	}

	bool Enabled()
	{
		return files.enabled();
	}
		bool ChristmasEmptySrvPoC()
		{
			return xmas_empty_srv_poc_ready;
		}
	bool MapEnabled(std::string_view name)
	{
		return files.map_enabled(name);
	}
	bool DedicatedIslandBR()
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		return island_dedicated_br_launch;
#else
		return false;
#endif
	}
	int Find(std::string_view name)
	{
		return files.find(name);
	}
	const char* map_display_name(std::string_view name)
	{
		return files.map_display_name(name);
	}
	const char* animtree_source(std::string_view zone, std::string_view name)
	{
		return files.animtree_source(zone, name);
	}
	const std::filesystem::path& Path(unsigned index)
	{
		return files.path(index);
	}
	const std::array<std::uint8_t, 136>* Header(unsigned index)
	{
		return files.header(index);
	}
	int FindCache(std::string_view name)
	{
		return files.find_cache(name);
	}
	const std::array<std::uint8_t, 800>& CacheHeader(unsigned index)
	{
		return files.cache_header(index);
	}
	bool CacheMatches(unsigned index)
	{
		return files.cache_matches(index);
	}
	std::span<const char* const> CachePackages(std::string_view zone)
	{
		return files.cache_packages(zone);
	}
	bool OwnsZone(std::string_view name)
	{
		return Find(std::string(name) + ".ff") >= 0;
	}
	std::span<const char* const> Before(std::string_view name)
	{
		return files.before(name);
	}
	std::span<const char* const> After(std::string_view)
	{
		return {};
	}
	const char* NativeSharedZone(std::string_view name)
	{
#ifdef IW8X_ISLAND_PORT_PROBE
			if (island_shared_profile_permitted(island_native_br_ready,
				files.map_enabled("mp_sm_island_1"), island_dedicated_br_launch,
				island_client_br_parent_selected.load(std::memory_order_acquire)))
		{
			// The native game requests shared-zone techsets and localized
			// companions independently of the principal common zone. Selecting
			// just common_br_mp otherwise leaves common_core_mp's companions
			// resident, creating a mixed-profile DB (and invalidating the
			// ScriptableDef/loot dependency graph).
			if (name == "common_core_mp") return "common_br_mp";
			if (name == "techsets_common_core_mp") return "techsets_common_br_mp";
			if (name == "ww_common_core_mp") return "ww_common_br_mp";
			if (name == "eng_common_core_mp") return "eng_common_br_mp";
		}
#endif
		return nullptr;
	}
		bool SelectAuthenticatedIslandClientMatch(std::string_view map, std::string_view mode, int playlist) noexcept
		{
#ifdef IW8X_ISLAND_PORT_PROBE
			const bool selected = !island_dedicated_br_launch && island_native_br_ready &&
				files.map_enabled("mp_sm_island_1") && island_authenticated_assignment(map, mode, playlist);
			island_native_match_assigned.store(selected, std::memory_order_release);
			island_client_br_parent_selected.store(selected, std::memory_order_release);
			if (selected)
				LOG_INFO("NightFiles", "authenticated native Island BR matchmaking assignment selected real common_br_mp shared asset family; playlist=%d", playlist);
			return selected;
#else
			(void)map;(void)mode;(void)playlist;
			return false;
#endif
		}
		void ClearAuthenticatedIslandClientMatch() noexcept
		{
#ifdef IW8X_ISLAND_PORT_PROBE
			island_native_match_assigned.store(false, std::memory_order_release);
			if (!island_dedicated_br_launch)
				island_client_br_parent_selected.store(false, std::memory_order_release);
#endif
		}
	bool authenticated_island_client_match() noexcept
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		return island_native_match_assigned.load(std::memory_order_acquire);
#else
		return false;
#endif
	}
	hook::Status InstallIslandNativeZoneParent(uintptr_t moduleBase)
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		if (!island_native_br_ready || !files.map_enabled("mp_sm_island_1"))
			return hook::Status::Installed;
		if (!moduleBase) return hook::Status::Failed;
		if (original_parent_lookup.load(std::memory_order_acquire)) return hook::Status::Installed;
		parent_game_base = moduleBase;
		const auto status = hook::Install(reinterpret_cast<void*>(moduleBase + native_parent_lookup_rva),
			&native_parent_lookup, native_parent_lookup_bytes, sizeof(native_parent_lookup_bytes), original_parent_lookup);
		if (status == hook::Status::Installed)
			LOG_INFO("NightFiles", "Island BR native mp/levelZoneParents.csv parent lookup installed (RVA 0x13E2BA0); BR and Devmap task tracker share one parent");
		return status;
#else
		(void)moduleBase;
		return hook::Status::Installed;
#endif
	}
}
