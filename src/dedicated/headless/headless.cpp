#include "dedicated/headless/headless.h"

#include "engine/addresses.hpp"

#include "client/autostart.h"

#include "common/console.h"

#include "dedicated/dedicated.h"

#include "engine/game.h"

#include "dedicated/headless/d3d.h"
#include "dedicated/headless/non_player.h"

#include "engine/safemem.h"

#include <d3d12.h>

#include <atomic>
#include <cstring>
namespace headless
{
	namespace
	{
		uintptr_t g_base = 0;
		using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
		using CreateAGSDeviceFn = bool(__fastcall*)(IUnknown*, REFIID, D3D_FEATURE_LEVEL, void*);
		std::atomic<CreateDeviceFn> g_createDevice{ nullptr };
		std::atomic<CreateAGSDeviceFn> g_createAGSDevice{ nullptr };
		std::atomic<game::CL_Screen_Update_t> g_screenUpdate{ nullptr };
		std::atomic<game::CL_MainMP_FrameDispatch_t> g_clientFrame{ nullptr };
		using netchan_transmit_fn = int (*)(uintptr_t, int, const void*, uintptr_t);
		std::atomic<netchan_transmit_fn> original_netchan_transmit{ nullptr };
		std::atomic<game::BG_Omnvar_LoadTables_t> g_omnvarLoadTables{ nullptr };
		std::atomic<unsigned> g_omnvarLoadSequence{ 0 };
		using SetLastStandEnabledFn = void (*)(void*, bool);
		std::atomic<SetLastStandEnabledFn> g_setLastStandEnabled{ nullptr };
		constexpr uintptr_t kSetLastStandEnabledRVA = 0x1778e80;
		constexpr uint8_t kSetLastStandEnabledBytes[] = { 0x88, 0x91, 0x39, 0xfa, 0x00, 0x00, 0xc3 };

		void SetLastStandEnabled(void* clientSystem, bool enabled)
		{
			if (!clientSystem && dedicated::Enabled())
			{
				// GScr_SetLastStandEnabled (1207660) validates the player/argument and
				// then writes a local-client UI byte through F24EB10[0]. EFC760 allocates
				// that system per rendered client; a headless host has none. The setter
				// only writes this+FA39, so there is no server state to replace here.
				static std::atomic<bool> logged{ false };
				if (!logged.exchange(true))
					LOG_INFO("Headless", "skipping last-stand client UI update: dedicated has no local client system");
				return;
			}
			g_setLastStandEnabled.load(std::memory_order_acquire)(clientSystem, enabled);
		}

		int ReadOmnvarCounter(uintptr_t rva)
		{
			int value = -1;
			safemem::ReadBytes(reinterpret_cast<const void*>(g_base + rva), &value, sizeof(value));
			return value;
		}

		void __fastcall DetourOmnvarLoadTables(unsigned char gameMode, const char* gameType, const char* mapName)
		{
			const bool autostarted = dedicated::Enabled() || autostart::Complete();
			const unsigned sequence = g_omnvarLoadSequence.fetch_add(1, std::memory_order_relaxed) + 1;
			int total = ReadOmnvarCounter(game::kOmnvarTotalCountRVA);
			const int gameCount = ReadOmnvarCounter(game::kOmnvarGameCountRVA);
			const int clientCount = ReadOmnvarCounter(game::kOmnvarClientCountRVA);
			const int archived = ReadOmnvarCounter(game::kOmnvarArchivedCountRVA);
			const int pmove = ReadOmnvarCounter(game::kOmnvarPmoveCountRVA);

			if (autostarted)
			{
				LOG_INFO("Omnvar", "full table load #%u begins: mode=%u gametype=%s map=%s total=%d game=%d client=%d archived=%d pmove=%d",
					sequence, static_cast<unsigned>(gameMode), gameType ? gameType : "<null>", mapName ? mapName : "<null>", total,
					gameCount, clientCount, archived, pmove);

				// BG_Omnvar_LoadTables is a complete table build: it always starts with
				// omnvars/omnvars.csv and then loads the selected mode/gametype/level.
				// It has no duplicate suppression.  A second full load with a non-empty
				// table therefore duplicates definitions until Replay hits its MP count
				// limits.  Stock mode transitions clear the tables separately, so
				// preserve that invariant during unattended server startup.
				if (total > 0)
				{
					LOG_WARN("Omnvar", "full table load #%u entered with total=%d; clearing previous Omnvar table before stock reload",
						sequence, total);
					reinterpret_cast<game::BG_Omnvar_ClearTables_t>(g_base + game::kBGOmnvarClearTablesRVA)();
					total = ReadOmnvarCounter(game::kOmnvarTotalCountRVA);
					LOG_INFO("Omnvar", "full table load #%u clear completed: total=%d", sequence, total);
				}
			}

			g_omnvarLoadTables.load(std::memory_order_acquire)(gameMode, gameType, mapName);

			if (autostarted)
			{
				LOG_INFO("Omnvar", "full table load #%u completed: total=%d game=%d client=%d archived=%d pmove=%d archive_bits=%d",
					sequence, ReadOmnvarCounter(game::kOmnvarTotalCountRVA), ReadOmnvarCounter(game::kOmnvarGameCountRVA),
					ReadOmnvarCounter(game::kOmnvarClientCountRVA), ReadOmnvarCounter(game::kOmnvarArchivedCountRVA),
					ReadOmnvarCounter(game::kOmnvarPmoveCountRVA), ReadOmnvarCounter(game::kOmnvarArchivedBitsRVA));
			}
		}

		int transmit_local_client_packet(uintptr_t channel, int length, const void* data, uintptr_t extra)
		{
			if (dedicated::Enabled() && NonPlayerHost())
			{
				for (int client = 0; client < 2; ++client)
				{
					uintptr_t connection{}, transport{};
					const auto pointer = g_base + game::kClient0ConnectionRVA + client * game::kClientConnectionPointerStride;
					if (safemem::ReadBytes(reinterpret_cast<const void*>(pointer), &connection, sizeof(connection)) && connection &&
						channel == connection + game::kClientConnectionNetchanOffset &&
						safemem::ReadBytes(reinterpret_cast<const void*>(channel + game::kNetchanConnectionOffset), &transport, sizeof(transport)) &&
						!transport)
					{
						// Full map restart advances the placeholder to CA_CONNECTED.
						// It has no player slot/NetConnection and cannot send usercmds.
						// Return no transmitted bytes, like the native send-failure path;
						// retain client frame/loading updates and all remote channels.
						static std::atomic<unsigned> suppressed{ 0 };
						const auto count = suppressed.fetch_add(1, std::memory_order_relaxed) + 1;
						if (count <= 8)
							LOG_INFO("Headless", "skipped local client %d packet: non-player dedicated host has no NetConnection (count %u)", client, count);
						return 0;
					}
				}
			}
			return original_netchan_transmit.load(std::memory_order_acquire)(channel, length, data, extra);
		}

		uintptr_t __fastcall DetourClientFrame(int localClientNum)
		{
			// Replay's normal CL_ConnectAndPreloadMap path calls GameAllocate first and
			// only then changes the local connection state from 0 to 1.  The observed
			// post-StartMatch crash violates that invariant: state > 0 while the
			// per-client connection object is null.  Restore the stock idle state at
			// the frame boundary, before any virtual MP-client code can dereference the
			// absent object.  The stock connect/preload path will set state 1 again
			// after it has allocated and initialized the connection object.
			if ((dedicated::Enabled() || autostart::Complete()) && localClientNum >= 0 && localClientNum < 2)
			{
				int state = 0;
				uintptr_t connection = 0;
				const uintptr_t stateAddress =
					g_base + game::kClient0ConnectionStateRVA + static_cast<uintptr_t>(localClientNum) * game::kClientConnectionStateStride;
				const uintptr_t pointerAddress =
					g_base + game::kClient0ConnectionRVA + static_cast<uintptr_t>(localClientNum) * game::kClientConnectionPointerStride;
				if (safemem::ReadBytes(reinterpret_cast<const void*>(stateAddress), &state, sizeof(state)) &&
					safemem::ReadBytes(reinterpret_cast<const void*>(pointerAddress), &connection, sizeof(connection)) && state > 0 &&
					!connection)
				{
					const int idle = 0;
					if (safemem::WriteBytes(reinterpret_cast<void*>(stateAddress), &idle, sizeof(idle)))
					{
						static std::atomic<unsigned> repairs{ 0 };
						const unsigned count = repairs.fetch_add(1, std::memory_order_relaxed) + 1;
						if (count <= 8 || (count % 120) == 0)
						{
							LOG_WARN("Headless",
								"repaired local client %d lifecycle before MP frame: state=%d connection=null -> state=0 (repair %u)",
								localClientNum, state, count);
						}
					}
				}
			}

			return g_clientFrame.load(std::memory_order_acquire)(localClientNum);
		}

		HRESULT WINAPI DetourCreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** output)
		{
#ifdef IW8X_ISLAND_PORT_PROBE
			d3d::EnableProbeDiagnostics();
#endif
			const HRESULT result = g_createDevice.load(std::memory_order_acquire)(adapter, level, iid, output);
			if (SUCCEEDED(result) && output && *output && !d3d::AttachDevice(*output))
			{
				static_cast<IUnknown*>(*output)->Release();
				*output = nullptr;
				return E_FAIL;
			}
			return result;
		}

		bool __fastcall DetourCreateAGSDevice(IUnknown* adapter, REFIID iid, D3D_FEATURE_LEVEL level, void* output)
		{
#ifdef IW8X_ISLAND_PORT_PROBE
			d3d::EnableProbeDiagnostics();
#endif
			const bool result = g_createAGSDevice.load(std::memory_order_acquire)(adapter, iid, level, output);
			if (result && output)
			{
				void* device = nullptr;
				std::memcpy(&device, output, sizeof(device));
				// Preserve AGS ownership; do not make Replay create a second device
				// over a successful result. Attach failures are explicitly logged.
				d3d::AttachDevice(device);
			}
			return result;
		}

		void DetourScreenUpdate()
		{
			// This also advances LUI, loading, client allocation and worker lifetimes.
			// A party start-request bit is not proof that those updates can stop.
			const auto isMainThread = reinterpret_cast<game::Sys_IsMainThread_t>(g_base + game::kSysIsMainThreadRVA);
			const bool mainThread = isMainThread();

			// Pump before the stock screen update.  UI_StartServer can move the local
			// client into a connecting state immediately, while the connection object
			// is allocated by the native frontend/loading transition.  Pumping after
			// CL_Screen_Update leaves one Com_Frame where CheckForResend can observe
			// state > 0 with a null connection pointer.
			if (mainThread && !dedicated::Enabled()) autostart::Pump();

			g_screenUpdate.load(std::memory_order_acquire)();

			if (!mainThread) return;

			// The stock frame already includes Com_DoMaxFPSWait. Report actual
			// suppression separately from hook installation, without per-draw logs.
			static ULONGLONG nextReport = 0;
			const ULONGLONG now = GetTickCount64();
			if (now >= nextReport)
			{
				nextReport = now + 30000;
				const auto count = d3d::ReadCounters();
				LOG_INFO("Headless",
					"filtered commands: lists=%llu draw=%llu dispatch=%llu indirect=%llu clear=%llu rays=%llu; native frame updates active",
					count.commandLists, count.draws, count.dispatches, count.indirect, count.clears, count.rays);
				int connectionState = -1;
				uintptr_t connection = 0;
				safemem::ReadBytes(
					reinterpret_cast<void*>(g_base + game::kClient0ConnectionStateRVA), &connectionState, sizeof(connectionState));
				safemem::ReadBytes(reinterpret_cast<void*>(g_base + game::kClient0ConnectionRVA), &connection, sizeof(connection));
				const bool serverRunning = reinterpret_cast<bool (*)()>(g_base + game::kIsAnyLocalServerRunningRVA)();
				LOG_INFO("Headless", "host status: server_running=%d client_state=%d connection_allocated=%d start_requested=%d",
					serverRunning, connectionState, connection != 0,
					dedicated::Enabled() ? dedicated::StartRequested() : autostart::Complete());
			}
		}
	}

	hook::Status InstallDeviceHooks(uintptr_t moduleBase)
	{
		auto status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kCreateD3DDeviceRVA), &DetourCreateDevice,
			game::kCreateD3DDevicePrologue, sizeof(game::kCreateD3DDevicePrologue), g_createDevice);
		if (status != hook::Status::Installed) return status;
		return hook::Install(reinterpret_cast<void*>(moduleBase + game::kCreateAGSD3DDeviceRVA), &DetourCreateAGSDevice,
			game::kCreateAGSD3DDevicePrologue, sizeof(game::kCreateAGSD3DDevicePrologue), g_createAGSDevice);
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (g_screenUpdate.load(std::memory_order_acquire)) return hook::Status::Installed;

		uint8_t mainThreadBytes[sizeof(game::kSysIsMainThreadPrologue)]{};
		if (!safemem::ReadBytes(
				reinterpret_cast<void*>(moduleBase + game::kSysIsMainThreadRVA), mainThreadBytes, sizeof(mainThreadBytes)) ||
			std::memcmp(mainThreadBytes, game::kSysIsMainThreadPrologue, sizeof(mainThreadBytes)) != 0)
			return hook::Status::NotReady;
		uint8_t serverBytes[sizeof(game::kIsAnyLocalServerRunningPrologue)]{};
		if (!safemem::ReadBytes(
				reinterpret_cast<void*>(moduleBase + game::kIsAnyLocalServerRunningRVA), serverBytes, sizeof(serverBytes)) ||
			std::memcmp(serverBytes, game::kIsAnyLocalServerRunningPrologue, sizeof(serverBytes)) != 0)
			return hook::Status::NotReady;

		g_base = moduleBase;
		auto status = InstallNonPlayer(moduleBase); // Dedicated session owner has no player slot.
		if (status != hook::Status::Installed) return status;
		status = InstallDeviceHooks(moduleBase);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(moduleBase + kSetLastStandEnabledRVA), &SetLastStandEnabled,
			kSetLastStandEnabledBytes, sizeof(kSetLastStandEnabledBytes), g_setLastStandEnabled);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kBGOmnvarLoadTablesRVA), &DetourOmnvarLoadTables,
			game::kBGOmnvarLoadTablesPrologue, game::kBGOmnvarLoadTablesStolen, g_omnvarLoadTables);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kNetchanTransmitRVA), &transmit_local_client_packet,
			game::kNetchanTransmitPrologue, sizeof(game::kNetchanTransmitPrologue), original_netchan_transmit);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kCLMainMPFrameDispatchRVA), &DetourClientFrame,
			game::kCLMainMPFrameDispatchPrologue, game::kCLMainMPFrameDispatchStolen, g_clientFrame);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(moduleBase + game::kCLScreenUpdateRVA), &DetourScreenUpdate,
			game::kCLScreenUpdatePrologue, game::kCLScreenUpdateStolen, g_screenUpdate);
		if (status == hook::Status::Installed)
			LOG_INFO("Headless",
				"startup D3D12 command filtering armed; MP client lifecycle guard + Omnvar full-load lifecycle repair active; stock screen/loading/streaming updates preserved; D3D12 device required");
		return status;
	}
}
