#pragma once
#include "engine/inline_hook.h"

#include <array>
#include <filesystem>
#include <string_view>
namespace menu_art1645
{
	inline constexpr char kZone[] = "dev_magma_ui1645_images";
	inline constexpr char kZoneFile[] = "dev_magma_ui1645_images.ff";
	inline constexpr char kZoneSourceFile[] = "magma_ui1645_images.ff";
	inline constexpr char kWorldwideFile[] = "ww_dev_magma_ui1645_images.ff";
	inline constexpr char kEnglishFile[] = "eng_dev_magma_ui1645_images.ff";
	inline constexpr char kTechsetsFile[] = "techsets_dev_magma_ui1645_images.ff";
	inline constexpr char kCache[] = "cache_magma_ui1645";
	inline constexpr char kCacheFile[] = "cache_magma_ui1645.xpak";

	bool Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& config);
	hook::Status Install(uintptr_t moduleBase);
	bool Enabled();
	bool Loaded();
	void ObserveLoad(bool loaded);
	const std::filesystem::path& ZonePath();
	const std::filesystem::path& CachePath();
	const std::filesystem::path& CompanionPath(unsigned index);
	const std::array<uint8_t, 136>& ZoneHeader();
	const std::array<uint8_t, 136>& CompanionHeader();
	const std::array<uint8_t, 800>& CacheHeader();
	bool CacheMatches();
	int FindVideo(std::string_view name);
	const std::filesystem::path& VideoPath(unsigned index);
}
