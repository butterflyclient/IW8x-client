#include "network/backend/backend_agent.h"
#include "network/social/roster_memory.h"
#include "network/backend/native_matchmaking.h"
#include "network/backend/frontend_backend.h"
#include "auth/profile/local_stats.h"
#include "network/backend/native_mm_backend.h"
#include "network/backend/native_mm_bindings.h"
#include "network/backend/join_cleanup_gate.hpp"
#include "network/social/social_runtime.h"
#include "network/online_services.h"
#include "network/social/party_transport.h"
#include "auth/offline_auth.h"
#include "client/online_frontend.h"
#include "network/playlists.h"
#include "network/client_playlist_map.hpp"
#include "engine/game.h"
#include "common/console.h"
#include "engine/commands.h"
#include "engine/safemem.h"
#include "diagnostics/night/night_files.h"
#include <bcrypt.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <charconv>
#include <filesystem>
#include <fstream>
namespace native_matchmaking
{
	namespace
	{
		using native_mm::Json;
		uintptr_t base = 0, handler = 0;
		bool installed = false, server = false, enabled = false;
		constexpr uintptr_t kMatchmaker = 0xC27E4B0;
#ifndef IW8X_CLIENT_ONLY
		native_mm::Backend backend;
#endif
		std::mutex backendLock;
		bool tokenStorageReady = true;
		using AssignTeamFn = uint64_t (*)(uintptr_t, unsigned, int, char);
		std::atomic<AssignTeamFn> originalAssignTeam{ nullptr };
#ifndef IW8X_CLIENT_ONLY

		uint64_t assign_team(uintptr_t party, unsigned controller, int member, char human)
		{
			if (server && human && member >= 0 && member < 200 && party == base + ServerPartyRVA())
			{
				uint8_t status = 0;
				uint64_t id = 0;
				if (safemem::ReadBytes(reinterpret_cast<void*>(party + 488ull * member + 2594), &status, 1) && status == 5 &&
					safemem::ReadBytes(reinterpret_cast<void*>(party + 488ull * member + 2600), &id, sizeof(id)))
					if (const int team = online_services::TournamentTeam(id)) return uint64_t(team);
			}
			return originalAssignTeam.load()(party, controller, member, human);
		}
#endif

#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/native_matchmaking_save_tokens.inl"
#endif
		uint64_t generation = 0, matchId = 0, nextPoll = 0;
		bool pollPending = false, delivered = false;
			bool sawGameplay = false, joinedBR = false, islandLobbyMapPinned = false;
			int islandPinnedPlaylist = -1;
			ULONGLONG islandResyncWarnAt = 0;
		uint64_t playedSession = 0;
		using InfoFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, unsigned);
		using TokenFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t);
		using StartFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, unsigned, uintptr_t);
		using StopFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uint64_t, uintptr_t);
		using ReadyFn = bool (*)(uintptr_t, int);
		using RegisterFn = bool (*)(uintptr_t, uintptr_t);
		using BoolFn = bool (*)();
		using QosFn = bool (*)(unsigned);
		using CountFn = unsigned (*)();
		std::atomic<InfoFn> originalInfo{ nullptr };
		std::atomic<TokenFn> originalToken{ nullptr };
		std::atomic<StartFn> originalStart{ nullptr };
		std::atomic<StopFn> originalCancel{ nullptr }, originalShutdown{ nullptr };
		std::atomic<ReadyFn> originalReady{ nullptr };
		std::atomic<RegisterFn> originalRegister{ nullptr };
		std::atomic<BoolFn> originalQosComplete{ nullptr };
		std::atomic<QosFn> originalQosSuccess{ nullptr };
		std::atomic<CountFn> originalQosCount{ nullptr };
		std::atomic<BoolFn> originalJoinModeCleanup{ nullptr };
		constexpr uintptr_t kJoinModeCleanupRva = 0xEFC980;
		constexpr uintptr_t kJoinModeStateRva = 0xEEB0B20 + 0x6B0;
		constexpr uintptr_t kJoinModeObjectRva = 0xF270FA0;
		constexpr uintptr_t kJoinModeListRva = 0xF24EB10;
		constexpr uintptr_t kJoinModeListCountRva = 0xF24EB18;
		constexpr uintptr_t kJoinModeListStateRva = 0xF24EB1C;

		bool join_cleanup_entry_valid(std::uintptr_t object) noexcept
		{
			std::uintptr_t vtable = 0, release = 0;
			return object && safemem::ReadBytes(reinterpret_cast<const void*>(object), &vtable, sizeof(vtable)) && vtable &&
				vtable <= UINTPTR_MAX - 0x70 &&
				safemem::ReadBytes(reinterpret_cast<const void*>(vtable + 0x70), &release, sizeof(release)) && release &&
				safemem::RegionExecutable(reinterpret_cast<const void*>(release));
		}

		bool join_mode_cleanup()
		{
			// EFCB05, EFCB55 and EFCB95 share F24EB10[0], F24EB18
			// (count), and F24EB1C (state). After disconnect, the next
			// MP -> Warzone transition can run with delivered=false and a
			// stale object. Validate even after matchmaking has shut down.
			std::uint8_t listState = 0;
			std::int32_t count = 0;
			std::uintptr_t entry = 0;
			if (safemem::ReadBytes(reinterpret_cast<const void*>(base + kJoinModeListStateRva), &listState, sizeof(listState)) &&
				safemem::ReadBytes(reinterpret_cast<const void*>(base + kJoinModeListCountRva), &count, sizeof(count)) &&
				safemem::ReadBytes(reinterpret_cast<const void*>(base + kJoinModeListRva), &entry, sizeof(entry)) &&
				join_cleanup_gate::reset_invalid_list(listState, count, count == 1 && join_cleanup_entry_valid(entry)))
			{
				// The native list has space for ONE pointer. Clear count
				// before slot, so a partial write cannot leave a live count
				// pointing at a stale object. Leave all valid entries alone.
				if (safemem::RegionWritable(reinterpret_cast<void*>(base + kJoinModeListRva)) &&
					safemem::RegionWritable(reinterpret_cast<void*>(base + kJoinModeListCountRva)))
				{
					const std::int32_t zero = 0;
					const std::uintptr_t empty = 0;
					if (safemem::WriteBytes(reinterpret_cast<void*>(base + kJoinModeListCountRva), &zero, sizeof(zero)))
					{
						if (!safemem::WriteBytes(reinterpret_cast<void*>(base + kJoinModeListRva), &empty, sizeof(empty)))
							LOG_WARN("NativeMM", "cleanup-list count cleared, but stale slot could not be cleared");
						LOG_WARN("NativeMM", "discarded invalid mode-transition cleanup entry state=%u count=%d object=%p (EFCB05/EFCB55/EFCB95)",
							unsigned(listState), count, reinterpret_cast<void*>(entry));
					}
				}
			}
			// In native mode !=1, EFCB23 dereferences [F270FA0+28]
			// without a null test. No singleton means no object to release,
			// even on the later mode switch when delivered is no longer set.
			std::uint8_t state = 0;
			std::uintptr_t object = 0;
			if (safemem::ReadBytes(reinterpret_cast<const void*>(base + kJoinModeStateRva), &state, sizeof(state)) &&
				safemem::ReadBytes(reinterpret_cast<const void*>(base + kJoinModeObjectRva), &object, sizeof(object)) &&
				join_cleanup_gate::skip(state, object))
			{
				LOG_WARN("NativeMM", "skipped absent frontend cleanup singleton during mode transition (EFCB23)");
				return true;
			}
			return originalJoinModeCleanup.load(std::memory_order_acquire)();
		}

		template <class F>
		F native(uintptr_t rva)
		{
			return reinterpret_cast<F>(base + rva);
		}

		template <class T>
		T read(uintptr_t address)
		{
			T v{};
			safemem::ReadBytes(reinterpret_cast<void*>(address), &v, sizeof(v));
			return v;
		}

		template <class T>
		void write(uintptr_t address, T value)
		{
			memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		}

		void release(uintptr_t object)
		{
			if (_InterlockedDecrement(reinterpret_cast<volatile long*>(object + 8)) == 0)
				reinterpret_cast<void (*)(uintptr_t, unsigned)>(read<uintptr_t>(read<uintptr_t>(object)))(object, 1);
		}

		// Use actual bdRemoteTask objects. The game's TaskManager retains its own
		// reference and invokes its original callbacks only after the service replies.
		uintptr_t* task(uintptr_t* out, const char* operation, uintptr_t result, Json body)
		{
			*out = 0;
			const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
			if (!object) return out;
			native<uintptr_t (*)(uintptr_t)>(0x20FDEF0)(object);
			write<long>(object + 8, 2);
			write<float>(object + 24, 10.0f);
			write<int>(object + 28, 1);
			body["op"] = operation;
			const std::string op = operation;
			const auto epoch = generation;
			const bool submitted = social_runtime::SubmitMatchmaking(body.dump(),
				[object, result, op, epoch](unsigned status, std::string text)
				{
					bool success = status == 200 && read<int>(object + 28) == 1;
					try
					{
						const auto response = Json::parse(text);
						if (success && op == "token")
						{
							const auto token = native_mm::Number(response.at("token"));
							success = token && result;
							if (success) write<uint64_t>(result + 16, token);
						}
						else if (status == 200 && op == "start")
						{
							const auto id = native_mm::Number(response.at("mm_id"));
							const auto buffer = read<uintptr_t>(result + 32);
							const auto capacity = read<unsigned>(result + 40);
							const auto document = Json{ { "mm_id", id } }.dump();
							success = success && id && epoch == generation && buffer && capacity > document.size() && capacity <= 16384;
							if (success)
							{
								memcpy(reinterpret_cast<void*>(buffer), document.c_str(), document.size() + 1);
								matchId = id;
								delivered = false;
								nextPoll = 0;
							}
							else if (id)
							{
								social_runtime::SubmitMatchmaking(Json{ { "op", "cancel" }, { "mm_id", id } }.dump(),
									[](unsigned, std::string)
									{
									});
							}
						}
						else if (success)
							success = response.value("accepted", false);
					}
					catch (...)
					{
						success = false;
					}
					if (read<int>(object + 28) == 1)
					{
						write<unsigned>(object + 80, success ? 0 : 5);
						write<int>(object + 28, success ? 2 : 3);
					}
					LOG_INFO("NativeMM", "%s task completed http=%u success=%d", op.c_str(), status, int(success));
					release(object);
				});
			if (!submitted)
			{
				release(object);
				release(object);
				return out;
			}
			*out = object;
			LOG_INFO("NativeMM", "%s task submitted", operation);
			return out;
		}

		Json document_impl(const char* text, unsigned length)
		{
			if (!text || !length || length > 16384) return {};
			std::string copy(length, '\0');
			if (!safemem::ReadBytes(text, copy.data(), length)) return {};
			if (copy.back() == 0) copy.pop_back();
			return Json::parse(copy, nullptr, false);
		}

		uintptr_t* info(uintptr_t self, uintptr_t* out, const char* data, unsigned length)
		{
			if (!Enabled()) return originalInfo.load()(self, out, data, length);
			auto doc = document_impl(data, length);
			if (!doc.is_object())
			{
				*out = 0;
				return out;
			}
			return task(out, "info", 0, Json{ { "document", doc } });
		}

		uintptr_t* token_impl(uintptr_t self, uintptr_t* out, uintptr_t result)
		{
			if (!Enabled()) return originalToken.load()(self, out, result);
			return task(out, "token", result, Json::object());
		}

		uintptr_t* start(uintptr_t self, uintptr_t* out, const char* data, unsigned length, uintptr_t result)
		{
			if (!Enabled()) return originalStart.load()(self, out, data, length, result);
			if (online_frontend::local_play())
			{
				*out = 0;
				return out;
			}
			auto doc = document_impl(data, length);
			if (!doc.is_object())
			{
				*out = 0;
				return out;
			}
			++generation;
			matchId = 0;
			night_files::ClearAuthenticatedIslandClientMatch();
				islandLobbyMapPinned = false;
				islandPinnedPlaylist = -1;
			delivered = false;
			pollPending = false;
			sawGameplay = false;
			playedSession = 0;
			party_transport::ClearDedicatedRoute();
			LOG_INFO("NativeMM", "native search requested with %zu token(s)", doc.value("tokens", Json::array()).size());
			return task(out, "start", result, Json{ { "document", doc } });
		}

		uintptr_t* stop(uintptr_t self, uintptr_t* out, uint64_t id, uintptr_t result)
		{
			if (!Enabled()) return originalCancel.load()(self, out, id, result);
			++generation;
			matchId = 0;
			night_files::ClearAuthenticatedIslandClientMatch();
				islandLobbyMapPinned = false;
				islandPinnedPlaylist = -1;
			delivered = false;
			pollPending = false;
			party_transport::ClearDedicatedRoute();
			return task(out, "cancel", result, Json{ { "mm_id", id } });
		}

		uintptr_t* shutdown(uintptr_t self, uintptr_t* out, uint64_t id, uintptr_t result)
		{
			if (!Enabled()) return originalShutdown.load()(self, out, id, result);
			++generation;
			matchId = 0;
			night_files::ClearAuthenticatedIslandClientMatch();
				islandLobbyMapPinned = false;
				islandPinnedPlaylist = -1;
			delivered = false;
			pollPending = false;
			party_transport::ClearDedicatedRoute();
			return task(out, "shutdown", result, Json{ { "mm_id", id } });
		}

		bool ready(uintptr_t self, int controller)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			if (Enabled() && controller == 0 && caller >= 0x1046E10 && caller < 0x1047280)
				return !online_frontend::local_play() && social_runtime::BackendConnected() && local_stats::RemoteReady();
			return originalReady.load()(self, controller);
		}

		bool register_impl(uintptr_t self, uintptr_t eventHandler)
		{
			if (!Enabled() || eventHandler != base + kMatchmaker + 1071104) return originalRegister.load()(self, eventHandler);
			if (!social_runtime::BackendConnected()) return false;
			handler = eventHandler;
			LOG_INFO("NativeMM", "native matchmaking event handler registered");
			return true;
		}

		bool qos_complete()
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			// This backend has one explicitly configured dedicated endpoint, not a
			// retail data-center pool. Its live authenticated HTTP lease is the
			// reachability result. Do not populate or mark unrelated retail QoS caches.
			if (Enabled() && caller >= 0x15FC110 && caller < 0x15FC310) return social_runtime::BackendConnected();
			return originalQosComplete.load()();
		}

		bool qos_success(unsigned type)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			if (Enabled() && type == 2 && caller >= 0x15FC5E0 && caller < 0x15FC850) return social_runtime::BackendConnected();
			return originalQosSuccess.load()(type);
		}

		unsigned qos_count()
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			if (Enabled() && caller >= 0x15FC5E0 && caller < 0x15FC850) return social_runtime::BackendConnected() ? 1 : 0;
			return originalQosCount.load()();
		}
	}

	bool Enabled()
	{
		return enabled && !server && installed;
	}

	bool UseOnlineLobby()
	{
		return enabled && installed && (server || !online_frontend::local_play());
	}

	uintptr_t ServerPartyRVA()
	{
		return UseOnlineLobby() ? 0xC73ADE0 : 0xC796480;
	}

	bool InMatch()
	{
		return Enabled() && read<int>(base + game::kClient0ConnectionStateRVA) == 9 && !native<bool (*)()>(game::kIsInFrontEndRVA)() &&
			read<int>(base + 0xC73ADE0 + 311116) && !read<int>(base + 0xC73ADE0 + 311112) && read<int>(base + 0xC73ADE0 + 311000) == -2;
	}

	bool Joining()
	{
		return Enabled() && delivered && !InMatch();
	}

	bool StartCountdown(int& milliseconds)
	{
		if (!Joining() || !native<bool (*)()>(game::kIsInFrontEndRVA)()) return false;
		const auto party = base + 0xC73ADE0;
		// Native GetPartyState1608630 tests the client's playlist minimum before
		// the received-go flag. A dedicated host can legitimately start with fewer
		// players. Read only its real go/deadline, also used by countdown audio at
		// 1604AE0; never modify party state or the configured minimum to show text.
		if (read<int>(party + 311112) || !read<int>(party + 311116) || !read<int>(party + 311120) || read<int>(party + 311000) != -2 ||
			!read<uint8_t>(party + 311008))
			return false;
		const auto deadline = native<unsigned (*)(uintptr_t)>(0x118C410)(party);
		if (!deadline) return false;
		milliseconds = int(deadline - unsigned(native<uint64_t (*)()>(0x148FCE0)()));
		return milliseconds >= -30000 && milliseconds <= 600000;
	}

	std::string GameSession()
	{
		if (!InMatch()) return {};
		const auto session = read<uintptr_t>(base + 0xC73ADE0);
		std::array<char, 8> id{};
		if (!session || !safemem::ReadBytes(reinterpret_cast<void*>(session + 88), id.data(), id.size())) return {};
		return matchmaking::Hex(std::string_view(id.data(), id.size()));
	}

	void Configure(bool value)
	{
		enabled = value;
	}
#ifndef IW8X_CLIENT_ONLY

	void ServerPump()
	{
		if (!server || !UseOnlineLobby()) return;
		static uint64_t next = 0;
		const auto now = GetTickCount64();
		if (now < next) return;
		next = now + 250;
		party_roster::Memory memory;

		// Public GameParty sleeps on clients after they enter gameplay. Devmap's
		// dedicated host remains awake to admit more players, so its lobby timeout
		// must observe actual gameplay traffic rather than wait for lobby heartbeats.
		// Never refresh on a timer alone: disconnected clients still expire.
		struct Seen
		{
			uintptr_t client = 0;
			uint64_t uid = 0;
			int packet = 0;
		};

		static std::array<Seen, 200> seen{};
		const auto party = base + ServerPartyRVA();
		if (!memory.Read<int>(party + 311112) || !memory.Read<int>(party + 311116))
		{
			seen = {};
			return;
		}
		const int count = std::clamp(memory.Read<int>(base + 0xE17FCD0), 0, 200);
		for (int i = 0; i < count; ++i)
		{
			const auto client = memory.Read<uintptr_t>(base + 0xE17F690 + 8ull * i);
			if (!client || !memory.Contains(client, 214760))
			{
				seen[i] = {};
				continue;
			}
			auto value = [client]<class T>(size_t offset)
			{
				T v{};
				memcpy(&v, reinterpret_cast<void*>(client + offset), sizeof(v));
				return v;
			};
			if (value.operator()<uint8_t>(8) < 3 || value.operator()<int>(214756) == 2)
			{
				seen[i] = {};
				continue;
			}
			std::array<char, 32> guid{};
			memcpy(guid.data(), reinterpret_cast<void*>(client + 214724), guid.size());
			uint64_t uid = 0;
			const auto end = std::find(guid.begin(), guid.end(), '\0');
			const auto parsed = std::from_chars(guid.data(), guid.data() + (end - guid.begin()), uid);
			if (!uid || parsed.ec != std::errc{} || parsed.ptr != guid.data() + (end - guid.begin())) continue;
			const int packet = value.operator()<int>(0x2FEB4);
			auto& prior = seen[i];
			const bool advanced = prior.client == client && prior.uid == uid && prior.packet != packet;
			prior = { client, uid, packet };
			if (!advanced) continue;
			const auto member = party + 488ull * i;
			if (memory.Read<uint64_t>(member + 2600) != uid || memory.Read<uint8_t>(member + 2594) != 5) continue;
			write<int>(member + 2456, int(native<uint64_t (*)()>(0x148FCE0)()));
		}
	}
#endif


	bool Searching()
	{
		if (!Enabled()) return false;
		const int state = read<int>(base + kMatchmaker);
		return state >= 3 && state <= 7 && !delivered;
	}

	void Cancel()
	{
		if (!Enabled()) return;
		const auto id = matchId;
		++generation;
		matchId = 0;
		night_files::ClearAuthenticatedIslandClientMatch();
			islandLobbyMapPinned = false;
			islandPinnedPlaylist = -1;
		delivered = false;
		pollPending = false;
		party_transport::ClearDedicatedRoute();
		if (id)
			social_runtime::SubmitMatchmaking(Json{ { "op", "cancel" }, { "mm_id", id } }.dump(),
				[](unsigned, std::string)
				{
				});
		native<void (*)(uintptr_t)>(0x10489A0)(base + kMatchmaker);
	}

	void Pump()
	{
		if (!Enabled() || online_frontend::local_play()) return;
		const auto party = base + 0xC73ADE0;
		const auto session = read<uintptr_t>(party);
		if (InMatch())
		{
				// Stop pinning lobby selection once gameplay takes ownership.
				islandLobbyMapPinned = false;
				islandPinnedPlaylist = -1;
			if (!sawGameplay)
			{
				playedSession = session ? read<uint64_t>(session + 88) : 0;
				sawGameplay = true;
			}
		}
		else if (sawGameplay && native<bool (*)()>(game::kIsInFrontEndRVA)() && native<bool (*)()>(game::kIsFrontEndRVA)() &&
			read<int>(base + 0xC7F1870) == 0)
		{
			// The native results/scoreboard and exitlevel have already completed.
			// Retire only the dedicated lobby we actually played, before its host
			// timeout reports an error. Stock xstoplobbybackout keeps PrivateParty
			// and performs native lobby/member/UI cleanup (Party_Backout1189590).
			const bool same = playedSession && session && read<uint64_t>(session + 88) == playedSession && read<int>(party + 311116) &&
				!read<int>(party + 311112) && read<int>(party + 311000) == -2;
			if (!same)
				sawGameplay = false;
			else if (commands::Queue(base, joinedBR ? "set QTQRQPLNK 1;set MLQNQTRRTK 1;xstoplobbybackout\n" : "xstoplobbybackout\n"))
			{
				sawGameplay = false;
				playedSession = 0;
				Cancel();
				LOG_INFO("NativeMM", "completed gameplay lobby retired through native backout; BR=%d", int(joinedBR));
			}
		}
		static int oldState = -1, oldLobby = -1;
		const int state = read<int>(base + kMatchmaker), lobby = read<int>(base + kMatchmaker + 4);
			// Replay can rerun stock playlist rules during native JoinLobby and
			// restore Donetsk after our correctly ordered initial pre-join sync.
			// Keep the exact authenticated Island map selected only throughout
			// frontend lobby/preload; never override gameplay or unrelated maps.
			if (islandLobbyMapPinned &&
				client_playlist_map::ShouldMaintainIslandLobbyMap("mp_sm_island_1", "br",
					islandPinnedPlaylist, delivered, native<bool (*)()>(game::kIsInFrontEndRVA)(),
					state, lobby))
			{
				if (!playlists::MaintainAssignedClientLobbyMap(islandPinnedPlaylist, "mp_sm_island_1", "br") &&
					GetTickCount64() >= islandResyncWarnAt)
				{
					islandResyncWarnAt = GetTickCount64() + 1000;
					LOG_WARN("NativeMM", "authenticated Island lobby ui_mapname resync failed; native preload may select wrong map");
				}
			}
		if (state != oldState || lobby != oldLobby)
		{
			oldState = state;
			oldLobby = lobby;
			LOG_INFO("NativeMM", "native state=%d lobby=%d controller=%d", state, lobby, read<int>(base + kMatchmaker + 8));
		}
		if (!matchId || !handler || delivered || pollPending || state != 7 || GetTickCount64() < nextPoll) return;
		const auto id = matchId, epoch = generation;
		nextPoll = GetTickCount64() + 500;
		pollPending = social_runtime::SubmitMatchmaking(Json{ { "op", "poll" }, { "mm_id", id } }.dump(),
			[id, epoch](unsigned status, std::string text)
			{
				if (epoch != generation || id != matchId || delivered) return;
				pollPending = false;
				if (status == 401 || status == 403 || status == 404 || status == 409)
				{
					LOG_WARN("NativeMM", "search retired after backend rejected current party http=%u", status);
					Cancel();
					return;
				}
				try
				{
					auto response = Json::parse(text);
					if (status != 200 || native_mm::Number(response.at("mm_id")) != id || !response.contains("lobby_id")) return;
					if (read<int>(base + kMatchmaker) != 7 || read<uint64_t>(base + kMatchmaker + 79280) != id) return;
					const auto lobby = native_mm::Number(response.at("lobby_id"));
					const auto host = response.at("host").dump(), backendDoc = response.at("backend").dump();
					if (!lobby || host.size() > 16384 || backendDoc.size() > 16384) return;
						const auto& assignedMap=response.at("map").get_ref<const std::string&>();
						const auto& assignedMode=response.at("mode").get_ref<const std::string&>();
						const int assignedPlaylist=response.at("playlist").get<int>();
						if (!playlists::PrepareClientJoin(assignedPlaylist, assignedMap, assignedMode)) return;
						// Native matchmaking's authenticated dedicated assignment says
						// mp_sm_island_1/br while the stock 1.20 playlist's internal
						// map index still names mp_donetsk. Select the actual Island BR
						// shared family *before* the engine JoinLobby/NetConstString
						// initialization. Other maps/modes stay unmodified.
						const bool islandClientAssigned=night_files::SelectAuthenticatedIslandClientMatch(
							assignedMap,assignedMode,assignedPlaylist);
					matchmaking::Endpoint route;
					if (response.contains("route") && matchmaking::ReadEndpoint(response.at("route"), route))
						party_transport::DedicatedRoute(route.host, route.port);
						// The native join call may begin map/NetConstString preload
						// immediately. Recheck on the same client dispatch stack after
						// selecting the Island BR parent; never leave the JoinLobby
						// transition with Donetsk still selected by native UI rules.
						if (islandClientAssigned && !playlists::MaintainAssignedClientLobbyMap(
							assignedPlaylist, assignedMap, assignedMode))
						{
							party_transport::ClearDedicatedRoute();
							night_files::ClearAuthenticatedIslandClientMatch();
							LOG_ERR("NativeMM", "Island JoinLobby cancelled: ui_mapname failed final authenticated lobby readback");
							return;
						}
					// Native msgJoinLobby -> document parsers -> stock PartyHost callback.
					// No UI JoinServer/JoinSession call, no systemlink dvar write here.
					native<void (*)(uintptr_t, uint64_t, uint64_t, const char*, unsigned, const char*, unsigned)>(0x103ECF0)(
						handler, id, lobby, host.c_str(), unsigned(host.size()), backendDoc.c_str(), unsigned(backendDoc.size()));
						joinedBR = response.at("mode").get<std::string>() == "br";
						islandLobbyMapPinned = islandClientAssigned;
						islandPinnedPlaylist = islandClientAssigned ? assignedPlaylist : -1;
					delivered = true;
					LOG_INFO("NativeMM", "JoinLobby dispatched mm=%llu lobby=%llu map=%s mode=%s", id, lobby,
						response.at("map").get_ref<const std::string&>().c_str(),
						response.at("mode").get_ref<const std::string&>().c_str());
						if (islandClientAssigned)
							LOG_INFO("NativeMM", "native Island BR parent selected from authenticated assignment; client/server NetConstString equality still required");
				}
				catch (...)
				{
					LOG_WARN("NativeMM", "discarded malformed lobby event");
				}
			});
	}

	bool Command(std::string_view command)
	{
		if (command != "native_mm_status") return false;
		OP_INFO("NativeMM | enabled=%d backend=%d state=%d lobby=%d search=%llu handler=%d delivered=%d", int(Enabled()),
			int(social_runtime::BackendConnected()), read<int>(base + kMatchmaker), read<int>(base + kMatchmaker + 4), matchId,
			int(handler != 0), int(delivered));
		return true;
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/native_matchmaking_request.inl"
#endif
	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (installed) return hook::Status::Installed;
		base = image;
		server = dedicated;
#ifndef IW8X_CLIENT_ONLY
		if (server)
		{
			constexpr unsigned char clockBytes[] = { 0x83, 0x3D, 0x95, 0x44, 0x13, 0x0D, 0x00, 0x49, 0xB8, 0, 0, 0, 0, 0, 0, 0 };
			unsigned char actual[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x148FCE0), actual, 16) || memcmp(actual, clockBytes, 16))
				return hook::Status::NotReady;
			backend = native_mm::Backend((GetTickCount64() << 16) | GetCurrentProcessId());
			if (!backend_agent::Enabled()) try
				{
					const std::filesystem::path path = L"players/native_mm_tokens.json";
					if (std::filesystem::exists(path))
					{
						std::ifstream f(path);
						Json saved;
						f >> saved;
						tokenStorageReady = backend.LoadTokens(saved);
					}
				}
				catch (...)
				{
					tokenStorageReady = false;
				}
			if (!tokenStorageReady) LOG_ERR("NativeMM", "token storage invalid; matchmaking service disabled until repaired");
			constexpr uint8_t expected[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xec, 0xd8, 0x02, 0x00,
				0x00 };
			hook::Batch batch;
			batch.Add(reinterpret_cast<void*>(base + 0x1198760), &assign_team, expected, sizeof(expected), originalAssignTeam);
			const auto status = batch.Commit();
			if (status != hook::Status::Installed) return status;
			installed = true;
			return hook::Status::Installed;
		}
#endif

		for (const auto& entry : kNativeMMBindings)
		{
			unsigned char actual[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + entry.rva), actual, 16) || memcmp(actual, entry.bytes, 16))
			{
				LOG_ERR("NativeMM", "binding mismatch RVA=%llx", entry.rva);
				return hook::Status::NotReady;
			}
		}
		auto signature = [](uintptr_t rva)
		{
			for (const auto& e : kNativeMMBindings)
				if (e.rva == rva) return e.bytes;
			return static_cast<const unsigned char*>(nullptr);
		};
		hook::Batch batch;
#define MM_HOOK(rva,fn,original) batch.Add(reinterpret_cast<void*>(base+rva),&fn,signature(rva),16,original)
		MM_HOOK(0xF76FC0, info, originalInfo);
		MM_HOOK(0xF70AD0, token_impl, originalToken);
		MM_HOOK(0xF77550, start, originalStart);
		MM_HOOK(0xF6DB10, stop, originalCancel);
		MM_HOOK(0xF77470, shutdown, originalShutdown);
		MM_HOOK(0xF6BD60, ready, originalReady);
		MM_HOOK(0xF76070, register_impl, originalRegister);
		MM_HOOK(0xF50D90, qos_complete, originalQosComplete);
		MM_HOOK(0xF50E10, qos_success, originalQosSuccess);
		MM_HOOK(0xF4FE70, qos_count, originalQosCount);
		MM_HOOK(kJoinModeCleanupRva, join_mode_cleanup, originalJoinModeCleanup);
#undef MM_HOOK
		const auto status = batch.Commit();
		if (status != hook::Status::Installed) return status;
		installed = true;
		LOG_INFO("NativeMM", "verified native matchmaking service adapters installed (configuration gated)");
		return hook::Status::Installed;
	}
}
