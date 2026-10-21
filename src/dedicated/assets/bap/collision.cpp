#include "bap.h"
#include "engine/asset_pools.hpp"

#include "engine/addresses.hpp"

#include "console.h"

#include "game.h"
#include "safemem.h"

#include "state.h"
#include "thirdparty/minhook/include/MinHook.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>

#ifndef IW8X_CLIENT_ONLY
#include "dedicated/assets/bap/internal.hpp"
#else
#include "dedicated/assets/bap/collision.hpp"
#endif
#include "diagnostics/night/night_files.h"
namespace bap::detail
{
	namespace
	{
		bool matches_bytes(uintptr_t address, const uint8_t* expected, size_t size)
		{
			std::array<uint8_t, 16> actual{};
			return size <= actual.size() && safemem::RegionExecutable(reinterpret_cast<void*>(address)) &&
				safemem::ReadBytes(reinterpret_cast<void*>(address), actual.data(), size) && !std::memcmp(actual.data(), expected, size);
		}

		bool writable_range(const void* pointer, size_t size)
		{
			const auto* at = static_cast<const char*>(pointer);
			while (size)
			{
				size_t available = 0;
				if (!safemem::RegionWritable(at) || !safemem::RegionReadable(at, available) || !available) return false;
				const auto length = available < size ? available : size;
				at += length;
				size -= length;
			}
			return true;
		}
	}

	// Reference DLL sub_180008DB8 replaces the MODE_ALL branch of
	// CL_TransientsCollisionMP_ProcessFastfiles: load every registered numbered tile
	// at priority 10000 and request WorldCollision + StaticModels setup. Merely
	// selecting MODE_ALL invokes the stock single-package *_cg_ls_tr load instead.
	// These globals/functions are mapped to Replay, not the reference's RVAs.
	static constexpr uintptr_t kCollisionProcessRva = game::addresses::runtime::collision_collision_process;

	static constexpr uintptr_t kCollisionStateRva = game::addresses::runtime::collision_collision_state;

	// G_Glass_PieceStateChanged calls Glass_GetSystemForInitialPiece before packing
	// destroyed-piece impact metadata.  With BAP MODE_ALL, collision can be resident
	// for a Donetsk tile whose visual FxGlass transient is not.  Replay's helper then
	// legitimately returns nullptr, while the stock caller immediately dereferences
	// it in Glass_GetInitialPieceCenter.  Keep the server-side glass transition, nav
	// obstacle removal, and script notification by steering only that case through
	// the stock zero-impact branch.
	static constexpr uintptr_t kGlassPieceStateChangedRva = game::addresses::runtime::collision_glass_piece_state_changed;

	static constexpr uintptr_t kGlassGetSystemForInitialPieceRva = game::addresses::runtime::collision_glass_get_system_for_initial_piece;

	static constexpr uint8_t kGlassPieceStateChangedBytes[] = {
		0x4C,
		0x8B,
		0xDC,
		0x53,
		0x55,
		0x41,
		0x56,
		0x48,
		0x81,
		0xEC,
		0x80,
		0x00,
		0x00,
		0x00,
		0x48,
		0x8B,
	};

	static constexpr uint8_t kGlassGetSystemForInitialPieceBytes[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x83,
		0x3D,
		0x84,
		0x87,
		0x93,
		0x0D,
		0x01,
		0x48,
		0x8B,
		0xDA,
		0x44,
	};

	static CollisionState* g_collision = nullptr;

	static CollisionLoadFn g_collisionLoad = nullptr;

	static CollisionTileFn g_collisionSetupWorld = nullptr;

	static CollisionTileFn g_collisionSetupModels = nullptr;

	static CollisionUpdateFn g_collisionUpdate = nullptr;

	static std::atomic<CollisionProcessFn> g_collisionProcess{ nullptr };

	static std::atomic<GlassPieceStateChangedFn> g_glassPieceStateChanged{ nullptr };

	static GlassGetSystemForInitialPieceFn g_glassGetSystemForInitialPiece = nullptr;

	static std::atomic<unsigned> g_glassTransientFallbackCount{ 0 };

	static std::atomic<bool> g_nightCollisionEnabled{ false };
	static uintptr_t g_collisionBase = 0;
	static std::atomic<bool> g_collisionReady{ false };

	static bool g_collisionQueuedLogged = false;

	static bool g_collisionReadyLogged = false;

	static bool g_collisionInvalidLogged = false;
	ULONGLONG g_collisionLastProgress = 0;

	__declspec(noinline) void __fastcall GlassPieceStateChangedDetour(
		unsigned pieceIndex, int oldState, const float* damagePos, const float* damageDir)
	{
		const auto original = g_glassPieceStateChanged.load(std::memory_order_acquire);
		if (!original) return;

		// Stock takes its safe sentinel path when damageDir has no magnitude.  Only
		// probe transient glass residency when stock would otherwise need center/axis
		// data to pack impact metadata.
		const float lengthSq = damageDir ? damageDir[0] * damageDir[0] + damageDir[1] * damageDir[1] + damageDir[2] * damageDir[2] : 0.0f;
		if (lengthSq > 0.0f && g_collision && g_glassGetSystemForInitialPiece && ShouldUseFullCollision(g_collision->mapName))
		{
			unsigned localPieceIndex = 0;
			if (!g_glassGetSystemForInitialPiece(pieceIndex, &localPieceIndex))
			{
				// This is exactly the information stock cannot calculate without the
				// unloaded FxGlassSystem.  A zero direction selects Replay's existing
				// piece+8=0x00FF / piece+0xA=0 fallback, after which stock still calls
				// Nav_DestroyObstacleByGlass and emits its normal script notification.
				static const float zeroImpactDirection[3]{ 0.0f, 0.0f, 0.0f };
				const unsigned count = g_glassTransientFallbackCount.fetch_add(1, std::memory_order_relaxed) + 1;
				if (count <= 8 || (count & (count - 1)) == 0)
				{
					LOG_WARN("BAP",
						"glass piece %u belongs to unloaded FxGlass transient; using stock zero-impact fallback "
						"(local=%u count=%u)",
						pieceIndex, localPieceIndex, count);
				}
#ifndef IW8X_CLIENT_ONLY
				RecordEvent("GlassTransientFallback", reinterpret_cast<uintptr_t>(_ReturnAddress()), nullptr, "enter", nullptr, pieceIndex,
					localPieceIndex);
#endif
				original(pieceIndex, oldState, damagePos, zeroImpactDirection);
				return;
			}
		}

		original(pieceIndex, oldState, damagePos, damageDir);
	}

	__declspec(noinline) void ProcessCollisionFastfiles(bool force)
	{
		auto& s = *g_collision;
		if (s.mode != 2 || !ShouldUseFullCollision(s.mapName))
		{
			g_collisionQueuedLogged = g_collisionReadyLogged = g_collisionInvalidLogged = false;
			// Includes MODE_NONE: the native shutdown/unload path consumes exactly
			// the individual request bits we set, including on a same-map restart.
			g_collisionProcess.load(std::memory_order_acquire)(force);
			return;
		}
		if (s.paused && !force) return;
		if (!s.numTiles) return;
		if (s.numTiles > kCollisionTileLimit)
		{
			if (!g_collisionInvalidLogged) LOG_ERR("BAP", "invalid collision tile count=%u (limit=%u)", s.numTiles, kCollisionTileLimit);
			g_collisionInvalidLogged = true;
			return;
		}
		// Registration supplies the MP-system file indices. Never confuse a tile
		// number or the TransientInfo-local index with a LoadByFileIndex argument.
		for (unsigned tile = 0; tile < s.numTiles; ++tile)
		{
			if (!s.fileIndices[tile])
			{
				if (!g_collisionInvalidLogged)
					LOG_WARN("BAP", "waiting for numbered collision registration: tile=%u count=%u", tile, s.numTiles);
				g_collisionInvalidLogged = true;
				return;
			}
		}
		g_collisionInvalidLogged = false;
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
		unsigned queued = 0;
		for (unsigned tile = 0; tile < s.numTiles; ++tile)
		{
			const unsigned word = tile >> 5;
			const auto bit = 0x80000000u >> (tile & 31);
			if (!(s.loadRequested[word] & bit))
			{
				s.loadRequested[word] |= bit;
#ifndef IW8X_CLIENT_ONLY
				RecordEvent("CollisionTileLoad", caller, nullptr, "enter", nullptr, tile, s.fileIndices[tile]);
#endif
				g_collisionLoad(s.fileIndices[tile], 10000);
				++queued;
			}
		}
		if (!g_collisionQueuedLogged || queued)
		{
			LOG_INFO("BAP", "%s full collision: numbered tiles=%u newly queued=%u priority=10000; ls fastfile unused", s.mapName,
				s.numTiles, queued);
			g_collisionQueuedLogged = true;
			g_collisionReadyLogged = false;

			g_collisionLastProgress = GetTickCount64();
		}

		unsigned loaded = 0;
		unsigned setup = 0;
		for (unsigned tile = 0; tile < s.numTiles; ++tile)
		{
			const unsigned word = tile >> 5;
			const auto bit = 0x80000000u >> (tile & 31);
			// Keep Replay's real completion checks. The reference pre-fills loaded
			// and setup flags; here readiness waits for the DB and physics workers.
			g_collisionUpdate(tile, true);
			if (!(s.loaded[word] & bit)) continue;
			++loaded;
			if (!(s.setupRequested[word] & bit) && !(s.setup[word] & bit))
			{
				s.setupRequested[word] |= bit;
#ifndef IW8X_CLIENT_ONLY
				RecordEvent("CollisionTileSetup", caller, nullptr, "enter", nullptr, tile, s.fileIndices[tile]);
#endif
				g_collisionSetupWorld(tile);
				g_collisionSetupModels(tile);
			}
			if (s.setup[word] & bit) ++setup;
		}
		// HasGridSetupForPosition(MODE_ALL) reads this aggregate. The load-all pair
		// remains false/false so GetZoneName and IsRequestedLoaded use numbered tiles.
		s.setupAll = setup == s.numTiles;

		const auto now = GetTickCount64();
		if (!s.setupAll && now - g_collisionLastProgress >= 5000)
		{
			LOG_INFO("BAP", "%s numbered collision progress: loaded=%u setup=%u tiles=%u", s.mapName, loaded, setup, s.numTiles);
			g_collisionLastProgress = now;
		}
		if (s.setupAll && !g_collisionReadyLogged)
		{
			LOG_INFO("BAP", "%s full collision ready: loaded=%u setup=%u tiles=%u", s.mapName, loaded, setup, s.numTiles);
#ifndef IW8X_CLIENT_ONLY
			RecordEvent("CollisionTilesReady", caller, nullptr, "return", nullptr, loaded, setup);
#endif
			g_collisionReadyLogged = true;
		}
	}
}

namespace bap
{
	using namespace detail;

	bool ShouldUseFullCollision(const char* mapName)
	{
		if (!g_collisionReady.load(std::memory_order_acquire)) return false;
		if (g_nightCollisionEnabled.load(std::memory_order_acquire) && mapName && night_files::MapEnabled(mapName)) return true;
#ifndef IW8X_CLIENT_ONLY
		return Ready() && RequiredMap(mapName);
#else
		return false;
#endif
	}

	hook::Status InstallCollisionProcessor(uintptr_t moduleBase)
	{
#ifndef IW8X_CLIENT_ONLY
		if (!Enabled() && !night_files::Enabled()) return hook::Status::Installed;
#else
		if (!night_files::Enabled()) return hook::Status::Installed;
#endif
		if (g_collisionReady.load(std::memory_order_acquire))
			return g_collisionBase == moduleBase ? hook::Status::Installed : hook::Status::Failed;

		// Pin both call targets, the glass transient seam, and the bitset/file-index
		// layout to exact Replay bytes.
		struct Guard
		{
			uintptr_t rva;
			std::uint8_t bytes[16];
		};

		const Guard guards[] = {
			{ kCollisionProcessRva, { 0x48, 0x81, 0xEC, 0x98, 0, 0, 0, 0x80, 0x3D, 0xA7, 0xC6, 0xF5, 0x04, 0, 0x74, 0x08 } },
			{ kGlassPieceStateChangedRva, { 0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x80, 0, 0, 0, 0x48, 0x8B } },
			{ kGlassGetSystemForInitialPieceRva,
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x83, 0x3D, 0x84, 0x87, 0x93, 0x0D, 0x01, 0x48, 0x8B, 0xDA, 0x44 } },
			{ game::addresses::runtime::rva_015f71c0,
				{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x0F, 0xB7, 0xD9, 0xE8 } },
			{ game::addresses::runtime::rva_01491980,
				{ 0x8B, 0xC1, 0x48, 0x8D, 0x15, 0xB7, 0xB9, 0x13, 0x0D, 0x48, 0xC1, 0xE8, 0x05, 0x83, 0xE1, 0x1F } },
			{ game::addresses::runtime::rva_0139e9d0,
				{ 0x8B, 0xC1, 0x48, 0x8D, 0x15, 0xC7, 0x20, 0xF3, 0x0C, 0x48, 0xC1, 0xE8, 0x05, 0x83, 0xE1, 0x1F } },
			{ game::addresses::runtime::rva_00d6bbc0,
				{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48 } },
			{ game::addresses::runtime::rva_00d6bbf7,
				{ 0x43, 0x85, 0xBC, 0x3E, 0x60, 0x75, 0xCC, 0x05, 0x0F, 0x95, 0xC1, 0x43, 0x85, 0xBC, 0x3E, 0xE0 } },
			{ game::addresses::runtime::rva_00d6bc15,
				{ 0x41, 0x0F, 0xB7, 0x8C, 0x77, 0x60, 0x7F, 0xCC, 0x05, 0x66, 0x85, 0xC9, 0x74, 0x23, 0xE8, 0x28 } },
			{ game::addresses::runtime::rva_00d6bc46,
				{ 0x43, 0x85, 0xBC, 0x3E, 0x60, 0x76, 0xCC, 0x05, 0x0F, 0x95, 0xC1, 0x43, 0x85, 0xBC, 0x3E, 0xE0 } },
		};
		for (const auto& guard : guards)
		{
			if (!matches_bytes(moduleBase + guard.rva, guard.bytes, sizeof(guard.bytes)))
			{
				LOG_ERR("BAP", "collision mapping mismatch at Replay RVA 0x%llX", static_cast<unsigned long long>(guard.rva));
				return hook::Status::Failed;
			}
		}
		auto* collision = reinterpret_cast<CollisionState*>(moduleBase + kCollisionStateRva);
		// Replay's collision globals straddle multiple VirtualQuery regions. The first
		// writable region ends at +0xB10 from this base, while the verified numbered
		// file-index table extends through +0x1270. Validate the whole range by walking
		// each committed region instead of requiring one MEMORY_BASIC_INFORMATION.
		if (!writable_range(collision, sizeof(*collision)))
		{
			LOG_ERR("BAP", "collision state range is not fully readable/writable @+0x%llX size=0x%zX",
				static_cast<unsigned long long>(kCollisionStateRva), sizeof(*collision));
			return hook::Status::Failed;
		}
		g_collision = collision;
		g_collisionLoad = reinterpret_cast<CollisionLoadFn>(moduleBase + game::addresses::runtime::rva_015f71c0);
		g_collisionSetupWorld = reinterpret_cast<CollisionTileFn>(moduleBase + game::addresses::runtime::rva_01491980);
		g_collisionSetupModels = reinterpret_cast<CollisionTileFn>(moduleBase + game::addresses::runtime::rva_0139e9d0);
		g_collisionUpdate = reinterpret_cast<CollisionUpdateFn>(moduleBase + game::addresses::runtime::rva_00d6bbc0);
		g_glassGetSystemForInitialPiece = reinterpret_cast<GlassGetSystemForInitialPieceFn>(moduleBase + kGlassGetSystemForInitialPieceRva);

		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(moduleBase + kGlassPieceStateChangedRva), &GlassPieceStateChangedDetour,
			kGlassPieceStateChangedBytes, sizeof(kGlassPieceStateChangedBytes), g_glassPieceStateChanged);
		batch.Add(reinterpret_cast<void*>(moduleBase + kCollisionProcessRva), &ProcessCollisionFastfiles, guards[0].bytes,
			sizeof(guards[0].bytes), g_collisionProcess);
		const auto status = batch.Commit();
		if (status != hook::Status::Installed)
		{
			LOG_ERR("BAP", "could not install numbered collision processor detour at Replay RVA 0x%llX (status=%d)",
				static_cast<unsigned long long>(kCollisionProcessRva), static_cast<int>(status));
			return status;
		}

		g_collisionBase = moduleBase;
		g_collisionReady.store(true, std::memory_order_release);
		LOG_INFO("BAP", "glass transient safety guard installed (G_Glass_PieceStateChanged RVA 0x1212B00)");
		LOG_INFO("BAP", "MODE_ALL numbered collision loader installed (Replay RVA 0xD6AEA0)");

		return status;
	}

#ifndef IW8X_CLIENT_ONLY
	hook::Status InstallCollision(uintptr_t moduleBase)
	{
		if (!Enabled()) return hook::Status::Installed;
		if (!Ready() || g_base != moduleBase)
		{
			LOG_ERR("BAP", "collision install requested before core/lifecycle routing was ready");
			return hook::Status::Failed;
		}
		return InstallCollisionProcessor(moduleBase);
	}

#endif

	hook::Status InstallNightCollision(uintptr_t moduleBase)
	{
		// Caller has verified the Night bundle. Sharing only the MP processor does
		// not activate BAP's DB pool/table rewrites in an ordinary client.
		if (!asset_pools::port_ready())
		{
			LOG_ERR("NightCollision", "Night native collision pools were not prepared before engine allocation");
			return hook::Status::Failed;
		}
		const auto status = InstallCollisionProcessor(moduleBase);
		if (status == hook::Status::Installed)
		{
			g_nightCollisionEnabled.store(true, std::memory_order_release);
			LOG_INFO("NightCollision", "admitted port maps use registered numbered collision; cg_ls_tr is not requested");
			LOG_INFO("NightCollision", "native pools allocated before engine startup: physicsasset=96000 xmodeldetailcollision=96000");
		}
		return status;
	}
}
