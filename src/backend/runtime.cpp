#include <winsock2.h>
#include <ws2tcpip.h>
#include "backend/backend.h"
#include "network/social/social_runtime.h"
#include "network/social/chat_runtime.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/playlist_backend.h"
#include "network/backend/publisher_variables.h"
#include "network/backend/native_matchmaking.h"
#include "network/online_services.h"
#include "network/match_connection.h"
#include "network/profile/progression_backend.h"
#include "network/backend/tournament_backend.h"
#include "network/profile/profile_store.h"
#include "common/console.h"
#include <bcrypt.h>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <cstdarg>
namespace backend120
{
	namespace
	{
		std::mutex logsLock, hostLock;
		std::deque<Log> logs;
		uint64_t logSerial = 0, started = 0, hostAt = 0, commandSerial = 0;
		std::atomic<bool> running = false;
		std::atomic<uint64_t> requests = 0, errors = 0;
		SOCKET discovery = INVALID_SOCKET;
		std::thread udp;
		HANDLE dataLock = INVALID_HANDLE_VALUE;
		bool winsock = false;
		std::set<int> unavailable;
		std::string hostKey, instance, commandId;
		Json host = Json::object();
		match_queue::Queue queue;
		tournament120::Reservation reservation;
		int requested = -1;

		bool host_live()
		{
			return hostAt && GetTickCount64() - hostAt < 8000;
		}

		bool equal_key(std::string_view a, std::string_view b)
		{
			unsigned different = unsigned(a.size() != b.size());
			for (size_t i = 0; i < b.size(); ++i)
				different |= unsigned((i < a.size() ? a[i] : 0) ^ b[i]);
			return !different;
		}

		match_http::Reply host_poll(const Json& j)
		{
			if (!equal_key(j.value("key", std::string{}), hostKey)) return { 403, "{\"error\":\"host authentication required\"}" };
			const auto boot = j.at("instance").get<std::string>();
			if (!chat::Hex(boot, 32) || j.at("version") != 1 || j.at("build") != "iw8-1.20.4") return { 400, "{}" };
			const auto& state = j.at("state");
			const auto session = state.at("session").get<std::string>();
			const auto playlist = state.at("playlist").get<int>();
			if (playlist < -1 || playlist > 128 || (!session.empty() && !chat::Hex(session, 216)) || !state.at("ready").is_boolean() ||
				state.at("capacity").get<int>() < 0 || state.at("capacity").get<int>() > 200 || !state.at("players").is_array() ||
				state.at("players").size() > 200 || !chat::Id(state, "host_id"))
				return { 400, "{}" };
			if (state.at("ready") == true && (playlist < 0 || session.empty())) return { 400, "{}" };
			for (const auto& p : state.at("players"))
				if (!chat::Id(p, "xuid") || !chat::Text(p.at("name").get<std::string>(), 35) || !p.at("team").is_number_integer())
					return { 400, "{}" };
			for (const char* field : { "map", "mode", "assignment", "endpoint" })
				if (!state.at(field).is_string() || state.at(field).get<std::string>().size() > 160) return { 400, "{}" };
			if (!state.at("resetting").is_boolean() || !state.at("game_mode").is_number_integer()) return { 400, "{}" };
			const auto& events = j.at("events");
			if (!events.is_array() || events.size() > 64) return { 400, "{}" };
			std::vector<challenges::Event> decoded;
			for (const auto& hex : events)
			{
				if (!hex.is_string() || hex.get_ref<const std::string&>().size() > 16384) return { 400, "{}" };
				const auto bytes = profile_store::Decode(hex.get<std::string>());
				auto e = challenges::Unpack(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
				if (!e) return { 400, "{}" };
				decoded.push_back(*e);
			}
			const auto queries = j.value("ownership", Json::array());
			if (!queries.is_array() || queries.size() > 64) return { 400, "{}" };
			for (const auto& q : queries)
				if (!chat::Id(q, "xuid") || !q.at("item").is_number_integer() || q.at("item").get<int>() <= 0) return { 400, "{}" };
			const auto admin = j.value("admin", std::string{});
			if (admin.size() > 256) return { 400, "{}" };
			const auto reject = j.value("rejected", -1);
			if (reject < -1 || reject > 128) return { 400, "{}" };
			std::string instruction;
			int selection = -1;
			{
				std::lock_guard guard(hostLock);
				if (instance != boot)
				{
					if (host_live() && !host.value("resetting", false)) return { 409, "{\"error\":\"dedicated slot occupied\"}" };
					instance = boot;
					requested = -1;
					commandId.clear();
					queue.Clear();
					unavailable.clear();
					Record("LOBBY", "Dedicated server registered");
				}
				host = state;
				hostAt = GetTickCount64();
				queue.Expire(hostAt);
				if (reject >= 0)
				{
					unavailable.insert(reject);
					requested = -1;
					commandId.clear();
					queue.Clear();
					Record("ERRORS", "Dedicated rejected playlist " + std::to_string(reject));
				}
				if (playlist >= 0)
				{
					requested = -1;
					commandId.clear();
				}
				if (playlist < 0 && queue.Tickets().empty())
				{
					requested = -1;
					commandId.clear();
				}
				if (playlist < 0 && !state.value("resetting", false) && queue.ShouldStart(hostAt) &&
					(!reservation.id || queue.Tickets().size() == 4))
				{
					if (requested < 0)
					{
						requested = queue.Playlist();
						commandId = instance + ":" + std::to_string(++commandSerial);
						Record("MATCHMAKING", "Dispatching playlist " + std::to_string(requested));
					}
				}
				selection = requested;
				instruction = commandId;
			}
			PublishHost(state);
			Json ack = Json::array();
			for (const auto& event : progression_backend::CommitBatch(decoded))
			{
				// The dedicated outbox retains this until account AND bracket commit.
				// Lost replies are safe: the progression ledger deduplicates retries.
				ack.push_back(std::to_string(event.session) + "_" + std::to_string(event.xuid) + "_" + std::to_string(event.sequence));
			}
			Json result = { { "version", 1 }, { "instance", boot }, { "playlist", selection }, { "command", instruction },
				{ "valid_for_ms", 3000 }, { "events_ack", ack } };
			result["ownership"] = Json::array();
			for (const auto& q : queries)
			{
				auto answer = q;
				answer["owns"] = progression_backend::Owns(chat::Id(q, "xuid"), q.at("item").get<int>());
				result["ownership"].push_back(answer);
			}
			if (!admin.empty()) Command(admin);
			{
				std::lock_guard guard(hostLock);
				Json teams = Json::object();
				for (const auto& [id, t] : reservation.teams)
					teams[std::to_string(id)] = t;
				result["tournament"] = { { "id", reservation.id }, { "teams", teams } };
			}
			return { 200, result.dump() };
		}
	}

	std::string Random(size_t count)
	{
		std::vector<uint8_t> bytes(count);
		if (BCryptGenRandom(nullptr, bytes.data(), ULONG(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
			throw std::runtime_error("random generator failed");
		return profile_store::Encode(bytes);
	}

	bool AtomicFile(const std::filesystem::path& p, std::string_view bytes)
	{
		try
		{
			std::filesystem::create_directories(p.parent_path());
			const auto temp = p.wstring() + L".tmp";
			HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f == INVALID_HANDLE_VALUE) return false;
			DWORD n = 0;
			const bool good = WriteFile(f, bytes.data(), DWORD(bytes.size()), &n, nullptr) && n == bytes.size() && FlushFileBuffers(f);
			CloseHandle(f);
			return good && MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}
		catch (...)
		{
			return false;
		}
	}

	void Record(std::string category, std::string text)
	{
		SYSTEMTIME t{};
		GetLocalTime(&t);
		char stamp[32];
		sprintf_s(stamp, "%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
		std::lock_guard guard(logsLock);
		logs.push_back({ ++logSerial, stamp, std::move(category), std::move(text) });
		if (logs.size() > 4000) logs.pop_front();
		std::ofstream file("magma-backend.log", std::ios::app);
		const auto& row = logs.back();
		file << row.time << " [" << row.category << "] " << row.text << '\n';
	}

	std::vector<Log> Logs(uint64_t after)
	{
		std::lock_guard guard(logsLock);
		std::vector<Log> result;
		for (const auto& l : logs)
			if (l.sequence > after) result.push_back(l);
		return result;
	}

	bool Running()
	{
		return running;
	}

	uint64_t HostId()
	{
		std::lock_guard guard(hostLock);
		return host_live() ? chat::Id(host, "host_id") : 0;
	}

	Json CamoStatus(uint64_t owner)
	{
		std::lock_guard guard(hostLock);
		if (host_live() && host.contains("camos")) return host["camos"].value(std::to_string(owner), Json::object());
		return Json::object();
	}

	Json Status()
	{
		std::lock_guard guard(hostLock);
		return { { "running", running.load() }, { "uptime_ms", started ? GetTickCount64() - started : 0 }, { "requests", requests.load() },
			{ "errors", errors.load() }, { "dedicated_online", host_live() }, { "playlist", host.value("playlist", -1) },
			{ "map", host.value("map", std::string{}) }, { "mode", host.value("mode", std::string{}) },
			{ "queue_players", queue.Tickets().size() }, { "players", host.value("players", Json::array()) },
			{ "state", host_live() ? (host.value("ready", false) ? "In match" : "Waiting / loading") : "Offline" } };
	}

	match_http::Reply Request(std::string_view path, std::string_view body, uint32_t peer)
	{
		++requests;
		match_http::Reply r{ 404, "{}" };
		try
		{
			if (path == "/health")
				r = { 200, Json{ { "service", "magma-backend-tu19" }, { "version", 1 }, { "running", running.load() } }.dump() };
			else if (path == "/v1/host/poll")
				r = host_poll(Json::parse(body));
			else if (path == "/v1/frontend")
				r = frontend_backend::Request(body, peer);
			else if (path == "/v1/playlists")
				r = playlist_backend::Request(body);
			else if (path == "/v1/publisher-variables")
				r = publisher_variables::Request(body);
			else if (path == "/v1/native-mm")
				r = native_matchmaking::Request(body, peer);
			else if (path.starts_with("/v1/social/") || path == "/v1/party-relay")
				r = social_runtime::Request(path, body, peer);
			else if (path.starts_with("/v1/chat/"))
				r = chat_runtime::Request(path, body, peer);
			else if (path == "/v1/server")
				r = { 200, Status().dump() };
		}
		catch (const std::exception&)
		{
			r = { 400, "{\"error\":\"malformed request\"}" };
		}
		if (r.status >= 400) ++errors;
		// Never log request bodies, credentials, DTLS session keys or profile blobs.
		Record(r.status >= 400					? "ERRORS"
				: path == "/v1/native-mm"		? "MATCHMAKING"
				: path.starts_with("/v1/host/") ? "LOBBY"
												: "HTTP",
			std::string(path) + " -> " + std::to_string(r.status));
		return r;
	}

	bool Command(std::string_view text)
	{
		if (text == "playlist_reload") return playlist_backend::Reload();
		if (text.starts_with("playlist_catalog ")) return SetPlaylistMode(text.substr(17));
		return progression_backend::Command(text, true);
	}

	bool Start(uint16_t httpPort, uint16_t udpPort)
	{
		if (running) return true;
		try
		{
			std::filesystem::create_directories(L"players/backend");
			dataLock = CreateFileW(
				L"players/backend/service.lock", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (dataLock == INVALID_HANDLE_VALUE) throw std::runtime_error("backend data directory already in use");
			{
				std::lock_guard guard(hostLock);
				host = Json::object();
				hostAt = 0;
				instance.clear();
				queue.Clear();
				reservation = {};
				requested = -1;
				commandId.clear();
			}
			const std::filesystem::path keyPath = L"players/backend/host.key";
			if (!std::filesystem::exists(keyPath) && !AtomicFile(keyPath, Random(32))) throw std::runtime_error("cannot save host key");
			{
				std::ifstream f(keyPath);
				f >> hostKey;
			}
			if (!chat::Hex(hostKey, 64)) throw std::runtime_error("invalid host key; existing file retained");
			const std::filesystem::path catalog = L"players/backend/playlists.json";
			if (!std::filesystem::exists(catalog))
			{
				auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(6000), RT_RCDATA);
				auto loaded = LoadResource(nullptr, resource);
				auto bytes = std::string_view(static_cast<const char*>(LockResource(loaded)), SizeofResource(nullptr, resource));
				if (bytes.empty() || !AtomicFile(catalog, bytes)) throw std::runtime_error("cannot install default playlist catalog");
			}
			WSADATA wsa{};
			if (WSAStartup(MAKEWORD(2, 2), &wsa)) throw std::runtime_error("Winsock initialization failed");
			winsock = true;
			if (!ServicesStart()) throw std::runtime_error("service data invalid; inspect retained backend data");
			discovery = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(udpPort);
			BOOL exclusive = TRUE;
			if (discovery == INVALID_SOCKET ||
				setsockopt(discovery, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive)) ||
				bind(discovery, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
				throw std::runtime_error("discovery port already in use");
			DWORD timeout = 250;
			setsockopt(discovery, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
			if (!match_http::Start(httpPort, Request)) throw std::runtime_error("HTTP port already in use");
			running = true;
			started = GetTickCount64();
			udp = std::thread(
				[]
				{
					while (running)
					{
						char b[128];
						sockaddr_in from{};
						int n = sizeof(from);
						const int got = recvfrom(discovery, b, sizeof(b), 0, reinterpret_cast<sockaddr*>(&from), &n);
						if ((got == 1 && b[0] == 0) || (got > 1 && uint8_t(b[0]) == 0xF1))
						{
							const char response[] = { 1, 1 };
							sendto(discovery, response, 2, 0, reinterpret_cast<sockaddr*>(&from), n);
						}
					}
				});
			Record("BGS", "Backend online: HTTP " + std::to_string(httpPort) + ", discovery " + std::to_string(udpPort));
			return true;
		}
		catch (const std::exception& e)
		{
			Record("ERRORS", e.what());
			Stop();
			return false;
		}
	}

	void Stop()
	{
		running = false;
		match_http::Stop();
		if (udp.joinable()) udp.join();
		if (discovery != INVALID_SOCKET)
		{
			closesocket(discovery);
			discovery = INVALID_SOCKET;
		}
		ServicesStop();
		if (winsock)
		{
			WSACleanup();
			winsock = false;
		}
		if (dataLock != INVALID_HANDLE_VALUE)
		{
			CloseHandle(dataLock);
			dataLock = INVALID_HANDLE_VALUE;
		}
		std::lock_guard guard(hostLock);
		hostAt = 0;
	}
}

namespace online_services
{
	uint64_t MatchmakingHostId()
	{
		return backend120::HostId();
	}

	matchmaking::Assignment AllocateNativeMatch(const match_queue::Packet& q, uint32_t peer)
	{
		using namespace backend120;
		std::lock_guard guard(hostLock);
		matchmaking::Assignment a;
		a.queue.request = q.request;
		if (q.kind == match_queue::kCancel)
		{
			queue.Cancel(q.player.xuid, q.request, peer, 0);
			return a;
		}
		a.queue.playlist = q.playlists.front();
		a.queue.phase = match_queue::Phase::Unavailable;
		if (!host_live() || host.value("resetting", false) || q.playlists.empty()) return a;
		if (reservation.id)
		{
			for (auto id : q.players)
				if (!reservation.teams.contains(id.xuid)) return a;
			if (q.playlists.front() != tournament120::Playlist || !reservation.teams.contains(q.player.xuid)) return a;
		}
		const int active = host.value("playlist", -1),
				  selected = active >= 0 ? active
			: queue.Playlist() >= 0		 ? queue.Playlist()
										 : q.playlists.front();
		if (unavailable.contains(selected)) return a;
		if (std::find(q.playlists.begin(), q.playlists.end(), selected) == q.playlists.end())
		{
			a.queue.phase = match_queue::Phase::WrongPlaylist;
			return a;
		}
		if (!queue.AddGroup(selected, { q.player, q.request, GetTickCount64(), peer, 0 }, q.players)) return a;
		a.queue.playlist = selected;
		a.queue.seconds = queue.Seconds(GetTickCount64());
		for (const auto& t : queue.Tickets())
			a.queue.players.push_back(t.player);
		a.queue.phase = host.value("ready", false) ? match_queue::Phase::Ready
			: active >= 0						   ? match_queue::Phase::Loading
												   : match_queue::Phase::Waiting;
		if (a.queue.phase == match_queue::Phase::Ready)
		{
			const auto bytes = profile_store::Decode(host.at("session").get<std::string>());
			if (bytes.size() != a.queue.session.size())
			{
				a.queue.phase = match_queue::Phase::Unavailable;
				return a;
			}
			std::copy(bytes.begin(), bytes.end(), a.queue.session.begin());
			std::array<uint8_t, 4> ip{};
			memcpy(ip.data(), &peer, 4);
			a.queue.session = matchmaking::NativeRouteSession(a.queue.session, ip);
			a.id = host.at("assignment");
			a.map = host.at("map");
			a.mode = host.at("mode");
			const auto* e = a.queue.session.data();
			char text[INET_ADDRSTRLEN]{};
			inet_ntop(AF_INET, e, text, sizeof(text));
			uint16_t port;
			memcpy(&port, e + 4, 2);
			a.publicEndpoint = { text, port };
		}
		return a;
	}

	tournament120::Host TournamentHost()
	{
		using namespace backend120;
		std::lock_guard guard(hostLock);
		tournament120::Host h;
		h.sampled = host_live();
		h.frame = hostAt;
		h.waiting = h.sampled && host.value("playlist", -1) < 0 && queue.Tickets().empty();
		h.running = h.sampled && host.value("ready", false);
		h.allocation = reservation.id;
		return h;
	}

	bool ReserveTournament(const tournament120::Reservation& r)
	{
		using namespace backend120;
		std::lock_guard guard(hostLock);
		if (r.id != reservation.id && (host.value("playlist", -1) >= 0 || !queue.Tickets().empty())) return false;
		reservation = r;
		return true;
	}
}

namespace console
{
	void Print(const char* format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		backend120::Record(strstr(text, "[ERR") ? "ERRORS" : "BGS", text);
	}

	void OperatorInfo(const char* format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		backend120::Record("CONTROL", text);
	}

	void OperatorWarn(const char* format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		backend120::Record("ERRORS", text);
	}

	void OperatorError(const char* format, ...)
	{
		char text[4096];
		va_list args;
		va_start(args, format);
		vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
		va_end(args);
		backend120::Record("ERRORS", text);
	}
}
