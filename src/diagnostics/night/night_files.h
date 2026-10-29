#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include "engine/inline_hook.h"
namespace night_files
{
	bool requested(const std::filesystem::path& config);
	bool preserve_settings(const std::filesystem::path& local, const std::filesystem::path& download);
	bool Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& config);
	bool Enabled();
	// Explicit --dedi plus IW8X_XMAS_EMPTY_SRV_POC=1 and [ports] custom_maps=1;
	// true only after all five original PoC files pass the production SHA gate.
	bool ChristmasEmptySrvPoC();
	bool MapEnabled(std::string_view name);
	// True only for an actual --dedi --map mp_sm_island_1 --mode br
	// process, not for a normal client viewing/joining the Island lobby.
	bool DedicatedIslandBR();
	const char* map_display_name(std::string_view name);
	const char* animtree_source(std::string_view zone, std::string_view name);
	int Find(std::string_view name);
	const std::filesystem::path& Path(unsigned index);
	const std::array<std::uint8_t, 136>* Header(unsigned index);
	int FindCache(std::string_view package);
	const std::array<std::uint8_t, 800>& CacheHeader(unsigned index);
	bool CacheMatches(unsigned index);
	std::span<const char* const> CachePackages(std::string_view zone);
	bool OwnsZone(std::string_view name);
	std::span<const char* const> Before(std::string_view name);
	std::span<const char* const> After(std::string_view name);
	// Native BR shared-zone profile: only a verified dedicated Island BR
	// launch may replace stock common_core_mp with stock common_br_mp.
	// The native loader must resolve the real BR companions; never substitute
	// a blank fastfile or append both mutually exclusive common profiles.
	const char* NativeSharedZone(std::string_view name);
	// An authenticated native matchmaking assignment is the authoritative
	// Island selection on the *client*: the stock Blood Money playlist entry
	// can still refer to Donetsk even when the real dedicated map is Island.
	// This only selects the existing native BR common asset family. It never
	// changes map strings, skips NetConstString checks, or admits other maps.
	bool SelectAuthenticatedIslandClientMatch(std::string_view map, std::string_view mode, int playlist) noexcept;
	void ClearAuthenticatedIslandClientMatch() noexcept;
	bool authenticated_island_client_match() noexcept;
	// The native mp/levelZoneParents.csv lookup chooses the parent *before*
	// DB_LoadFastfiles. Install a narrowly scoped Island row at that boundary
	// so the Devmap waiter and the later four-zone loader agree on BR.
	hook::Status InstallIslandNativeZoneParent(uintptr_t moduleBase);
}
