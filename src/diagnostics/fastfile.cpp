#include "diagnostics/fastfile.h"

#include "engine/addresses.hpp"

#include "common/console.h"

#include "engine/safemem.h"

#include "dedicated/dedicated.h"

#include "engine/image_identity.h"

#include "client/frontend/menu_art1645.h"
#include "diagnostics/night/night_files.h"
#include "diagnostics/weapons/weapon_files.h"

#include "common/deployment_profile.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
namespace fastfile
{
	namespace
	{
		// Replay IDA: DB_LoadFastfile/DB_LoadXFile, RVA 0x11a79f0; seven x64 arguments.
		// Logging observes actual resident loads, including resume/cancel, without
		// changing loader flags, priorities, collision policy or the native result.
		constexpr uintptr_t kLoadRva = 0x11a79f0;
		constexpr uint8_t kLoadBytes[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x07,
			0x00, 0x00 };
		using LoadFn = unsigned(__fastcall*)(const char*, void*, void*, unsigned, bool, int, void*);
		std::atomic<LoadFn> g_original{ nullptr };
		using OpenFn = void* (*)(void*, unsigned, const char*, unsigned);
		using ExistsFn = bool (*)(void*, unsigned, const char*);
		using LooseFn = HANDLE (*)(unsigned, const char*, unsigned);
		using OpenPathFn = HANDLE (*)(const char*, unsigned);
		using PackagesFn = bool (*)(const char*);
		using FindPackFn = unsigned (*)(const char*);
		using AddPackFn = void (*)(unsigned, const char*);
		using VerifyPackFn = bool (*)(const void*, const void*);
		using VerifySignatureFn = bool (*)(const void*, const void*);
		std::atomic<OpenFn> g_open{ nullptr };
		std::atomic<ExistsFn> g_exists{ nullptr };
		std::atomic<LooseFn> g_loose{ nullptr };
		std::atomic<OpenPathFn> g_openPath{ nullptr };
		std::atomic<PackagesFn> g_packages{ nullptr };
		std::atomic<FindPackFn> g_findPack{ nullptr };
		std::atomic<AddPackFn> g_addPack{ nullptr };
		std::atomic<VerifyPackFn> g_verifyPack{ nullptr };
		std::atomic<VerifySignatureFn> g_verifySignature{ nullptr };
		thread_local bool g_verifyingCustomCache = false;
		thread_local bool g_verifyingArtCache = false;
		thread_local int g_verifyingNightCache = -1;
		thread_local int g_verifyingWeaponCache = -1;
		using InitFn = void (*)(void*);
		std::atomic<InitFn> g_init{ nullptr };
		uintptr_t g_base{};

		struct SceneFile
		{
			const char* name;
			uint64_t size;
			const char* sha;
		};

#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
		constexpr SceneFile kFiles116[] = {
#include "client/frontend/frontend116_files.inc"
		};
#if __has_include("frontend124_files.inc")
		constexpr SceneFile kFiles124[] = {
#include "client/frontend/frontend124_files.inc"
		};
		static_assert(std::size(kFiles124) == 4);
#endif
#if __has_include("frontend128_test_files.inc")
		// The test manifest is deliberately compiled into the admission gate.
		constexpr SceneFile kFiles128Test[] = {
#include "client/frontend/frontend128_test_files.inc"
		};
		static_assert(std::size(kFiles128Test) == 4);
#endif
		constexpr SceneFile kFiles128Halloween[] = {
#include "client/frontend/frontend128_halloween_files.inc"
		};
		static_assert(std::size(kFiles128Halloween) == 4);
		static_assert(std::size(kFiles116) == 4);
		constexpr SceneFile kCache124{ "cache_magma_frontend124.xpak", 13434880ULL,
			"324afc68a414173912b431895eee7a0144c74eb314b781996556f3e897a012f1" };
		constexpr char kCachePackage124[] = "cache_magma_frontend124";
		constexpr SceneFile kCache128Test{ "cache_magma_frontend128.xpak", 101056512ULL,
			"b3ae13d0b2f286f4b15a1637d6ba530edd3aa575b8631365d55ab8e6204fd0df" };
		constexpr char kCachePackage128Test[] = "cache_magma_frontend128";
		const SceneFile* g_files = kFiles116;
		const wchar_t* g_folder = L"mp_frontend2";
#else
		constexpr SceneFile kCache124{}, kCache128Test{};
		constexpr char kCachePackage124[] = "", kCachePackage128Test[] = "";
		const SceneFile* g_files = nullptr;
		const wchar_t* g_folder = L"";
#endif
		std::array<std::filesystem::path, 4> g_paths;
		std::filesystem::path g_cachePath;
		std::array<uint8_t, 800> g_cacheHeader{};
		bool g_scene124 = false;
		bool g_scene128Test = false;
		bool g_cacheReady = false;

		bool CacheFileMatches()
		{
			std::error_code error;
			const auto& expected = g_scene128Test ? kCache128Test : kCache124;
			return std::filesystem::file_size(g_cachePath, error) == expected.size && !error &&
				image_identity::Digest(g_cachePath.c_str()) == expected.sha;
		}

		using CacheCheckFn = bool (*)();
		CacheCheckFn g_cacheCheck = CacheFileMatches;
		std::array<std::array<uint8_t, 136>, 4> g_headers;
		constexpr std::string_view kAlias = "__magma120_converted_";
		constexpr int kNightFileBase = 10000;
		constexpr int kWeaponFileBase = 20000;

#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
		bool Select(std::wstring_view scene)
		{
			g_scene124 = false;
			g_scene128Test = false;
			g_cacheReady = false;
			g_cachePath.clear();
			g_cacheHeader.fill(0);
			if (scene == L"1.16")
			{
				g_files = kFiles116;
				g_folder = L"mp_frontend2";
				return true;
			}
			if (scene == L"1.24")
			{
#if __has_include("frontend124_files.inc")
				g_files = kFiles124;
				g_folder = L"mp_frontend5";
				g_scene124 = true;
				return true;
#else
				LOG_ERR("FrontendFiles120", "1.24 scene selected but its converted package is not built");
				return false;
#endif
			}
			if (scene == L"1.28-test")
			{
#if __has_include("frontend128_test_files.inc")
				g_files = kFiles128Test;
				g_folder = L"mp_frontend6";
				g_scene128Test = true;
				return true;
#else
				LOG_ERR("FrontendFiles120", "1.28 test scene selected but its converted package is not built");
				return false;
#endif
			}
			if (scene == L"1.28-halloween")
			{
				g_files = kFiles128Halloween;
				g_folder = L"mp_frontend6b";
				g_scene128Test = true;
				return true;
			}
			if (scene == L"stock" || scene == L"1.20")
			{
				g_files = nullptr;
				return true;
			}
			LOG_ERR("FrontendFiles120", "unknown frontend scene selection");
			return false;
		}
#endif

		int Find(std::string_view name)
		{
			const int weapon = weapon_files::Find(name);
			if (weapon >= 0) return kWeaponFileBase + weapon;
			const int night = night_files::Find(name);
			if (night >= 0) return kNightFileBase + night;
			if (menu_art1645::Enabled())
			{
				if (name == menu_art1645::kZoneFile) return 5;
				if (name == menu_art1645::kCacheFile) return 6;
				if (name == menu_art1645::kWorldwideFile) return 7;
				if (name == menu_art1645::kEnglishFile) return 8;
				if (name == menu_art1645::kTechsetsFile) return 9;
				const int video = menu_art1645::FindVideo(name);
				if (video >= 0) return 10 + video;
			}
			if (!g_files) return -1;
			for (size_t i = 0; i < g_paths.size(); ++i)
				if (name == g_files[i].name) return int(i);
			if (g_cacheReady && ((g_scene124 && name == kCache124.name) || (g_scene128Test && name == kCache128Test.name)))
				return int(g_paths.size());
			return -1;
		}

		std::filesystem::path PathFor(int index)
		{
			if (index >= kWeaponFileBase) return weapon_files::Path(unsigned(index - kWeaponFileBase));
			if (index >= kNightFileBase) return night_files::Path(unsigned(index - kNightFileBase));
			if (index == 5) return menu_art1645::ZonePath();
			if (index == 6) return menu_art1645::CachePath();
			if (index >= 7 && index <= 9) return menu_art1645::CompanionPath(index - 7);
			if (index >= 10) return menu_art1645::VideoPath(index - 10);
			return index == int(g_paths.size()) ? g_cachePath : g_paths[static_cast<size_t>(index)];
		}

		std::string Name(const char* name)
		{
			char buffer[128]{};
			if (!safemem::ReadString(name, buffer, sizeof(buffer))) return {};
			return buffer;
		}

		bool Patch(std::string_view name)
		{
			if (!name.ends_with(".fp")) return false;
			return Find(std::string(name.substr(0, name.size() - 3)) + ".ff") >= 0;
		}

		void* Open(void* self, unsigned folder, const char* name, unsigned flags)
		{
			const auto key = Name(name);
			if (Patch(key)) return nullptr; // Source deltas are already applied offline.
			if (Find(key) < 0) return g_open.load()(self, folder, name, flags);
			const std::string alias = std::string(kAlias) + key;
			auto* result = g_open.load()(self, folder, alias.c_str(), flags);
			LOG_INFO("FrontendFiles120", "converted open %s result=%p", key.c_str(), result);
			return result;
		}

		bool Exists(void* self, unsigned folder, const char* name)
		{
			const auto key = Name(name);
			if (Patch(key)) return false;
			return Find(key) >= 0 || g_exists.load()(self, folder, name);
		}

		HANDLE Loose(unsigned folder, const char* name, unsigned flags)
		{
			const auto key = Name(name);
			if (Patch(key)) return INVALID_HANDLE_VALUE;
			const bool alias = key.starts_with(kAlias);
			const int index = Find(alias ? std::string_view(key).substr(kAlias.size()) : std::string_view(key));
			if (index < 0) return alias ? INVALID_HANDLE_VALUE : g_loose.load()(folder, name, flags);
			// Replay's disk provider calls this directly with the original filename;
			// the CASC provider reaches it through our alias. Both use verified files.
			// CASC's loose worker needs synchronous handles. DiskFS stores these flags
			// at descriptor+0x130 and uses ReadFileEx (Replay RVA 0xf63110); preserve
			// its requested asynchronous/unbuffered mode on the actual HANDLE.
			const auto open_flags = alias ? flags & ~0x18u : flags;
			const auto path = PathFor(index).string();
			const auto opener = g_openPath.load(std::memory_order_acquire);
			const auto handle = opener ? opener(path.c_str(), open_flags) : INVALID_HANDLE_VALUE;
			LOG_INFO("FrontendFiles120", "loose %s flags=%X handle=%p", key.c_str(), open_flags, handle);
				if (night_files::ChristmasEmptySrvPoC() &&
					std::string_view(alias ? std::string_view(key).substr(kAlias.size()) : std::string_view(key)) ==
						"srv_mp_jup_shipment_xmas.ff")
					LOG_INFO("XmasSrvPoC", "native loose-file resolution: zone=srv_mp_jup_shipment_xmas path=%s flags=0x%X handle=%p",
						path.c_str(), open_flags, handle);
			return handle;
		}

		bool ConvertedDescriptor(const void* descriptor)
		{
			const auto key = Name(static_cast<const char*>(descriptor));
			const int index = Find(key);
			if (index < 0) return false;
			const std::array<uint8_t, 136>* expected = nullptr;
			if (index >= kWeaponFileBase)
				expected = weapon_files::Header(unsigned(index - kWeaponFileBase));
			else if (index >= kNightFileBase)
				expected = night_files::Header(unsigned(index - kNightFileBase));
			else if (index == 5)
				expected = &menu_art1645::ZoneHeader();
			else if (index >= 7 && index <= 9)
				expected = &menu_art1645::CompanionHeader();
			else if (index < int(g_headers.size()))
				expected = &g_headers[index];
			if (!expected) return false;
			std::array<uint8_t, 136> header{};
			uint8_t authenticated = 1;
			uint32_t handle = 0xfeffffffu;
			const auto* info = static_cast<const uint8_t*>(descriptor);
			return safemem::ReadBytes(info + 0x88, header.data(), header.size()) && header == *expected &&
				!memcmp(header.data(), "IWffc100", 8) && safemem::ReadBytes(info + 0x50, &authenticated, 1) && !authenticated &&
				safemem::ReadBytes(info + 0x40, &handle, 4) && handle != 0xfeffffffu;
		}

		void InitStream(void* state)
		{
			void* descriptor = nullptr;
			void* inflater = nullptr;
			uint32_t context = 0;
			if (safemem::ReadBytes(static_cast<uint8_t*>(state) + 0xb0, &descriptor, sizeof(descriptor)) &&
				ConvertedDescriptor(descriptor) && safemem::ReadBytes(state, &inflater, sizeof(inflater)) && inflater &&
				safemem::ReadBytes(static_cast<uint8_t*>(descriptor) + 0x54, &context, 4))
			{
				// Replay's header parser accepts IWffc100 and sets authenticated=false.
				// D871E0 nevertheless errors for that flag before calling D88120.
				// Use the latter's native unsigned initialization for only our exact,
				// SHA-verified offline files. No descriptor/authentication flag changes.
				reinterpret_cast<void (*)(void*, bool, void*, void*)>(g_base + 0xd88120)(
					inflater, false, descriptor, context ? static_cast<uint8_t*>(descriptor) + 0x54 : nullptr);
				LOG_INFO("FrontendFiles120", "native unsigned stream: %s", Name(static_cast<const char*>(descriptor)).c_str());
				return;
			}
			g_init.load()(state);
		}

		bool VerifySignature(const void* digest, const void* signature)
		{
			std::array<uint8_t, 32> hash{};
			std::array<uint8_t, 256> signedHash{};
			const auto* expected = g_verifyingWeaponCache >= 0 ? &weapon_files::CacheHeader(unsigned(g_verifyingWeaponCache))
				: g_verifyingNightCache >= 0 ? &night_files::CacheHeader(unsigned(g_verifyingNightCache))
				: g_verifyingArtCache ? &menu_art1645::CacheHeader()
				: &g_cacheHeader;
			if ((g_verifyingCustomCache || g_verifyingArtCache || g_verifyingNightCache >= 0 || g_verifyingWeaponCache >= 0) &&
				safemem::ReadBytes(digest, hash.data(), hash.size()) &&
				safemem::ReadBytes(signature, signedHash.data(), signedHash.size()) &&
				!memcmp(hash.data(), expected->data() + 24, hash.size()) &&
				!memcmp(signedHash.data(), expected->data() + 56, signedHash.size()))
			{
				// The native caller has already recomputed and matched this XPAK's
				// index hash. The package is ours, and its entire file was SHA-pinned
				// immediately before this native verification; only its RSA signature
				// is absent because the original game's private signing key is not ours.
				LOG_INFO("FrontendFiles120", "accepted verified custom cache signature");
				return true;
			}
			return g_verifySignature.load(std::memory_order_acquire)(digest, signature);
		}

		bool VerifyPack(const void* pack, const void* header)
		{
			const auto name = Name(static_cast<const char*>(pack));
			const bool sceneCache =
				g_cacheReady && ((g_scene124 && name == kCachePackage124) || (g_scene128Test && name == kCachePackage128Test));
			const bool menuCache = menu_art1645::Enabled() && name == menu_art1645::kCache;
			const int nightCache = night_files::FindCache(name);
			const int weaponCache = weapon_files::FindCache(name);
			if (!sceneCache && !menuCache && nightCache < 0 && weaponCache < 0)
				return g_verifyPack.load(std::memory_order_acquire)(pack, header);
			const auto& expected = weaponCache >= 0 ? weapon_files::CacheHeader(unsigned(weaponCache))
				: nightCache >= 0 ? night_files::CacheHeader(unsigned(nightCache))
				: menuCache ? menu_art1645::CacheHeader()
				: g_cacheHeader;
			std::array<uint8_t, 800> observed{};
			if (!safemem::ReadBytes(header, observed.data(), observed.size()) || observed != expected ||
				!(weaponCache >= 0 ? weapon_files::CacheMatches(unsigned(weaponCache))
					: nightCache >= 0 ? night_files::CacheMatches(unsigned(nightCache))
					: menuCache ? menu_art1645::CacheMatches()
					: g_cacheCheck()))
			{
				LOG_ERR("FrontendFiles120", "custom cache changed before native verification");
				return false;
			}

			struct Guard
			{
				bool previous = g_verifyingCustomCache;
				bool previousArt = g_verifyingArtCache;
				int previousNight = g_verifyingNightCache;
				int previousWeapon = g_verifyingWeaponCache;

				explicit Guard(bool art, int night, int weapon)
				{
					g_verifyingCustomCache = !art && night < 0 && weapon < 0;
					g_verifyingArtCache = art;
					g_verifyingNightCache = night;
					g_verifyingWeaponCache = weapon;
				}

				~Guard()
				{
					g_verifyingCustomCache = previous;
					g_verifyingArtCache = previousArt;
					g_verifyingNightCache = previousNight;
					g_verifyingWeaponCache = previousWeapon;
				}
			} guard(menuCache, nightCache, weaponCache);

			return g_verifyPack.load(std::memory_order_acquire)(pack, header);
		}

		bool LoadPackages(const char* zone)
		{
			const bool original = g_packages.load(std::memory_order_acquire)(zone);
			if (weapon_files::OwnsZone(Name(zone)))
			{
				const auto find = g_findPack.load(std::memory_order_acquire);
				const auto add = g_addPack.load(std::memory_order_acquire);
				if (!find || !add) return false;
				for (const auto* package : weapon_files::CachePackages())
				{
					auto index = find(package);
					if (index == 128)
					{
						add(5, package);
						index = find(package);
					}
					if (index == 128)
					{
						LOG_ERR("WeaponFiles", "native stream package registration failed: %s", package);
						return false;
					}
				}
				return true;
			}
			if (night_files::OwnsZone(Name(zone)))
			{
				const auto find = g_findPack.load(std::memory_order_acquire);
				const auto add = g_addPack.load(std::memory_order_acquire);
				if (!find || !add) return false;
				for (const auto* package : night_files::CachePackages(Name(zone)))
				{
					auto index = find(package);
					const bool already_registered = index != 128;
					if (index == 128)
					{
						add(5, package);
						index = find(package);
					}
					if (index == 128)
					{
						LOG_ERR("NightFiles", "native stream package registration failed: %s", package);
						return false;
					}
					if (night_files::MapEnabled("mp_sm_island_1") &&
						std::string_view(package).starts_with("cache_magma_island164"))
						LOG_INFO("IslandStream", "zone=%s cache=%s native_index=%u %s", zone, package,
							index, already_registered ? "reused" : "registered");
				}
				return true;
			}
			if (menu_art1645::Enabled() && Name(zone) == menu_art1645::kZone)
			{
				const auto find = g_findPack.load(std::memory_order_acquire);
				const auto add = g_addPack.load(std::memory_order_acquire);
				if (!find || !add) return false;
				auto index = find(menu_art1645::kCache);
				if (index == 128)
				{
					add(5, menu_art1645::kCache);
					index = find(menu_art1645::kCache);
				}
				LOG_INFO("MenuArt1645", "native stream package=%s index=%u", menu_art1645::kCache, index);
				return index != 128;
			}
			if (!g_cacheReady ||
				!((g_scene124 && Name(zone) == "mp_frontend5") ||
					(g_scene128Test && (Name(zone) == "mp_frontend6" || Name(zone) == "mp_frontend6b"))))
				return original;
			const char* package = g_scene128Test ? kCachePackage128Test : kCachePackage124;
			const auto find = g_findPack.load(std::memory_order_acquire);
			const auto add = g_addPack.load(std::memory_order_acquire);
			if (!find || !add) return false;
			auto index = find(package);
			if (index == 128)
			{
				// The native XPAK API appends .xpak and resolves folder 5. Our exact
				// cache file is routed through the native CASC/loose-file interface.
				add(5, package);
				index = find(package);
			}
			if (index == 128)
			{
				LOG_ERR("FrontendFiles120", "native stream package registration failed for %s", package);
				return false;
			}
			LOG_INFO("FrontendFiles120", "native stream package=%s index=%u", package, index);
			return true;
		}

		bool VerifyFiles()
		{
			wchar_t image[MAX_PATH]{};
			if (!GetModuleFileNameW(nullptr, image, MAX_PATH)) return false;
			const auto gameRoot = std::filesystem::path(image).parent_path();
			const auto config = gameRoot / deployment::kConfigFile;
			if (!night_files::Initialize(gameRoot, config)) return false;
			if (dedicated::Enabled()) return true;
			if (!menu_art1645::Initialize(gameRoot, config)) return false;
			if (!weapon_files::Initialize(gameRoot, config)) return false;
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
			LOG_INFO("FrontendFiles120", "stock frontend; Escape bundle loader available; other custom content compiled out");
			return true;
#else
			wchar_t scene[32]{};
			GetPrivateProfileStringW(L"ui", L"frontend_scene", L"1.16", scene, 32, config.c_str());
			if (!Select(scene)) return false;
			if (!g_files)
			{
				LOG_INFO("FrontendFiles120", "stock frontend selected");
				return true;
			}
			const auto root = gameRoot / L"magma" / L"frontends" / g_folder;
			for (size_t i = 0; i < g_paths.size(); ++i)
			{
				g_paths[i] = root / g_files[i].name;
				std::error_code error;
				if (std::filesystem::file_size(g_paths[i], error) != g_files[i].size || error ||
					image_identity::Digest(g_paths[i].c_str()) != g_files[i].sha)
				{
					LOG_ERR("FrontendFiles120", "missing or changed converted file: %ls", g_paths[i].c_str());
					return false;
				}
				std::ifstream stream(g_paths[i], std::ios::binary);
				if (!stream.read(reinterpret_cast<char*>(g_headers[i].data()), g_headers[i].size()) ||
					memcmp(g_headers[i].data(), "IWffc100", 8))
					return false;
			}
			if (g_scene124 || g_scene128Test)
			{
				const auto& expected = g_scene128Test ? kCache128Test : kCache124;
				g_cachePath = root / expected.name;
				std::error_code error;
				if (std::filesystem::file_size(g_cachePath, error) != expected.size || error ||
					image_identity::Digest(g_cachePath.c_str()) != expected.sha)
				{
					LOG_ERR("FrontendFiles120", "missing or changed selected stream cache: %ls", g_cachePath.c_str());
					return false;
				}
				std::ifstream stream(g_cachePath, std::ios::binary);
				if (!stream.read(reinterpret_cast<char*>(g_cacheHeader.data()), g_cacheHeader.size())) return false;
				g_cacheReady = true;
				LOG_INFO("FrontendFiles120", "verified selected stream cache: %ls", g_cachePath.c_str());
			}
			LOG_INFO("FrontendFiles120", "all four converted Replay fastfiles verified for scene %ls", scene);
			return true;
#endif
		}

		unsigned __fastcall Load(
			const char* name, void* zoneMem, void* assetList, unsigned flags, bool wasPaused, int failureMode, void* signature)
		{
			const DWORD entryError = GetLastError();
			char zone[256]{};
			safemem::ReadString(name, zone, sizeof(zone));
			for (char* p = zone; *p; ++p)
				if (static_cast<unsigned char>(*p) < 32 || *p == 127) *p = ' ';
			const bool xmasSrv = night_files::ChristmasEmptySrvPoC() &&
				std::string_view(zone) == "srv_mp_jup_shipment_xmas";
			if (xmasSrv)
			{
				const int index = night_files::Find("srv_mp_jup_shipment_xmas.ff");
				if (index < 0)
					LOG_ERR("XmasSrvPoC", "native DB_LoadFastfile entry without a verified Christmas srv registry entry");
				else
				{
					const auto& path = night_files::Path(static_cast<unsigned>(index));
					std::error_code error;
					const auto size = std::filesystem::file_size(path, error);
					const auto sha = error ? std::string{} : image_identity::Digest(path.c_str());
					LOG_INFO("XmasSrvPoC", "native DB_LoadFastfile ENTRY zone=%s path=%ls bytes=%llu sha256=%s flags=0x%X paused=%u failure_mode=%d",
						zone, path.c_str(), error ? 0ULL : static_cast<unsigned long long>(size),
						sha.empty() ? "UNAVAILABLE" : sha.c_str(), flags, unsigned(wasPaused), failureMode);
				}
			}
			LOG_INFO("Fastfile", "%s %s.ff", wasPaused ? "Resuming" : "Loading", zone);
			const ULONGLONG started = GetTickCount64();
			SetLastError(entryError);
			const unsigned result =
				g_original.load(std::memory_order_acquire)(name, zoneMem, assetList, flags, wasPaused, failureMode, signature);
			const DWORD exitError = GetLastError();
			const char* state = result == 0 ? "Loaded"
				: result == 1 ? "Paused"
				: result == 2 ? "Cancelled"
				: result == 3 ? "Unavailable"
				: "Returned";
			LOG_INFO("Fastfile", "%s %s.ff | %llu ms | result=%u", state, zone, GetTickCount64() - started, result);
			if (xmasSrv)
				LOG_INFO("XmasSrvPoC", "native DB_LoadFastfile RETURN zone=%s result=%u (%s) win32_error=%lu",
					zone, result, state, static_cast<unsigned long>(exitError));
			if (menu_art1645::Enabled() && std::strcmp(zone, menu_art1645::kZone) == 0) menu_art1645::ObserveLoad(result == 0);
			SetLastError(exitError);
			return result;
		}
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (g_original.load(std::memory_order_acquire))
			return hook::Status::Installed;
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(moduleBase + kLoadRva), &Load, kLoadBytes, sizeof(kLoadBytes), g_original);
		if (VerifyFiles())
		{
			if (!g_files && !menu_art1645::Enabled() && !night_files::Enabled() && !weapon_files::Enabled()) return batch.Commit();
			g_base = moduleBase;
			g_openPath.store(reinterpret_cast<OpenPathFn>(moduleBase + 0x13f4d50), std::memory_order_release);
			static constexpr uint8_t openBytes[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac,
				0x24, 0x00, 0xfe, 0xff, 0xff };
			static constexpr uint8_t existsBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81, 0xec, 0xe0, 0x02, 0x00, 0x00 };
			static constexpr uint8_t looseBytes[] = { 0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xec, 0x50, 0x03, 0x00, 0x00 };
			static constexpr uint8_t initBytes[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b, 0x81, 0xb0, 0x00, 0x00, 0x00 };
			batch.Add(reinterpret_cast<void*>(moduleBase + 0xd8a2b0), &Open, openBytes, sizeof(openBytes), g_open);
			batch.Add(reinterpret_cast<void*>(moduleBase + 0xd8a850), &Exists, existsBytes, sizeof(existsBytes), g_exists);
			batch.Add(reinterpret_cast<void*>(moduleBase + 0x13f4a30), &Loose, looseBytes, sizeof(looseBytes), g_loose);
			batch.Add(reinterpret_cast<void*>(moduleBase + 0xd871e0), &InitStream, initBytes, sizeof(initBytes), g_init);
			if (((g_scene124 || g_scene128Test) && g_cacheReady) || menu_art1645::Enabled() || night_files::Enabled() ||
				weapon_files::Enabled())
			{
				struct Binding
				{
					uintptr_t rva;
					std::array<uint8_t, 12> expected;
				};

				static constexpr Binding bindings[]{ { 0x159d710,
														 { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55 } },
					{ 0x159ebd0, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89 } } };
				for (const auto& binding : bindings)
				{
					std::array<uint8_t, 12> observed{};
					const auto* site = reinterpret_cast<const void*>(moduleBase + binding.rva);
					if (!safemem::RegionExecutable(site) || !safemem::ReadBytes(site, observed.data(), observed.size()) ||
						observed != binding.expected)
						return hook::Status::Failed;
				}
				g_findPack.store(reinterpret_cast<FindPackFn>(moduleBase + 0x159ebd0), std::memory_order_release);
				g_addPack.store(reinterpret_cast<AddPackFn>(moduleBase + 0x159d710), std::memory_order_release);
				static constexpr uint8_t packagesBytes[] = { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55, 0x48,
					0x8d, 0x68, 0xc8 };
				static constexpr uint8_t verifyPackBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x57, 0x41,
					0x56, 0x48, 0x83, 0xec, 0x70 };
				static constexpr uint8_t verifySignatureBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0xb8, 0x40, 0xbc, 0x00,
					0x00, 0xe8, 0x1e, 0x52, 0x44, 0x01 };
				batch.Add(reinterpret_cast<void*>(moduleBase + 0x159db40), &LoadPackages, packagesBytes, sizeof(packagesBytes), g_packages);
				batch.Add(
					reinterpret_cast<void*>(moduleBase + 0x159f800), &VerifyPack, verifyPackBytes, sizeof(verifyPackBytes), g_verifyPack);
				batch.Add(reinterpret_cast<void*>(moduleBase + 0xd87cc0), &VerifySignature, verifySignatureBytes,
					sizeof(verifySignatureBytes), g_verifySignature);
			}
		}
		else return hook::Status::Failed;
		return batch.Commit();
	}
}
