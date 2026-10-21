#include "client/frontend/menu_art1645.h"
#include "common/console.h"
#include "engine/image_identity.h"
#include "engine/safemem.h"
#include "diagnostics/night/night_files.h"
#include "diagnostics/weapons/weapon_files.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>
namespace menu_art1645
{
	namespace
	{
		constexpr uint64_t kZoneSize = 4485756;
		constexpr char kZoneSha[] = "cfccfb8259ea7dc599492b48956206fdd2e5901892a8c2d3cb2f5dbfcc067f47";
		constexpr uint64_t kCacheSize = 29818880;
		constexpr char kCacheSha[] = "9ad5f66c00659aad0d1cb565c337864870823aea0abd4efbde3a1842779e77c6";
		constexpr uint64_t kCompanionSize = 172;
		constexpr char kCompanionSha[] = "a55c2d642f3ea94c67c9851bbdae7168f54d616db8ec732f5bddb4a9117afe70";
		constexpr uintptr_t kLoadFastfilesRva = 0x11ACD30;
		constexpr uint8_t kLoadFastfilesBytes[] = { 0x40, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x89, 0x74,
			0x24, 0x58 };

		struct ZoneInfo
		{
			const char* name;
			uint32_t flags;
			uint32_t failureMode;
			uint32_t priority;
			uint32_t reserved;
		};

		static_assert(sizeof(ZoneInfo) == 24);
		using LoadFastfilesFn = uint64_t(__fastcall*)(const ZoneInfo*, unsigned, uint8_t, uint8_t);
		std::atomic<LoadFastfilesFn> g_original{ nullptr };
		std::atomic<bool> g_loaded{ false };
		bool g_enabled = false;
		std::filesystem::path g_zonePath, g_cachePath;
		std::array<std::filesystem::path, 3> g_companionPaths;
		std::array<uint8_t, 136> g_zoneHeader{};
		std::array<uint8_t, 136> g_companionHeader{};
		std::array<uint8_t, 800> g_cacheHeader{};

		struct VideoFile
		{
			const char* name;
			uint64_t size;
			const char* sha;
		};

		constexpr VideoFile kVideos[] = {
#include "client/frontend/menu_videos1645_files.inc"
		};
		std::array<std::filesystem::path, std::size(kVideos)> g_videoPaths;
		std::array<int, std::size(kVideos)> g_videoChecks{};
		std::mutex g_videoMutex;

		bool matches(const std::filesystem::path& path, uint64_t size, const char* sha)
		{
			std::error_code error;
			return std::filesystem::file_size(path, error) == size && !error && image_identity::Digest(path.c_str()) == sha;
		}

		uint64_t __fastcall load_fastfiles(const ZoneInfo* zones, unsigned count, uint8_t mode, uint8_t allowMissing)
		{
			auto original = g_original.load(std::memory_order_acquire);
			if ((!g_enabled && !night_files::Enabled() && !weapon_files::Enabled()) || !zones || !count || count > 256)
				return original(zones, count, mode, allowMissing);
			std::vector<ZoneInfo> expanded;
			expanded.reserve(count + 5);
			// Read the complete request before changing its order. Shared dependency
			// names remain static, and flags/lifetime/priority come from their owner.
			std::vector<ZoneInfo> requested;
			std::vector<std::string> names;
			requested.reserve(count);
			names.reserve(count);
			for (unsigned i = 0; i < count; ++i)
			{
				ZoneInfo zone{};
				char name[128]{};
				if (!safemem::ReadBytes(zones + i, &zone, sizeof(zone)) || !safemem::ReadString(zone.name, name, sizeof(name)))
					return original(zones, count, mode, allowMissing);
				requested.push_back(zone);
				names.emplace_back(name);
			}
			std::vector<std::string_view> inserted;
			auto dependencies = [&](const ZoneInfo& owner, std::span<const char* const> list)
			{
				for (const char* name : list)
				{
					if (std::find(names.begin(), names.end(), name) != names.end() ||
						std::find(inserted.begin(), inserted.end(), name) != inserted.end())
						continue;
					auto dependency = owner;
					dependency.name = name;
					expanded.push_back(dependency);
					inserted.emplace_back(name);
				}
			};
			bool found = false;
			bool native_br_substitution = false;
			uint32_t artFlags = 0;
			for (unsigned i = 0; i < count; ++i)
			{
				auto zone = requested[i];
				if (const auto replacement = night_files::NativeSharedZone(names[i]))
				{
					zone.name = replacement;
					native_br_substitution = true;
				}
				dependencies(zone, night_files::Before(names[i]));
				expanded.push_back(zone);
				dependencies(zone, night_files::After(names[i]));
				dependencies(zone, weapon_files::After(names[i]));
				if (g_enabled && !found && names[i] == "code_post_gfx")
				{
					ZoneInfo art = zone;
					art.name = kZone;
					artFlags = art.flags;
					expanded.push_back(art);
					found = true;
				}
			}
			if (!found && inserted.empty() && !native_br_substitution) return original(zones, count, mode, allowMissing);
			if (native_br_substitution)
				LOG_INFO("NightFiles", "substituted native common_br_mp in Island dedicated BR shared load (mode=%u)", mode);
			if (found)
				LOG_INFO("MenuArt1645", "scheduled original 1.64.5 image zone after code_post_gfx (count=%u flags=0x%X mode=%u)", count,
					artFlags, mode);
			if (!inserted.empty())
				LOG_INFO(
					"NightFiles", "scheduled %u dependency zones before native map loading (mode=%u)", unsigned(inserted.size()), mode);
			// Replay copies each 24-byte zone descriptor into its pending queue in
			// DB_LoadFastfiles before returning. The name points at static storage.
			return original(expanded.data(), static_cast<unsigned>(expanded.size()), mode, allowMissing);
		}
	}

	bool Initialize(const std::filesystem::path& gameRoot, const std::filesystem::path& config)
	{
		g_enabled = false;
		g_loaded.store(false, std::memory_order_release);
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
		return true;
#else
		wchar_t selected[32]{};
		GetPrivateProfileStringW(L"ui", L"main_menu_ui", L"1.20", selected, 32, config.c_str());
		if (_wcsicmp(selected, L"1.64.5") != 0) return true;
		const auto root = gameRoot / L"magma" / L"ui" / L"1.64.5";
		g_zonePath = root / kZoneSourceFile;
		g_cachePath = root / kCacheFile;
		g_companionPaths = { root / kWorldwideFile, root / kEnglishFile, root / kTechsetsFile };
		g_videoChecks.fill(0);
		for (size_t i = 0; i < std::size(kVideos); ++i)
			g_videoPaths[i] = root / L"video" / kVideos[i].name;
		if (!matches(g_zonePath, kZoneSize, kZoneSha) || !matches(g_cachePath, kCacheSize, kCacheSha))
		{
			LOG_ERR("MenuArt1645", "selected 1.64.5 image zone/cache missing or changed; keeping TU19 menus");
			return true;
		}
		for (const auto& path : g_companionPaths)
		{
			if (!matches(path, kCompanionSize, kCompanionSha))
			{
				LOG_ERR("MenuArt1645", "native companion missing or changed: %ls", path.c_str());
				return true;
			}
		}
		std::ifstream zone(g_zonePath, std::ios::binary), cache(g_cachePath, std::ios::binary);
		std::ifstream companion(g_companionPaths[0], std::ios::binary);
		if (!zone.read(reinterpret_cast<char*>(g_zoneHeader.data()), g_zoneHeader.size()) ||
			!companion.read(reinterpret_cast<char*>(g_companionHeader.data()), g_companionHeader.size()) ||
			!cache.read(reinterpret_cast<char*>(g_cacheHeader.data()), g_cacheHeader.size()) ||
			std::memcmp(g_zoneHeader.data(), "IWffc100", 8) || std::memcmp(g_companionHeader.data(), "IWffc100", 8))
		{
			LOG_ERR("MenuArt1645", "selected 1.64.5 package headers invalid");
			return true;
		}
		g_enabled = true;
		LOG_INFO("MenuArt1645", "verified 145 original images, one sprite table, 423 original labels and 66 stream objects");
		return true;
#endif
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (!g_enabled && !night_files::Enabled() && !weapon_files::Enabled()) return hook::Status::Installed;
		if (g_original.load(std::memory_order_acquire)) return hook::Status::Installed;
		return hook::Install(reinterpret_cast<void*>(moduleBase + kLoadFastfilesRva), &load_fastfiles, kLoadFastfilesBytes,
			sizeof(kLoadFastfilesBytes), g_original);
	}

	bool Enabled()
	{
		return g_enabled;
	}

	bool Loaded()
	{
		return g_loaded.load(std::memory_order_acquire);
	}

	void ObserveLoad(bool loaded)
	{
		g_loaded.store(loaded, std::memory_order_release);
	}

	const std::filesystem::path& ZonePath()
	{
		return g_zonePath;
	}

	const std::filesystem::path& CachePath()
	{
		return g_cachePath;
	}

	const std::filesystem::path& CompanionPath(unsigned index)
	{
		return g_companionPaths.at(index);
	}

	const std::array<uint8_t, 136>& ZoneHeader()
	{
		return g_zoneHeader;
	}

	const std::array<uint8_t, 136>& CompanionHeader()
	{
		return g_companionHeader;
	}

	const std::array<uint8_t, 800>& CacheHeader()
	{
		return g_cacheHeader;
	}

	bool CacheMatches()
	{
		return g_enabled && matches(g_cachePath, kCacheSize, kCacheSha);
	}

	int FindVideo(std::string_view name)
	{
		if (!g_enabled) return -1;
		// Native file callers may retain their video/ prefix. Only the exact
		// compiled catalog is redirected; no general loose-file substitution.
		if (name.starts_with("video/") || name.starts_with("video\\")) name.remove_prefix(6);
		for (size_t i = 0; i < std::size(kVideos); ++i)
		{
			if (name != kVideos[i].name) continue;
			std::lock_guard lock(g_videoMutex);
			if (!g_videoChecks[i])
			{
				g_videoChecks[i] = matches(g_videoPaths[i], kVideos[i].size, kVideos[i].sha) ? 1 : -1;
				LOG_INFO("MenuArt1645", "original title video %s verified=%d", kVideos[i].name, g_videoChecks[i] == 1);
			}
			return g_videoChecks[i] == 1 ? static_cast<int>(i) : -1;
		}
		return -1;
	}

	const std::filesystem::path& VideoPath(unsigned index)
	{
		return g_videoPaths.at(index);
	}
}
