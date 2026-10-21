#include "client/loader/custom_fastfiles.hpp"
#include "common/console.h"
#include "engine/image_identity.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

namespace custom_fastfiles
{
	namespace
	{
		bool filename(std::string_view name)
		{
			return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == name.npos;
		}

		bool animtree_name(std::string_view name)
		{
			return name.size() < 1024 && name.starts_with("animtrees/") && name.ends_with(".atr") &&
				name.find_first_of("\\:") == name.npos && name.find("//") == name.npos &&
				name.find("/../") == name.npos && name.find("/./") == name.npos;
		}
	}

	int registry::lookup(std::string_view name) const
	{
		const auto it = std::lower_bound(files_.begin(), files_.end(), name,
			[](const resident_file& file, std::string_view key)
			{
				return file.spec.name < key;
			});
		return it != files_.end() && name == it->spec.name ? static_cast<int>(it - files_.begin()) : -1;
	}

	bool registry::matches(unsigned index) const
	{
		if (index >= files_.size()) return false;
		const auto& file = files_[index];
		std::error_code error;
		return std::filesystem::file_size(file.path, error) == file.spec.size && !error &&
			image_identity::Digest(file.path.c_str()) == file.spec.sha;
	}

	bool registry::initialize(const std::filesystem::path& root, std::span<const bundle> bundles)
	{
		*this = {};
		registry pending;
		pending.bundles_.assign(bundles.begin(), bundles.end());
		for (unsigned owner = 0; owner < bundles.size(); ++owner)
		{
			const auto& bundle = bundles[owner];
			if (!bundle.zone || !bundle.folder) return false;
			const std::filesystem::path folder(bundle.folder);
			if (!filename(bundle.zone) || folder.empty() || folder.is_absolute() || folder.has_root_path()) return false;
			for (const auto& part : folder)
			{
				if (part == "..") return false;
			}
			for (unsigned other = 0; other < owner; ++other)
			{
				if (std::string_view(bundle.zone) == bundles[other].zone) return false;
			}
			for (const auto& spec : bundle.files)
			{
				if (!spec.name || !filename(spec.name) || !spec.sha || std::strlen(spec.sha) != 64) return false;
				pending.files_.push_back({ spec, root / folder / spec.name, {}, owner });
			}
		}
		std::sort(pending.files_.begin(), pending.files_.end(), [](const auto& a, const auto& b)
		{
			return std::string_view(a.spec.name) < b.spec.name;
		});
		for (unsigned i = 0; i < pending.files_.size(); ++i)
		{
			auto& file = pending.files_[i];
			if (i && std::string_view(pending.files_[i - 1].spec.name) == file.spec.name) return false;
			if (!pending.matches(i))
			{
				LOG_ERR("CustomFastfiles", "missing or changed converted file: %ls", file.path.c_str());
				return false;
			}
			if (std::string_view(file.spec.name).ends_with(".ff"))
			{
				std::ifstream input(file.path, std::ios::binary);
				if (!input.read(reinterpret_cast<char*>(file.header.data()), file.header.size()) ||
					std::memcmp(file.header.data(), "IWffc100", 8)) return false;
			}
		}
		std::vector<std::string_view> aliases;
		for (unsigned owner = 0; owner < bundles.size(); ++owner)
		{
			const auto& bundle = bundles[owner];
			const auto owned = [&](std::string_view name)
			{
				const auto index = pending.lookup(name);
				return index >= 0 && pending.files_[index].owner == owner;
			};
			if (!owned(std::string(bundle.zone) + ".ff")) return false;
			for (const auto* dependency : bundle.dependencies)
			{
				if (!dependency || !filename(dependency) || !owned(std::string(dependency) + ".ff")) return false;
			}
			for (const auto& alias : bundle.aliases)
			{
				if (!alias.name || !alias.target || !filename(alias.name) || !owned(alias.target) || pending.lookup(alias.name) >= 0 ||
					std::find(aliases.begin(), aliases.end(), alias.name) != aliases.end()) return false;
				aliases.emplace_back(alias.name);
			}
			std::vector<std::string_view> trees;
			for (const auto& alias : bundle.animation_trees)
			{
				if (!alias.name || !alias.target || !animtree_name(alias.name) || !animtree_name(alias.target) ||
					std::find(trees.begin(), trees.end(), alias.name) != trees.end()) return false;
				trees.emplace_back(alias.name);
			}
			for (const auto* package : bundle.packages)
			{
				if (!package || !filename(package) || !owned(std::string(package) + ".xpak") || pending.find_cache(package) >= 0) return false;
				cache_file cache{ package, static_cast<unsigned>(pending.lookup(std::string(package) + ".xpak")) };
				std::ifstream input(pending.files_[cache.file].path, std::ios::binary);
				if (!input.read(reinterpret_cast<char*>(cache.header.data()), cache.header.size())) return false;
				pending.caches_.push_back(cache);
			}
		}
		*this = std::move(pending);
		if (enabled()) LOG_INFO("CustomFastfiles", "verified %u bundles, %u files", unsigned(bundles_.size()), unsigned(files_.size()));
		return true;
	}

	bool registry::enabled() const
	{
		return !bundles_.empty();
	}
	bool registry::map_enabled(std::string_view name) const
	{
		return std::any_of(bundles_.begin(), bundles_.end(), [name](const auto& bundle)
		{
			return name == bundle.zone;
		});
	}

	const char* registry::map_display_name(std::string_view name) const
	{
		for (const auto& bundle : bundles_)
		{
			if (name == bundle.zone) return bundle.display_name;
		}
		return nullptr;
	}

	const char* registry::animtree_source(std::string_view zone, std::string_view name) const
	{
		for (const auto& bundle : bundles_)
		{
			if (zone != bundle.zone) continue;
			for (const auto& alias : bundle.animation_trees)
				if (name == alias.name) return alias.target;
		}
		return nullptr;
	}

	int registry::find(std::string_view name) const
	{
		if (name.starts_with("english/") || name.starts_with("english\\"))
		{
			name.remove_prefix(8);
			if (!name.starts_with("eng_") || !name.ends_with(".sabs")) return -1;
		}
		if (!filename(name)) return -1;
		for (const auto& bundle : bundles_)
		{
			for (const auto& alias : bundle.aliases)
			{
				if (name == alias.name) return lookup(alias.target);
			}
		}
		return lookup(name);
	}

	const std::filesystem::path& registry::path(unsigned index) const
	{
		return files_.at(index).path;
	}
	const std::array<std::uint8_t, 136>* registry::header(unsigned index) const
	{
		return index < files_.size() && std::string_view(files_[index].spec.name).ends_with(".ff") ? &files_[index].header : nullptr;
	}

	int registry::find_cache(std::string_view name) const
	{
		for (unsigned i = 0; i < caches_.size(); ++i)
		{
			if (name == caches_[i].name) return static_cast<int>(i);
		}
		return -1;
	}
	const std::array<std::uint8_t, 800>& registry::cache_header(unsigned index) const
	{
		return caches_.at(index).header;
	}
	bool registry::cache_matches(unsigned index) const
	{
		return index < caches_.size() && matches(caches_[index].file);
	}
	std::span<const char* const> registry::cache_packages(std::string_view zone) const
	{
		const auto index = find(std::string(zone) + ".ff");
		return index >= 0 ? bundles_[files_[index].owner].packages : std::span<const char* const>{};
	}
	std::span<const char* const> registry::before(std::string_view zone) const
	{
		for (const auto& bundle : bundles_)
		{
			if (zone == bundle.zone) return bundle.dependencies;
		}
		return {};
	}
}
