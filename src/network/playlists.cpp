#include "network/local_playlists.h"
#include "network/native_network.h"
#include "network/backend/native_matchmaking.h"
#include "network/playlists.h"

#include "engine/addresses.hpp"

#include "network/backend/backend_agent.h"
#include "dedicated/headless/non_player.h"
#include "network/playlist_document.h"
#include "network/playlist_selection.h"
#include "network/client_playlist_map.hpp"

#include "common/console.h"

#include "auth/offline_auth.h"
#include "dedicated/dedicated.h"
#include "network/backend/playlist_backend.h"

#include "engine/game.h"
#include "engine/safemem.h"

#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include <bcrypt.h>
namespace playlists
{
	namespace
	{
		// Verified against Replay's LiveStorage_PlaylistsFrame and native zlib callers.
		// These bindings deliberately do not use the unchecked UncompressPlaylists wrapper.
		struct Binding
		{
			uintptr_t rva;
			std::array<unsigned char, 16> bytes;
		};

		constexpr Binding kBindings[] = {
			{ 0x119a5a0, { 0x40, 0x56, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xf1, 0xe8, 0x72, 0x45, 0xff, 0xff, 0x85, 0xc0 } },
			// Native exec's fastfile path and named dvar lookup/setters (TU19).
			{ 0x1297140, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 } },
			{ 0x13e9a20, { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xec, 0x60, 0x08 } },
			{ 0x13e66c0, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0x30, 0x04, 0x00, 0x00, 0x48, 0x8b, 0x05 } },
			{ 0x13e62c0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xd9, 0xf0, 0xff, 0x05, 0x09, 0xb0, 0xf8, 0x0c, 0x8b } },
			// DB fastfile existence query through the mounted filesystem (not loose files).
			{ 0x11a7940, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x05 } },
			{ 0x13e97b0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x05, 0x3b, 0xac, 0x66, 0x04, 0x48, 0x33, 0xc4 } },
			{ 0x10c6bd0, { 0x83, 0x3d, 0xd9, 0x52, 0x42, 0x0b, 0x05, 0x0f, 0x9d, 0xc0, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
			{ 0x10d3ac0, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 } },
			{ 0x10ce1a0, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x55 } },
			{ 0x1b9f890, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0x7c, 0x24, 0x30, 0xe8, 0x60, 0xdb, 0x52, 0xff } },
			{ 0x206b170, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x41, 0x8d, 0x80, 0xf0, 0xfd, 0xff } },
			{ 0x206b040, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xd2, 0x0f, 0x84, 0x0d, 0x01 } },
			{ 0x206ab70, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48 } },
			{ 0x206afe0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x41, 0x48, 0x8b } },
			{ 0x206b1e0, { 0x33, 0xc0, 0x48, 0x89, 0x41, 0x40, 0x48, 0x89, 0x41, 0x38, 0x48, 0x89, 0x41, 0x30, 0xc3, 0xcc } },
			{ 0x12cf280, { 0x48, 0x83, 0xec, 0x28, 0xff, 0x15, 0x56, 0x22, 0x08, 0x01, 0x3b, 0x05, 0x90, 0x01, 0x2b, 0x0c } },
			// Native Playlist_RunRules recipe path, verified in replay_all_playlist_bindings.json.
			{ 0x15990e0, { 0x48, 0x8d, 0x05, 0x39, 0x16, 0x8c, 0x0d, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
			{ 0x159a500, { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0xb8, 0x70 } },
			{ 0x1599be0, { 0x8b, 0x15, 0x06, 0x1e, 0xc7, 0x0b, 0xe9, 0xe5, 0x03, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
			{ 0x1599eb0, { 0x8b, 0x15, 0x2e, 0x1a, 0xc7, 0x0b, 0xe9, 0x45, 0xfb, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
			{ 0x159a160, { 0x8b, 0x15, 0x82, 0x17, 0xc7, 0x0b, 0xe9, 0x95, 0xf8, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
			{ 0x159a690, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x82, 0xfd, 0xff, 0xff, 0x8b, 0x15 } },
			{ 0x11916c0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x89, 0x91, 0x34, 0xbf, 0x04, 0x00, 0x8d } },
			{ 0x13e8b50, { 0x48, 0x83, 0xec, 0x38, 0x80, 0x79, 0x1d, 0x00, 0x75, 0x06, 0x88, 0x54, 0x24, 0x20, 0xeb, 0x19 } },
			{ 0x13e9be0, { 0x40, 0x53, 0x48, 0x81, 0xec, 0x30, 0x08, 0x00, 0x00, 0x48, 0x8b, 0x05, 0x08, 0xa8, 0x66, 0x04 } },
		};
		constexpr uintptr_t kLoaded = 0xd3278a1, kOutOfDate = 0xd3278a0;
		constexpr uintptr_t kText = 0xd335940, kVersion = 0xc5484dc;
		constexpr uintptr_t kGametypeLast = 0xc57a4f8, kEntries = 0xc5484d8;
		constexpr uintptr_t kPlaylistTable = 0xc524ee0;
		constexpr size_t kTextCapacity = 0x50000;

		uintptr_t g_base = 0;
		bool g_localCatalog = false, g_localAttempted = false, g_localPending = false, g_requireBackend = false;
		std::filesystem::path g_localPath;
		std::atomic<bool> g_installed{ false }, g_ready{ false };
		std::mutex g_lock;
		std::string g_loadedHash;
		std::array<std::string, 129> g_extendedRules;
		DocumentInfo g_info;
		std::string g_error;
		// Dedicated main-thread state; rebuilt whenever the native playlist is loaded.
		bool g_mapsScanned = false;
		std::array<bool, 128> g_mapAvailable{};

		template <class Fn>
		Fn Function(uintptr_t rva)
		{
			return reinterpret_cast<Fn>(g_base + rva);
		}

		template <class T>
		T Read(uintptr_t rva)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
			return value;
		}

		std::string NativeString(uintptr_t address, size_t limit)
		{
			std::string result;
			while (result.size() < limit)
			{
				size_t room = 0;
				const auto p = reinterpret_cast<const char*>(address + result.size());
				if (!safemem::RegionReadable(p, room)) break;
				room = (std::min)(room, limit - result.size());
				if (const auto end = static_cast<const char*>(memchr(p, 0, room)))
				{
					result.append(p, end - p);
					return result;
				}
				result.append(p, room);
			}
			throw std::runtime_error("native playlist string is unreadable or exceeds bounds");
		}
#ifndef IW8X_CLIENT_ONLY

		void ScanDedicatedMaps()
		{
			if (g_mapsScanned) return;
			if (!Read<uintptr_t>(0xb8f6cb8)) throw std::runtime_error("native filesystem is not initialized");
			const int count = Read<uint8_t>(0xc57c910);
			if (count > 128) throw std::runtime_error("invalid native map count");
			nlohmann::json report = { { "check", "native base, worldwide and techsets fastfile existence; not a full integrity check" },
				{ "maps", nlohmann::json::array() }, { "playlists", nlohmann::json::array() } };
			unsigned available = 0, checked = 0;
			for (int map = 0; map < count; ++map)
			{
				const auto name = NativeString(Read<uintptr_t>(0xc57c510 + map * 8), 24);
				if (!name.starts_with("mp_")) continue;
				auto missing = nlohmann::json::array();
				// The partial installation can contain map shaders but lack ww_map.ff.
				// Query the same mounted archive filesystem used by native map loading.
				// Optional language fastfiles and streaming chunks are not validated here.
				for (const auto& prefix : { "", "ww_", "techsets_" })
				{
					const auto zone = std::string(prefix) + name;
					if (!Function<bool (*)(const char*)>(0x11a7940)(zone.c_str())) missing.push_back(zone + ".ff");
				}
				g_mapAvailable[map] = missing.empty();
				++checked;
				if (g_mapAvailable[map]) ++available;
				report["maps"].push_back({ { "map", name }, { "available", g_mapAvailable[map] }, { "missing", missing } });
				LOG_INFO("Playlists", "installed map scan: %s %s", name.c_str(), g_mapAvailable[map] ? "present" : "missing fastfiles");
			}
			for (int id = 0; id <= 128; ++id)
				if (g_info.multiplayer[id])
				{
					auto entries = nlohmann::json::array();
					const auto record = kPlaylistTable + id * 928;
					const int first = Read<uint16_t>(record + 864), total = Read<uint16_t>(record + 866);
					if (first + total > 2048) throw std::runtime_error("invalid native map entry range");
					for (int entry = 0; entry < total; ++entry)
					{
						const auto item = 0xc57a500 + 4 * (first + entry);
						const auto map = Read<uint8_t>(item);
						if (map < g_mapAvailable.size() && g_mapAvailable[map] && Read<uint8_t>(item + 2)) entries.push_back(entry);
					}
					report["playlists"].push_back({ { "id", id }, { "available_entries", entries }, { "total_entries", total } });
				}
			// An operator-readable scan, never an input that can become stale. Each new
			// dedicated process checks its own installation, including newly added maps.
			std::error_code ec;
			std::filesystem::create_directories("playlist-servers", ec);
			std::ofstream file("playlist-servers/installed-maps.json", std::ios::binary | std::ios::trunc);
			if (file) file << report.dump(2) << '\n';
			if (!file) LOG_WARN("Playlists", "could not write installed-maps.json; native map checks remain active");
			OP_INFO("Installed playlist maps | %u/%u have required fastfiles | playlist-servers/installed-maps.json", available, checked);
			g_mapsScanned = true;
		}
#endif


		bool Writable(uintptr_t rva, size_t length)
		{
			// Replay's buffer crosses a WRITECOPY/READWRITE region boundary on a
			// clean launch. Validate every region without changing page protection.
			auto address = g_base + rva;
			while (length)
			{
				size_t available = 0;
				const auto pointer = reinterpret_cast<void*>(address);
				if (!safemem::RegionReadable(pointer, available) || !available || !safemem::RegionWritable(pointer)) return false;
				const auto step = (std::min)(length, available);
				address += step;
				length -= step;
			}
			return true;
		}

		bool PlayingLocalMatch()
		{
			// The MP menu renders a frontend server too (sv_running=1, scene state8).
			// Com_FrontEndScene_IsActive returns scene state >=5; it distinguishes
			// that server from a locally hosted match. Verified Replay RVA 10C6BD0.
			return Function<bool (*)()>(game::kIsAnyLocalServerRunningRVA)() && !Function<bool (*)()>(game::kIsFrontEndRVA)();
		}

		void Apply(const playlist_catalog::Snapshot& catalog, bool updateFrontend = true, bool localCatalog = false)
		{
			const auto& text = catalog.text;
			const auto prepared = PrepareNativeCatalog(text);
			const auto info = ValidateDocument(prepared.text);
			if (!Writable(kText, kTextCapacity) || !Writable(kLoaded, 1) || !Writable(kOutOfDate, 1))
				throw std::runtime_error("native playlist globals unavailable");

			// All transfer/preflight failures preserve an existing loaded
			// playlist. From this point the native parser owns the table mutation.
			g_ready.store(false, std::memory_order_release);
			g_mapsScanned = false;
			g_mapAvailable.fill(false);
			safemem::WriteByte(reinterpret_cast<void*>(g_base + kLoaded), 0);
			auto buffer = reinterpret_cast<char*>(g_base + kText);
			std::memset(buffer, 0, kTextCapacity);
			std::memcpy(buffer, prepared.text.data(), prepared.text.size());
			Function<void (*)(const char*)>(0x10ce1a0)(buffer);
			if (Read<int>(kVersion) != info.version || Read<int>(kGametypeLast) + 1 != info.gametypes ||
				Read<uint16_t>(kEntries) != info.entries)
				throw std::runtime_error("native playlist registration counts/version disagree with input");
			for (int id = 0; id <= 128; ++id)
				if (info.ids[id] && !Read<uint16_t>(kPlaylistTable + id * 928 + 866))
					throw std::runtime_error("native playlist has no entries for ID " + std::to_string(id));
			// Native 10CE1A0 writes playlist rules to +848 in its 928-byte records.
			// Validate every source pointer inside the native 200 KiB pool before
			// publishing owned expanded strings; old strings survive until reparse.
			std::array<std::string, 129> expanded;
			for (const auto& rule : prepared.rules)
			{
				if (!expanded[rule.playlist].empty()) continue;
				const auto pointer = Read<uintptr_t>(kPlaylistTable + rule.playlist * 928 + 848);
				const auto pool = g_base + 0xc5484f0;
				if (pointer < pool || pointer >= pool + 204800) throw std::runtime_error("native playlist rules escaped their pool");
				expanded[rule.playlist] = ExpandLongRules(NativeString(pointer, pool + 204800 - pointer), rule.playlist, prepared.rules);
			}
			g_extendedRules = std::move(expanded);
			for (int id = 0; id <= 128; ++id)
				if (!g_extendedRules[id].empty())
				{
					const auto pointer = reinterpret_cast<uintptr_t>(g_extendedRules[id].c_str());
					safemem::WriteBytes(reinterpret_cast<void*>(g_base + kPlaylistTable + id * 928 + 848), &pointer, sizeof(pointer));
				}
			std::memset(buffer, 0, kTextCapacity);
			std::memcpy(buffer, text.data(), text.size());
			if (!prepared.rules.empty())
				LOG_INFO("Playlists", "restored %zu complete TU27 long rules after native parse", prepared.rules.size());
			// This order follows the success branch of LiveStorage_PlaylistsFrame.
			safemem::WriteByte(reinterpret_cast<void*>(g_base + kLoaded), 1);
			safemem::WriteByte(reinterpret_cast<void*>(g_base + kOutOfDate), 0);
			if (updateFrontend) Function<void (*)()>(0x1b9f890)();
			{
				std::lock_guard lock(g_lock);
				g_info = info;
				g_loadedHash = catalog.sha256;
				g_localCatalog = localCatalog;
				g_error.clear();
			}
			g_ready.store(true, std::memory_order_release);
			LOG_INFO("Playlists", "native registration complete: source=%s bytes=%zu version=%d gametypes=%zu entries=%zu sha256=%s",
				localCatalog ? "local" : "backend", text.size(), info.version, info.gametypes, info.entries, catalog.sha256.c_str());
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = base;
		wchar_t config[MAX_PATH]{};
		if (native_network::ConfigurationPath(config, MAX_PATH))
			g_requireBackend = GetPrivateProfileIntW(L"playlists", L"require_backend", 0, config) != 0;
		for (const auto& binding : kBindings)
		{
			std::array<unsigned char, 16> actual{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), actual.data(), actual.size()) || actual != binding.bytes)
			{
				LOG_ERR("Playlists", "Replay binding mismatch RVA=0x%llX", static_cast<unsigned long long>(binding.rva));
				return hook::Status::NotReady;
			}
		}
		unsigned char serverCheck[sizeof(game::kIsAnyLocalServerRunningPrologue)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kIsAnyLocalServerRunningRVA), serverCheck, sizeof(serverCheck)) ||
			std::memcmp(serverCheck, game::kIsAnyLocalServerRunningPrologue, sizeof(serverCheck)) != 0)
			return hook::Status::NotReady;
		g_installed.store(true, std::memory_order_release);
		LOG_INFO("Playlists", "verified Replay native loader bindings; HTTP provider and optional TU19 offline provider available");
		return hook::Status::Installed;
	}

	void Pump()
	{
		if (!g_installed.load(std::memory_order_acquire) || dedicated::Enabled() || !Function<bool (*)()>(0x12cf280)() ||
			!offlineauth::InitializeProfile())
			return;
		// The updater dereferences playlist/category dvars. Never call before registration.
		if (!Read<uintptr_t>(0xd386948) || !Read<uintptr_t>(0xd386960)) return;
		if (PlayingLocalMatch()) return;
		if (!Function<bool (*)()>(game::kIsInFrontEndRVA)()) return;
		std::filesystem::path localPath;
		bool pending = false;
		{
			std::lock_guard lock(g_lock);
			pending = g_localPending;
			localPath = g_localPath;
			g_localPending = false;
		}
		auto catalog = playlist_backend::Take();
		bool localCatalog = pending || (!catalog && !g_requireBackend && !g_localAttempted && !g_ready.load());
		if (localCatalog)
		{
			g_localAttempted = true;
			try
			{
				catalog = local::Load(g_base, localPath);
			}
			catch (const std::exception& error)
			{
				LOG_ERR("Playlists", "%s", error.what());
				return;
			}
		}
		if (!catalog) return;
		try
		{
			Apply(*catalog, true, localCatalog);
		}
		catch (const std::exception& error)
		{
			std::lock_guard lock(g_lock);
			g_error = error.what();
			LOG_ERR("Playlists", "%s", g_error.c_str());
		}
	}

	bool Ready()
	{
		if (!g_ready.load(std::memory_order_acquire) || Read<uint8_t>(kLoaded) == 0) return false;
		std::lock_guard lock(g_lock);
		return g_localCatalog || playlist_backend::Current(g_loadedHash);
	}

	bool HasPlaylist(int id)
	{
		if (!Ready() || id < 0 || id > 128) return false;
		std::lock_guard lock(g_lock);
		return g_info.ids[id];
	}
#ifndef IW8X_CLIENT_ONLY

	bool PrepareDedicated(int id, int entry, DedicatedPlan& plan, std::string& error, EntrySelection selection)
	{
		error.clear();
		if (!g_installed.load() || !dedicated::Enabled() || !Function<bool (*)()>(0x12cf280)() || !Read<uintptr_t>(0xd386948) ||
			!Read<uintptr_t>(0xd386950) || !Read<uintptr_t>(0xd386960))
			return false;
		try
		{
			if (!Ready())
			{
				// Dedicated hosts receive the same published catalog as clients.
				// An empty snapshot means the backend fetch is still in flight.
				const auto remote = playlist_backend::Take();
				if (remote)
					Apply(*remote, false);
				else
				{
					if (backend_agent::Enabled()) return false;
					try
					{
						Apply(playlist_backend::ServerCatalog(), false);
					}
					catch (const std::exception&)
					{
						if (g_requireBackend) throw;
						Apply(local::Load(g_base), false, true);
					}
				}
			}
			if (!HasPlaylist(id)) throw std::runtime_error("dedicated playlist ID is absent from the .aggr");
			const auto record = kPlaylistTable + id * 928;
			const auto count = Read<uint16_t>(record + 866);
			const int first = Read<uint16_t>(record + 864);
			if (!count || count > 2048 || first + count > 2048) throw std::runtime_error("invalid native playlist entry range");
			if (entry < 0 || entry >= count) throw std::runtime_error("dedicated playlist entry is out of range");
			ScanDedicatedMaps();
			const auto usable = [&](int candidate)
			{
				const auto item = 0xc57a500 + 4 * (first + candidate);
				const auto map = Read<uint8_t>(item);
				return map < g_mapAvailable.size() && g_mapAvailable[map] && Read<uint8_t>(item + 2);
			};
			if (selection == EntrySelection::WeightedAvailable)
			{
				std::vector<WeightedEntry> entries;
				for (int candidate = 0; candidate < count; ++candidate)
				{
					const auto item = 0xc57a500 + 4 * (first + candidate);
					const auto map = Read<uint8_t>(item), type = Read<uint8_t>(item + 1);
					bool available = usable(candidate) && map < Read<uint8_t>(0xc57c910) && type < 64;
					if (available)
						available = NativeString(Read<uintptr_t>(0xc57c510 + map * 8), 24).starts_with("mp_") &&
							!NativeString(g_base + 0xc57c920 + type * 136, 16).starts_with("cp_");
					entries.push_back({ candidate, Read<uint8_t>(item + 2), available });
				}
				const uint32_t total = EntryWeight(entries);
				if (!total) throw std::runtime_error("no installed weighted entries in this MP playlist");
				// Select once on the authoritative server, before cfg/recipe/devmap.
				// Rejection sampling preserves native .aggr weights without modulo bias.
				uint32_t random = 0;
				const uint32_t minimum = uint32_t(0 - total) % total;
				do
				{
					if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
						throw std::runtime_error("could not obtain playlist selection randomness");
				}
				while (random < minimum);
				entry = PickEntry(entries, random % total);
				OP_INFO("Playlist map selected | playlist=%d entry=%d eligible_weight=%u", id, entry, total);
			}
			if (!usable(entry))
			{
				if (selection == EntrySelection::Exact)
					throw std::runtime_error(
						"selected playlist entry has missing fastfiles or zero weight; see playlist-servers/installed-maps.json");
				const int next = NextDedicatedEntry(id, entry);
				if (next < 0)
					throw std::runtime_error(
						"no installed maps in this playlist; see playlist-servers/installed-maps.json (compatible map files required)");
				OP_INFO("Skipping unavailable playlist maps | id=%d entry=%d -> %d", id, entry, next);
				entry = next;
			}
			const auto item = 0xc57a500 + 4 * (Read<uint16_t>(record + 864) + entry);
			const auto map = Read<uint8_t>(item), type = Read<uint8_t>(item + 1);
			if (map >= Read<uint8_t>(0xc57c910) || type >= 64)
				throw std::runtime_error("native playlist entry has invalid map/gametype indices");
			const auto gametype = 0xc57c920 + type * 136;
			const auto string = NativeString;
			DedicatedPlan selected;
			selected.entry = entry;
			selected.map = string(Read<uintptr_t>(0xc57c510 + map * 8), 24);
			selected.gametype = string(g_base + gametype, 16);
			selected.weight = Read<uint8_t>(item + 2);
			selected.maxPlayers = Read<uint8_t>(record + 872); // Playlist_GetMaxPlayers, 10CD030.
			const int teamSize = Read<int>(gametype + 120);
			selected.teamCount = teamSize > 0 ? selected.maxPlayers / teamSize : Read<int>(gametype + 116);
			if (selected.maxPlayers < 1 || selected.maxPlayers > 200 || selected.teamCount < 0 || selected.teamCount > 200)
				throw std::runtime_error("native playlist has invalid player/team capacity");
			if (Read<uint8_t>(gametype + 112)) selected.recipe = string(g_base + gametype + 16, 16);
			selected.hardcore = Read<uint8_t>(gametype + 124) != 0;
			selected.tactical = Read<uint8_t>(gametype + 125) != 0;
			const auto token = [](const std::string& s)
			{
				return !s.empty() && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == s.npos;
			};
			if (!token(selected.map) || !token(selected.gametype) || (!selected.recipe.empty() && !token(selected.recipe)))
				throw std::runtime_error("invalid native map/script/recipe token");
			if (!selected.map.starts_with("mp_") || selected.gametype.starts_with("cp_"))
				throw std::runtime_error("this MP-only build cannot host campaign/co-op playlists");
			for (int category = 0; category < 26; ++category)
			{
				const auto cat = 0xc542280 + category * 968;
				const auto children = Read<uint8_t>(cat + 800);
				if (children > 20) throw std::runtime_error("invalid native category count");
				bool found = false;
				for (int child = 0; child < children; ++child)
					if (Read<int>(cat + 640 + child * 8) == 0 && Read<uint8_t>(cat + 644 + child * 8) == id) found = true;
				if (found)
				{
					selected.category = category;
					break;
				}
			}
			// Playlist_RunRules (Replay 0x10d23e0) executes these native strings in
			// this order. Its client party/voting/DDL side effects are unnecessary
			// for the existing headless cfg -> devmap startup.
			for (auto pointer : { Read<uintptr_t>(0xc57c500), Read<uintptr_t>(gametype + 96), Read<uintptr_t>(record + 848),
					 Read<uintptr_t>(0xc542280 + selected.category * 968 + 960) })
			{
				if (pointer)
				{
					selected.rules += string(pointer, 0xe000);
					selected.rules += '\n';
				}
			}
			if (selected.rules.size() > 0xe000) throw std::runtime_error("dedicated rules exceed command buffer budget");
			plan = std::move(selected);
			LOG_INFO("Playlists", "dedicated plan: id=%d entry=%d category=%d map=%s script=%s recipe=%s hardcore=%d native_rule_bytes=%zu",
				id, entry, plan.category, plan.map.c_str(), plan.gametype.c_str(), plan.recipe.c_str(), plan.hardcore, plan.rules.size());
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool ApplyDedicatedSettings(const DedicatedPlan& plan, std::string& error)
	{
		error.clear();
		if (!g_installed.load() || !dedicated::Enabled() || !Ready())
		{
			error = "dedicated playlist settings are not ready";
			return false;
		}
		const auto settings = Function<uintptr_t (*)()>(0x15990e0)();
		uintptr_t recipe = 0;
		const auto scriptDvar = Read<uintptr_t>(0xd386738);
		const auto hardcoreDvar = Read<uintptr_t>(0xd386760);
		const auto tacticalDvar = Read<uintptr_t>(0xd386768);
		const auto uiMaxClients = Read<uintptr_t>(0xd386878);
		const auto partyMaxPlayers = Read<uintptr_t>(0xd386840);
		const auto teamCountDvar = Read<uintptr_t>(0xc796328);
		if (!settings || !Writable(settings - g_base, 1) || !scriptDvar || !hardcoreDvar || !tacticalDvar || !uiMaxClients ||
			!partyMaxPlayers || !teamCountDvar || !safemem::ReadBytes(reinterpret_cast<void*>(settings + 8), &recipe, sizeof(recipe)))
		{
			error = "native match settings/dvars are not initialized";
			return false;
		}
		bool hardcore = plan.hardcore, tactical = plan.tactical;
		const char* script = plan.gametype.c_str();
		if (!plan.recipe.empty())
		{
			// LoadMatchRulesFromPlaylists reparses the retained genuine .aggr JSON,
			// builds recipes.ddl and its checksum. Do not translate recipes to dvars.
			if (!recipe || !Writable(recipe - g_base, 4104) ||
				!Function<int (*)(uintptr_t, const char*)>(0x159a500)(recipe, plan.recipe.c_str()))
			{
				error = "native playlist recipe load failed: " + plan.recipe;
				return false;
			}
			script = Function<const char* (*)(uintptr_t)>(0x1599be0)(recipe);
			char actual[16]{};
			if (!safemem::ReadString(script, actual, sizeof(actual)) || plan.gametype != actual)
			{
				error = "native recipe script differs from playlist entry";
				return false;
			}
			hardcore = Function<bool (*)(uintptr_t)>(0x1599eb0)(recipe);
			tactical = Function<bool (*)(uintptr_t)>(0x159a160)(recipe);
		}
		Function<void (*)(uintptr_t, const char*)>(0x13e9be0)(scriptDvar, script);
		Function<void (*)(uintptr_t, bool)>(0x13e8b50)(hardcoreDvar, hardcore);
		Function<void (*)(uintptr_t, bool)>(0x13e8b50)(tacticalDvar, tactical);
		// DevSetup copies the private LAN party's default capacity (20) into the
		// UI startup settings. Replay keeps its headless host registered in slot0:
		// DirectConnect (136EB20) cannot give that slot to a real player or bot even
		// with SV state CS_FREE. Reserve it in the native allocation and party index
		// bit calculation. The native non-player host has no reserved member, so it
		// uses exactly the playlist capacity. Published/bot occupancy limits agree.
		const int nativeSlots = (std::min)(200, plan.maxPlayers + (headless::NonPlayerHost() ? 0 : 1));
		Function<void (*)(uintptr_t, int)>(0x13e97b0)(uiMaxClients, nativeSlots);
		Function<void (*)(uintptr_t, int)>(0x13e97b0)(partyMaxPlayers, nativeSlots);
		Function<void (*)(uintptr_t, int)>(0x13e97b0)(teamCountDvar, plan.teamCount);
		const uintptr_t privateParty = native_matchmaking::ServerPartyRVA();
		if (Read<int>(privateParty + 311112) && Read<int>(privateParty + 311116))
			Function<void (*)(uintptr_t, int)>(0x11916c0)(g_base + privateParty, nativeSlots);
		if (native_matchmaking::UseOnlineLobby())
		{
			// Public party snapshots serialize the playlist entry ring, not just
			// ui_mapname. A later snapshot reruns Playlist_RunRules on the client;
			// an uninitialized/random entry replaces the server's installed map.
			// Match the fields written by native Party_SetPlaylistEntries1189EB0.
			const int index = Read<int>(privateParty + 351908);
			const uint8_t entry = static_cast<uint8_t>(plan.entry);
			if (index < 0 || index >= 8 || plan.entry < 0 || plan.entry >= 255 ||
				!safemem::WriteBytes(reinterpret_cast<void*>(g_base + privateParty + 351896 + index), &entry, 1) ||
				!safemem::WriteBytes(reinterpret_cast<void*>(g_base + privateParty + 351904), &entry, 1) ||
				!safemem::WriteBytes(reinterpret_cast<void*>(g_base + privateParty + 351905), &entry, 1))
			{
				error = "native playlist entry ring unavailable";
				return false;
			}
			Function<void (*)(uintptr_t)>(0x119a5a0)(g_base + privateParty);
		}
		const uint8_t usingRecipe = !plan.recipe.empty();
		// Match Playlist_RunRules' direct flag store. The broader native toggle also
		// rewrites map/recipe defaults, which would discard the selected entry.
		if (!safemem::WriteBytes(reinterpret_cast<void*>(settings), &usingRecipe, sizeof(usingRecipe)))
		{
			error = "could not enable native playlist recipe state";
			return false;
		}
		if (usingRecipe)
		{
			// LAN clients receive recipes through native party snapshots. Merely
			// setting usingRecipe leaves the snapshot time at zero, equal to a new
			// client's ack, so no delta is sent and that client keeps default war
			// rules. MatchRulesChanged publishes the recipe through the host party
			// after DevSetup has created it (159A690 -> 119BD50). Keep native checksum
			// validation and the native recipe-delta protocol intact.
			Function<void (*)(uintptr_t)>(0x159a690)(recipe);
		}
		LOG_INFO("Playlists", "native match settings applied: recipe=%s hardcore=%d tactical=%d max_players=%d native_slots=%d teams=%d",
			plan.recipe.empty() ? "none" : plan.recipe.c_str(), hardcore, tactical, plan.maxPlayers, nativeSlots, plan.teamCount);
		return true;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	int NextDedicatedEntry(int id, int entry)
	{
		if (!HasPlaylist(id) || !dedicated::Enabled() || !g_mapsScanned) return -1;
		const auto record = kPlaylistTable + id * 928;
		const int count = Read<uint16_t>(record + 866);
		const int first = Read<uint16_t>(record + 864);
		if (entry < 0 || entry >= count || count > 2048 || first + count > 2048) return -1;
		for (int offset = 1; offset <= count; ++offset)
		{
			const int next = (entry + offset) % count;
			const auto item = 0xc57a500 + 4 * (first + next);
			const auto map = Read<uint8_t>(item);
			if (map < g_mapAvailable.size() && g_mapAvailable[map] && Read<uint8_t>(item + 2)) return next;
		}
		return -1;
	}
#endif


	static bool SelectNative(int id)
	{
		const auto ordinalDvar = Read<uintptr_t>(0xd386948);
		const auto idDvar = Read<uintptr_t>(0xd386950);
		if (!ordinalDvar || !idDvar) return false;
		int ordinal = 0;
		for (int prior = 0; prior < id; ++prior)
			if (Read<uint16_t>(kPlaylistTable + prior * 928 + 866)) ++ordinal;
		// Replay's matchmaker join callback (RVA 10443C0) uses runRules=false.
		// This updates the ID, ordinal and profile through the native setters;
		// host map/rule selection is a separate transition (runRules=true).
		Function<void (*)(int, int, bool)>(0x10d3ac0)(0, id, false);
		int actualOrdinal = -1, actualId = -1;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(ordinalDvar + 40), &actualOrdinal, sizeof(actualOrdinal)) ||
			!safemem::ReadBytes(reinterpret_cast<void*>(idDvar + 40), &actualId, sizeof(actualId)) || actualOrdinal != ordinal ||
			actualId != id || Read<int>(0xee56c58) != ordinal || Read<int>(0xef28c0c) != ordinal)
		{
			LOG_ERR("Playlists", "native selection mismatch: requested=%d/%d actual=%d/%d", id, ordinal, actualId, actualOrdinal);
			return false;
		}
		LOG_INFO("Playlists", "native matchmaking selection: id=%d ordinal=%d", id, ordinal);
		return true;
	}

	bool SelectForMatchmaking(int id)
	{
		if (!HasPlaylist(id) || dedicated::Enabled() || !Function<bool (*)()>(0x12cf280)() || !offlineauth::IsReady() ||
			PlayingLocalMatch())
			return false;
		{
			std::lock_guard lock(g_lock);
			if (!g_info.multiplayer[id]) return false;
		}
		return SelectNative(id);
	}

		bool MaintainAssignedClientLobbyMap(int id, std::string_view assignedMap, std::string_view assignedMode)
		{
			// An ordinary stock/rotating playlist must never be pinned to Island.
			// Authenticated assignment map+mode+playlist are all required.
			const auto required = client_playlist_map::AssignedIslandMap(assignedMap, assignedMode, id);
			if (required.empty()) return false;
			if (dedicated::Enabled() || !Function<bool (*)()>(game::kIsInFrontEndRVA)())
				return false;
			const auto current = Function<const char* (*)(const char*)>(game::kDvarGetStringRVA)(
				client_playlist_map::kReplayUiMapnameDvar);
			char actual[48]{};
			if (!current || !safemem::ReadString(current, actual, sizeof(actual)))
			{
				LOG_ERR("Playlists", "Island join: ui_mapname is unreadable in native lobby");
				return false;
			}
			if (std::string_view(actual) == required) return true;
			// The native string dvar's registered/obfuscated name, not a raw
			// pointer write and not a late command queued behind map preload.
			Function<void (*)(const char*, const char*)>(0x13e9a20)(
				client_playlist_map::kReplayUiMapnameDvar, required.data());
			const auto readback = Function<const char* (*)(const char*)>(game::kDvarGetStringRVA)(
				client_playlist_map::kReplayUiMapnameDvar);
			char verified[48]{};
			if (!readback || !safemem::ReadString(readback, verified, sizeof(verified)) ||
				std::string_view(verified) != required)
			{
				LOG_ERR("Playlists", "Island join: ui_mapname native readback mismatch, old=%s new=%s",
					actual, verified);
				return false;
			}
			LOG_INFO("Playlists", "restored authenticated client ui_mapname=%s in native lobby (replaced=%s)", verified, actual);
			return true;
		}

	bool PrepareClientJoin(int id, std::string_view assignedMap, std::string_view assignedMode)
	{
		if (!HasPlaylist(id) || dedicated::Enabled() || !Function<bool (*)()>(0x12cf280)() || PlayingLocalMatch()) return false;
		std::string variant;
		bool bloodMoney = false;
		{
			std::lock_guard lock(g_lock);
			variant = g_info.brVariant[id];
			bloodMoney = g_info.bloodMoney[id];
		}
		const auto mapToSync = client_playlist_map::AssignedIslandMap(assignedMap, assignedMode, id);
		// Do not accept a mismatched mode/playlist when the authenticated host
		// says Island: a stock Donetsk MP config cannot safely join its BR zone.
		if (assignedMap == "mp_sm_island_1" && mapToSync.empty())
		{
			LOG_ERR("Playlists", "Island client join refused: map/BR playlist assignment inconsistent (playlist=%d mode=%.*s)",
				id, int(assignedMode.size()), assignedMode.data());
			return false;
		}
		if (variant.empty() && mapToSync.empty()) return true;
		if (variant.empty())
		{
			OP_ERR("Island BR join cancelled: Replay playlist has no BR client variant");
			return false;
		}
		// SelectNative intentionally uses runRules=false. A direct invite therefore
		// never runs the BR client config. Use native exec-from-fastfile synchronously
		// before join/preload; queueing it can race loading. Do not run host rules,
		// default_mp.cfg or local map selection here: the server owns those settings.
		if (!Function<bool (*)(unsigned, unsigned, const char*, bool)>(0x1297140)(0, 0, "br_core.cfg", false))
		{
			OP_ERR("BR join cancelled: native br_core.cfg is unavailable");
			return false;
		}
		Function<void (*)(const char*, const char*)>(0x13e9a20)("scr_br_gametype", variant.c_str());
		const auto hash = Function<uint32_t (*)(const char*)>(0x13e66c0)("ui_blood_money");
		const auto dvar = Function<uintptr_t (*)(uint32_t)>(0x13e62c0)(hash);
		if (!dvar || Read<uint8_t>(dvar - g_base + 29) == 9)
			Function<void (*)(const char*, const char*)>(0x13e9a20)("ui_blood_money", bloodMoney ? "1" : "0");
		else if (Read<uint8_t>(dvar - g_base + 29) == 0)
			Function<void (*)(uintptr_t, bool)>(0x13e8b50)(dvar, bloodMoney);
		else if (Read<uint8_t>(dvar - g_base + 29) == 5)
			Function<void (*)(uintptr_t, int)>(0x13e97b0)(dvar, bloodMoney ? 1 : 0);
		else
		{
			OP_ERR("BR join cancelled: unexpected ui_blood_money dvar type");
			return false;
		}
		// A stock config may reset selection dvars; retain the requested ID/profile.
		if (!SelectNative(id)) return false;
		if (!mapToSync.empty())
		{
			// Run synchronously AFTER stock br_core.cfg and playlist selection,
			// BEFORE JoinLobby enters native map loading. Its return is a hard
			// admission gate, not an asynchronous console command.
			if (!MaintainAssignedClientLobbyMap(id, assignedMap, assignedMode))
			{
				LOG_ERR("Playlists", "Island BR join cancelled: ui_mapname could not be synchronized before JoinLobby");
				return false;
			}
			LOG_INFO("Playlists", "authenticated client playlist=%d ui_mapname=mp_sm_island_1 mode=br synchronized before native JoinLobby", id);
		}
		LOG_INFO(
			"Playlists", "client BR config complete before join: playlist=%d variant=%s blood_money=%d", id, variant.c_str(), bloodMoney);
		return true;
	}
#ifndef IW8X_CLIENT_ONLY

	bool SelectDedicated(int id, int category)
	{
		if (!dedicated::Enabled() || !HasPlaylist(id) || category < 0 || category >= 26 || !Function<bool (*)()>(0x12cf280)() ||
			!Read<uintptr_t>(0xd386960))
			return false;
		// Run after default_mp.cfg/devmap, which otherwise resets the ordinal.
		// Replay's runRules=false branch only updates native dvars/active/profile
		// fields; it does not need client LUI or call the host voting path.
		Function<void (*)(uintptr_t, int)>(0x13e97b0)(Read<uintptr_t>(0xd386960), category);
		return SelectNative(id);
	}
#endif


	int ResolveCategoryPlaylist(int category, int child)
	{
		if (!Ready() || category < 0 || category >= 26 || child < 0 || child > 200 || !Function<bool (*)()>(0x12cf280)()) return -1;
		// Native 1B9F370 clamps out-of-range entries, but this bridge needs exact
		// children for filtered searches. Sentinel200 is expanded by the Lua caller.
		// Type0 denotes a playlist; other records navigate to another category.
		const uintptr_t record = 0xc542280 + category * 968;
		const auto count = Read<uint8_t>(record + 800);
		if (!count || count > 20 || child >= count) return -1;
		const int index = child;
		if (Read<int>(record + 640 + index * 8) != 0) return -1;
		const int id = Read<uint8_t>(record + 644 + index * 8);
		return HasPlaylist(id) ? id : -1;
	}

	void LogStatus()
	{
		const bool ready = Ready();
		if (!dedicated::Enabled() && g_base && safemem::RegionExecutable(
			reinterpret_cast<const void*>(g_base + game::kDvarGetStringRVA)))
		{
			const auto current = Function<const char* (*)(const char*)>(game::kDvarGetStringRVA)(
				client_playlist_map::kReplayUiMapnameDvar);
			char currentMap[48]{};
			const bool readable = current && safemem::ReadString(current, currentMap, sizeof(currentMap));
			const auto playlistDvar = Read<uintptr_t>(0xd386950);
			int selected = -1;
			if (playlistDvar)
				safemem::ReadBytes(reinterpret_cast<const void*>(playlistDvar + 40), &selected, sizeof(selected));
			OP_INFO("Client playlist assignment: id=%d ui_mapname=%s", selected,
				readable ? currentMap : "<unavailable>");
		}
		{
			std::lock_guard lock(g_lock);
			OP_INFO("Playlist: ready=%d native_loaded=%d version=%d gametypes=%d entries=%u source=%s sha256=%s error='%s'",
				int(ready), int(Read<uint8_t>(kLoaded)), Read<int>(kVersion), Read<int>(kGametypeLast) + 1,
				unsigned(Read<uint16_t>(kEntries)), g_localCatalog ? "local" : "backend", g_loadedHash.c_str(), g_error.c_str());
		}
		playlist_backend::LogStatus();
	}

	bool HandleCommand(std::string_view command)
	{
		if (command == "playlist_status")
		{
			LogStatus();
			return true;
		}
		if (command == "playlist_select" || command.starts_with("playlist_select "))
		{
			const auto value = command.size() > 16 ? command.substr(16) : std::string_view{};
			int id = -1;
			if (value.empty())
			{
				OP_ERR("Usage: playlist_select <registered ID>");
				return true;
			}
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
			if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
				OP_ERR("Usage: playlist_select <registered ID>");
			else if (SelectForMatchmaking(id))
				OP_INFO("Native playlist %d selected", id);
			else
				OP_ERR("Cannot select playlist %d; MP frontend and registered playlist required", id);
			return true;
		}
#ifndef IW8X_CLIENT_ONLY
		if (command == "playlist_backend_reload")
		{
			if (!dedicated::Enabled())
				OP_ERR("playlist_backend_reload is dedicated-only");
			else if (g_ready.load())
				OP_ERR("Restart the dedicated server to change an active native catalog");
			else if (playlist_backend::Reload())
				OP_INFO("Dedicated backend playlist catalog ready");
			else
				OP_ERR("Backend catalog load failed; see PlaylistBackend log");
			return true;
		}
#endif

		if (command == "playlist_fetch")
		{
			if (dedicated::Enabled())
				OP_ERR("Use playlist_backend_reload on an idle dedicated server");
			else if (!Function<bool (*)()>(game::kIsInFrontEndRVA)())
				OP_ERR("Return to the frontend before fetching playlists");
			else
			{
				g_ready = false;
				safemem::WriteByte(reinterpret_cast<void*>(g_base + kLoaded), 0);
				playlist_backend::Refresh();
				OP_INFO("Backend playlist fetch queued; local fallback %s", g_requireBackend ? "disabled" : "permitted");
			}
			return true;
		}
		if (command == "playlist_load" || command.starts_with("playlist_load "))
		{
			if (g_requireBackend)
			{
				OP_ERR("Local playlist overrides are disabled by playlists.require_backend; use playlist_fetch");
				return true;
			}
			auto argument = command.size() > 14 ? command.substr(14) : std::string_view{};
			if (argument.size() >= 2 && argument.front() == '"' && argument.back() == '"')
			{
				argument.remove_prefix(1);
				argument.remove_suffix(1);
			}
			std::lock_guard lock(g_lock);
			g_localPath = std::filesystem::path(argument);
			g_localPending = true;
			OP_INFO("Local playlist load queued; waiting for frontend profile/assets");
			return true;
		}

		return false;
	}
}
