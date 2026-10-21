#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
namespace weapon_files
{
	bool Initialize(const std::filesystem::path&, const std::filesystem::path&);
	bool Enabled();
	int Find(std::string_view);
	const std::filesystem::path& Path(unsigned);
	const std::array<std::uint8_t, 136>* Header(unsigned);
	int FindCache(std::string_view);
	const std::array<std::uint8_t, 800>& CacheHeader(unsigned);
	bool CacheMatches(unsigned);
	std::span<const char* const> CachePackages();
	bool OwnsZone(std::string_view);
	std::span<const char* const> After(std::string_view);
}
