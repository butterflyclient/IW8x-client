#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace custom_fastfiles
{
	struct file_spec
	{
		const char* name;
		std::uint64_t size;
		const char* sha;
	};

	struct file_alias
	{
		const char* name;
		const char* target;
	};

	struct asset_alias
	{
		const char* name;
		const char* target;
	};

	struct bundle
	{
		const char* zone;
		const char* folder;
		std::span<const file_spec> files;
		std::span<const char* const> packages;
		std::span<const char* const> dependencies;
		std::span<const file_alias> aliases;
		const char* display_name{};
		std::span<const asset_alias> animation_trees{};
	};

	// Descriptor strings must outlive the registry. Native handles and stream
	// ownership, including unload/restart, remain with Replay's existing loader.
	class registry
	{
		struct resident_file
		{
			file_spec spec;
			std::filesystem::path path;
			std::array<std::uint8_t, 136> header{};
			unsigned owner;
		};
		struct cache_file
		{
			const char* name;
			unsigned file;
			std::array<std::uint8_t, 800> header{};
		};
		std::vector<bundle> bundles_;
		std::vector<resident_file> files_;
		std::vector<cache_file> caches_;
		int lookup(std::string_view name) const;
		bool matches(unsigned index) const;

	public:
		bool initialize(const std::filesystem::path& root, std::span<const bundle> bundles);
		bool enabled() const;
		bool map_enabled(std::string_view name) const;
		const char* map_display_name(std::string_view name) const;
		const char* animtree_source(std::string_view zone, std::string_view name) const;
		int find(std::string_view name) const;
		const std::filesystem::path& path(unsigned index) const;
		const std::array<std::uint8_t, 136>* header(unsigned index) const;
		int find_cache(std::string_view name) const;
		const std::array<std::uint8_t, 800>& cache_header(unsigned index) const;
		bool cache_matches(unsigned index) const;
		std::span<const char* const> cache_packages(std::string_view zone) const;
		std::span<const char* const> before(std::string_view zone) const;
	};
}
