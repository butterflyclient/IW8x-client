#include "diagnostics/weapons/weapon_files.h"
#include "common/console.h"
#include "engine/image_identity.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
namespace weapon_files
{
	namespace
	{
		struct File
		{
			const char* name;
			std::uint64_t size;
			const char* sha;
		};
#include "diagnostics/weapons/weapon_files.inc"
		struct Resident
		{
			const File* spec{};
			std::filesystem::path path;
			std::array<std::uint8_t, 136> header{};
			bool fastfile = false;
		};

		bool enabled = false;
		std::vector<Resident> files;
		std::array<std::array<std::uint8_t, 800>, std::size(kCaches)> cacheHeaders{};
		std::array<unsigned, std::size(kCaches)> cacheFiles{};

		int lookup(std::string_view name)
		{
			const auto it = std::lower_bound(files.begin(), files.end(), name,
				[](const Resident& r, std::string_view n)
				{
					return r.spec->name < n;
				});
			return it != files.end() && name == it->spec->name ? int(it - files.begin()) : -1;
		}

		bool matches(unsigned i)
		{
			if (i >= files.size()) return false;
			std::error_code error;
			return std::filesystem::file_size(files[i].path, error) == files[i].spec->size && !error &&
				image_identity::Digest(files[i].path.c_str()) == files[i].spec->sha;
		}
	}

	bool Initialize(const std::filesystem::path& game, const std::filesystem::path& config)
	{
		enabled = false;
		files.clear();
		if (GetPrivateProfileIntW(L"ports", L"weapon_pilots", 0, config.c_str()) != 1) return true;
		if (GetPrivateProfileIntW(L"ports", L"weapon_pilots_development", 0, config.c_str()) != 1)
		{
			LOG_ERR("WeaponFiles", "Weapon candidates require the isolated development switch");
			return false;
		}
		const auto root = game / L"magma" / L"weapons" / L"pilots120";
		for (const auto& spec : kFiles)
			files.push_back({ &spec, root / spec.name });
		std::sort(files.begin(), files.end(),
			[](const auto& a, const auto& b)
			{
				return std::string_view(a.spec->name) < b.spec->name;
			});
		for (unsigned i = 0; i < files.size(); ++i)
		{
			if (i && std::string_view(files[i - 1].spec->name) >= files[i].spec->name) return false;
			auto& r = files[i];
			if (!matches(i))
			{
				LOG_ERR("WeaponFiles", "missing or changed candidate: %ls", r.path.c_str());
				return false;
			}
			r.fastfile = std::string_view(r.spec->name).ends_with(".ff");
			if (r.fastfile)
			{
				std::ifstream f(r.path, std::ios::binary);
				if (!f.read(reinterpret_cast<char*>(r.header.data()), r.header.size()) || std::memcmp(r.header.data(), "IWffc100", 8))
					return false;
			}
		}
		for (unsigned i = 0; i < std::size(kCaches); ++i)
		{
			const int at = lookup(std::string(kCaches[i]) + ".xpak");
			if (at < 0) return false;
			cacheFiles[i] = unsigned(at);
			std::ifstream f(files[at].path, std::ios::binary);
			if (!f.read(reinterpret_cast<char*>(cacheHeaders[i].data()), cacheHeaders[i].size())) return false;
		}
		enabled = true;
		LOG_INFO("WeaponFiles", "verified isolated weapon candidate: %u files, %u resident zones; gameplay unverified",
			unsigned(files.size()), unsigned(std::size(kZones)));
		return true;
	}

	bool Enabled()
	{
		return enabled;
	}

	int Find(std::string_view name)
	{
		return enabled ? lookup(name) : -1;
	}

	const std::filesystem::path& Path(unsigned i)
	{
		return files.at(i).path;
	}

	const std::array<std::uint8_t, 136>* Header(unsigned i)
	{
		return i < files.size() && files[i].fastfile ? &files[i].header : nullptr;
	}

	int FindCache(std::string_view name)
	{
		if (enabled)
			for (unsigned i = 0; i < std::size(kCaches); ++i)
				if (name == kCaches[i]) return int(i);
		return -1;
	}

	const std::array<std::uint8_t, 800>& CacheHeader(unsigned i)
	{
		return cacheHeaders.at(i);
	}

	bool CacheMatches(unsigned i)
	{
		return enabled && i < cacheFiles.size() && matches(cacheFiles[i]);
	}

	std::span<const char* const> CachePackages()
	{
		return enabled ? std::span<const char* const>(kCaches) : std::span<const char* const>();
	}

	bool OwnsZone(std::string_view name)
	{
		return enabled && lookup(std::string(name) + ".ff") >= 0;
	}

	std::span<const char* const> After(std::string_view name)
	{
		// Preserve the original resident weapon zone's lifetime and request flags.
		return enabled && name == "global_stream_mp" ? std::span<const char* const>(kZones) : std::span<const char* const>();
	}
}
