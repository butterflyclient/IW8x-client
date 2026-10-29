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
#include <intrin.h>
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
		// Original signed Replay F64CE0 expands one native map descriptor into
		// server/localized/main FF requests. Its internal owned pending-row
		// producer is RVA F65870, called at F64F6C/F64FAF/F64FED/F65026/F65042.
		// DB_LoadFastfiles does not receive "srv_mp_backlot2" directly: it
		// receives "mp_backlot2". Hooking only the upper batch therefore misses
		// legitimate stock server-zone loads. This is the genuine engine-thread
		// admission boundary, not the seven-argument DB worker loader.
		constexpr uintptr_t kNativeEnqueueRva = 0xF65870;
		constexpr uintptr_t kNativePendingCountRva = 0xB991000;
		constexpr uint8_t kNativeEnqueueBytes[] = {
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00
		};

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
		using NativeEnqueueFn = void(__fastcall*)(const char*, const char*, uint32_t, uint8_t, uint32_t, uint32_t);
		std::atomic<LoadFastfilesFn> g_original{ nullptr };
		std::atomic<NativeEnqueueFn> g_native_enqueue_original{ nullptr };
		std::atomic<bool> g_loaded{ false };
		std::atomic<bool> g_xmas_srv_poc_scheduled{ false };
		std::atomic<unsigned> g_xmas_srv_native_batches_observed{ 0 };
		std::atomic<unsigned> g_xmas_srv_native_enqueue_observations{ 0 };
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

		bool xmas_srv_poc_native_load_requested()
		{
			if (!night_files::ChristmasEmptySrvPoC()) return false;
			char enabled[8]{};
			return GetEnvironmentVariableA("IW8X_XMAS_EMPTY_SRV_LOAD", enabled,
				DWORD(std::size(enabled))) == 1 && enabled[0] == '1';
		}

		bool authentic_native_srv_companion(std::string_view base, std::string_view filename) noexcept
		{
			// Native engine passes two separate C strings (each copied into an
			// owned 64-byte row). The derived server name must *exactly*
			// match the base map, not just contain an arbitrary srv_ prefix.
			// Refuse path separators, traversal and punctuation even if a
			// future source-provided map name has a matching srv_ alias.
			const auto safe = [](std::string_view name)
			{
				return std::all_of(name.begin(), name.end(), [](char c)
				{
					return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						(c >= '0' && c <= '9') || c == '_';
				});
			};
			return base.starts_with("mp_") && base.size() >= 4 && base.size() < 60 &&
				filename.size() == base.size() + 4 && filename.starts_with("srv_") &&
				filename.substr(4) == base && safe(base);
		}

		bool xmas_native_enqueue_eligible(std::string_view base, std::string_view actual,
			uint64_t currentPendingRows, bool explicitlyEnabled, bool alreadyScheduled) noexcept
		{
			// A future genuine Christmas map load must never enqueue its own
			// server zone twice. The smoke-test applies only to another
			// authenticated stock MP base/srv pair, with a spare native slot.
			return explicitlyEnabled && !alreadyScheduled && currentPendingRows < 0x788 &&
				base != "mp_jup_shipment_xmas" && actual != "srv_mp_jup_shipment_xmas" &&
				authentic_native_srv_companion(base, actual);
		}

		void __fastcall native_pending_enqueue(const char* baseName, const char* actualFilename,
			uint32_t flags, uint8_t allowMissing, uint32_t failureMode, uint32_t priority)
		{
			const auto original = g_native_enqueue_original.load(std::memory_order_acquire);
			// Always forward the native original first. The original owns the
			// DB pending row and canonical string copies; do not reimplement
			// those allocators, indexes, authentication, or queue locks.
			original(baseName, actualFilename, flags, allowMissing, failureMode, priority);
			if (!night_files::ChristmasEmptySrvPoC()) return;

			char base[64]{}, actual[64]{};
			if (!safemem::ReadString(baseName, base, sizeof(base)) ||
				!safemem::ReadString(actualFilename, actual, sizeof(actual))) return;
			if (!authentic_native_srv_companion(base, actual)) return;

			const auto count = g_xmas_srv_native_enqueue_observations.fetch_add(1, std::memory_order_relaxed);
			if (count < 128)
				LOG_INFO("XmasSrvPoC", "V48 NATIVE pending server-zone pair base=%s actual=%s flags=0x%X failure=%u priority=%u allowMissing=%u",
					base, actual, flags, failureMode, priority, unsigned(allowMissing));

			const bool requested = xmas_srv_poc_native_load_requested();
			if (!requested || g_xmas_srv_poc_scheduled.load(std::memory_order_acquire) ||
				std::string_view(base) == "mp_jup_shipment_xmas") return;
			const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
			std::uint64_t queued = 0;
			if (!imageBase ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(imageBase + kNativePendingCountRva),
					&queued, sizeof(queued)))
			{
				LOG_WARN("XmasSrvPoC", "native Christmas srv pending-row insertion declined: original DB count unreadable");
				return;
			}
			if (!xmas_native_enqueue_eligible(base, actual, queued, requested,
				g_xmas_srv_poc_scheduled.load(std::memory_order_acquire)))
			{
				if (queued >= 0x788)
					LOG_WARN("XmasSrvPoC", "native Christmas srv pending-row insertion declined: original DB queue full (%llu)",
						static_cast<unsigned long long>(queued));
				return;
			}
			bool expected = false;
			if (!g_xmas_srv_poc_scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				return;

			// Both local names live for this synchronous call. Signed native
			// F65870 copies them into two owned 64B pending-row fields before
			// returning. Same-native-lock/same-batch/validated descriptor
			// flags as the real srv_mp_* row, before F64CE0 unlocks at F65060.
			constexpr char christmasBase[] = "mp_jup_shipment_xmas";
			constexpr char christmasActual[] = "srv_mp_jup_shipment_xmas";
			LOG_INFO("XmasSrvPoC", "V48 NATIVE scheduling empty srv_mp_jup_shipment_xmas after %s: original srv flags=0x%X failure=%u priority=%u allowMissing=%u queued=%llu",
				actual, flags, failureMode, priority, unsigned(allowMissing),
				static_cast<unsigned long long>(queued));
			original(christmasBase, christmasActual, flags, allowMissing, failureMode, priority);
		}

		uint64_t __fastcall load_fastfiles(const ZoneInfo* zones, unsigned count, uint8_t mode, uint8_t allowMissing)
		{
			auto original = g_original.load(std::memory_order_acquire);
			// Independent native V48 gate: the signed Replay batch entrypoint
			// is hook-installed at RVA 0x11ACD30, but original stock server
			// companion loads could be queued through another native entry.
			// Emit a bounded observation BEFORE any copied descriptor guards.
			// Absence of a matching srv_* row is not evidence of a failed
			// Christmas fastfile: distinguish high scheduler from DB worker.
			const bool christmasRegistryReady = night_files::ChristmasEmptySrvPoC();
			bool boundedHighTrace = false;
			if (christmasRegistryReady)
			{
				const unsigned ordinal = g_xmas_srv_native_batches_observed.fetch_add(1, std::memory_order_relaxed);
				boundedHighTrace = ordinal < 256;
				if (boundedHighTrace)
				{
					const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
					const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
					LOG_INFO("XmasSrvPoC", "V48 native DB_LoadFastfiles HIGH entry ordinal=%u caller_RVA=0x%llX count=%u mode=%u allowMissing=%u native_zones=%p",
						ordinal, static_cast<unsigned long long>(caller >= base ? caller - base : 0),
						count, unsigned(mode), unsigned(allowMissing), zones);
				}
			}
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
				{
					if (boundedHighTrace)
						LOG_WARN("XmasSrvPoC", "V48 native HIGH descriptor could not be read, index=%u count=%u; untouched original forwarding", i, count);
					return original(zones, count, mode, allowMissing);
				}
				requested.push_back(zone);
				names.emplace_back(name);
				if (boundedHighTrace && i < 128)
					LOG_INFO("XmasSrvPoC", "V48 native HIGH descriptor index=%u zone=%s flags=0x%X failure=%u priority=%u reserved=%u",
						i, name, zone.flags, zone.failureMode, zone.priority, zone.reserved);
			}
			std::vector<std::string_view> inserted;
			const bool xmasSrvRequested = xmas_srv_poc_native_load_requested();
			const bool xmasRegistryReady = night_files::ChristmasEmptySrvPoC();
			const bool xmasAlreadyInBatch = std::find(names.begin(), names.end(), "srv_mp_jup_shipment_xmas") != names.end();
			bool xmasScheduled = false;
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
				if (xmasRegistryReady && names[i].starts_with("srv_"))
				{
					const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
					const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
					LOG_INFO("XmasSrvPoC", "OBSERVED native DB_LoadFastfiles srv zone=%s caller_RVA=0x%llX flags=0x%X failure=%u priority=%u reserved=%u batch_count=%u mode=%u allowMissing=%u",
						names[i].c_str(), static_cast<unsigned long long>(caller >= base ? caller - base : 0),
						zone.flags, zone.failureMode, zone.priority, zone.reserved,
						count, unsigned(mode), unsigned(allowMissing));
				}
				if (const auto replacement = night_files::NativeSharedZone(names[i]))
				{
					zone.name = replacement;
					native_br_substitution = true;
				}
				dependencies(zone, night_files::Before(names[i]));
				expanded.push_back(zone);
				dependencies(zone, night_files::After(names[i]));
				dependencies(zone, weapon_files::After(names[i]));
				// A separate, explicit PoC switch schedules the empty Christmas
				// srv on the *engine's own* DB_LoadFastfiles thread, cloning the
				// exact flags/lifetime of a real native server-side companion.
				// If no genuine srv_* request occurs, do not invent a phase or
				// call DB_LoadFastfile from a detached thread.
				if (xmasSrvRequested && !g_native_enqueue_original.load(std::memory_order_acquire) &&
					!xmasAlreadyInBatch && !g_xmas_srv_poc_scheduled.load(std::memory_order_acquire) &&
					names[i].starts_with("srv_") && names[i] != "srv_mp_jup_shipment_xmas")
				{
					bool expected = false;
					if (g_xmas_srv_poc_scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
					{
						ZoneInfo probe = zone;
						probe.name = "srv_mp_jup_shipment_xmas";
						expanded.push_back(probe);
						xmasScheduled = true;
						LOG_INFO("XmasSrvPoC", "native DB_LoadFastfiles scheduling empty srv_mp_jup_shipment_xmas after native %s: flags=0x%X failure=%u priority=%u reserved=%u mode=%u allowMissing=%u",
							names[i].c_str(), zone.flags, zone.failureMode, zone.priority, zone.reserved,
							unsigned(mode), unsigned(allowMissing));
					}
				}
				if (g_enabled && !found && names[i] == "code_post_gfx")
				{
					ZoneInfo art = zone;
					art.name = kZone;
					artFlags = art.flags;
					expanded.push_back(art);
					found = true;
				}
			}
			if (!found && inserted.empty() && !native_br_substitution && !xmasScheduled)
				return original(zones, count, mode, allowMissing);
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
			if (g_original.load(std::memory_order_acquire))
				return night_files::ChristmasEmptySrvPoC() && !g_native_enqueue_original.load(std::memory_order_acquire)
					? hook::Status::Failed : hook::Status::Installed;
			hook::Batch batch;
			batch.Add(reinterpret_cast<void*>(moduleBase + kLoadFastfilesRva), &load_fastfiles, kLoadFastfilesBytes,
				sizeof(kLoadFastfilesBytes), g_original);
			if (night_files::ChristmasEmptySrvPoC())
				batch.Add(reinterpret_cast<void*>(moduleBase + kNativeEnqueueRva), &native_pending_enqueue,
					kNativeEnqueueBytes, sizeof(kNativeEnqueueBytes), g_native_enqueue_original);
			const auto result = batch.Commit();
			if (result == hook::Status::Installed && night_files::ChristmasEmptySrvPoC())
				LOG_INFO("XmasSrvPoC", "V48 signed Replay F65870 native server-companion pending-row observer installed; armed=%u",
					unsigned(xmas_srv_poc_native_load_requested()));
			return result;
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
