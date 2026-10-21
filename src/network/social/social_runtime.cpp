#include "network/backend/backend_agent.h"
#include "network/social/roster_memory.h"
#include "network/commerce/unlock_policy.h"
#include "network/backend/backend_latency.h"
#include "network/backend/native_matchmaking.h"
#include "network/social/party_roster_snapshot.h"
#include "network/social/social_runtime.h"
#include "network/social/chat_runtime.h"
#include "network/backend/frontend_backend.h"
#include "network/social/social_backend.h"
#include "network/social/party_relay.h"
#include "network/social/social_bindings.h"
#include "auth/offline_auth.h"
#include "engine/game.h"
#include "common/console.h"
#include "engine/safemem.h"
#include "network/playlists.h"
#include "network/online_services.h"
#include "auth/profile/local_progression.h"
#include "client/component/scheduler.hpp"
#include "common/utils/completion_budget.hpp"
#include <bcrypt.h>
#include <chrono>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include "network/social/runtime/internal.hpp"
namespace social_runtime::detail
{
	uintptr_t base = 0;

	bool installed = false, server = false, storageReady = true;

#ifndef IW8X_CLIENT_ONLY
	social::Backend service;
#endif

#ifndef IW8X_CLIENT_ONLY
	party_relay120::Service relayService;
#endif

	std::mutex serviceLock, clientLock;

	std::atomic<bool> stopping{ false };

	std::atomic<uint64_t> lastContact{ 0 }, eventAck{ 0 };

	matchmaking::BackendLatency matchmakingLatency;

	static scheduler::task_handle worker;
	static std::mutex worker_lock, shutdown_lock;
	static bool worker_stopping = false;
	std::atomic<uint64_t> worker_epoch{ 1 };
	constexpr size_t work_capacity = 96;

	scheduler::callback make_worker(uint64_t epoch);

	void wake_worker(bool start = false, bool notify = true)
	{
		scheduler::task_handle active;
		{
			std::lock_guard lock(worker_lock);
			if (worker_stopping || stopping.load(std::memory_order_acquire)) return;
			if (!worker && start) worker = scheduler::schedule(make_worker(worker_epoch.load(std::memory_order_acquire)));
			active = worker;
		}
		if (notify) active.wake();
	}

	std::wstring origin;

	// Worker-owned lease mirrored under clientLock; never logged or persisted.
	std::wstring communicationsOrigin;

	std::string communicationsLease;

	uint64_t communicationsAt = 0;

	Json snapshot;

	uint64_t snapshotAt = 0;

	uint64_t sequence = 0, revision = 0;

	unsigned refreshLists = 0;

	std::deque<Work> outgoing;

	std::deque<Result> completed;

	std::map<uint64_t, Task> tasks;

	// Engine thread owns every native reference.
	static std::map<uint64_t, std::function<void(unsigned, std::string)>> matchmakingReplies;

	static std::map<uint64_t, std::function<void(unsigned, std::string)>> frontendReplies;

	static std::map<uint64_t, std::string> presenceCache;

	std::atomic<FriendsFn> originalFriends{ nullptr };

	std::atomic<SubscribeFn> originalSubscribe{ nullptr };

	std::atomic<MessageFn> originalMessage{ nullptr };

	std::atomic<RequestsFn> originalRequests{ nullptr };

	std::atomic<SubscribeFn> originalAddFriend{ nullptr };

	std::atomic<RemoveFn> originalRemoveFriend{ nullptr };

	std::atomic<UsernameFn> originalUsername{ nullptr };

	std::atomic<UsernameBearerFn> originalUsernameBearer{ nullptr };

	std::atomic<InviteFn> originalInvite{ nullptr };

	std::atomic<FenceFn> originalFence{ nullptr };

	std::atomic<FlagsFn> originalFlags{ nullptr };

	std::atomic<AuthFn> originalInviteAuth{ nullptr };

	std::atomic<NetworkInviteFn> originalInviteNetwork{ nullptr };

	std::atomic<RequirementsFn> originalRequirements{ nullptr };

	std::atomic<AuthFn> originalAtomicJoin{ nullptr };

	std::atomic<AvailabilityGetFn> originalAvailability{ nullptr }, originalPendingAvailability{ nullptr };

	std::atomic<AvailabilitySetFn> originalSetAvailability{ nullptr };

	int ownAvailability = 4;

	uint64_t pendingAvailability = 0;

	std::map<uint64_t, int> friendAvailability;

	std::set<uint64_t> backendFriends;

	uint64_t verifiedJoinTarget = 0, verifiedJoinUntil = 0;

	static uint64_t discordTarget = 0, discordUntil = 0;

	Json resolvedGame;

	std::map<uint64_t, uint64_t> incomingInvites;

	bool backendClockReady = false;

	std::string hex(const void* data, size_t n)
	{
		const auto* p = static_cast<const unsigned char*>(data);
		std::string s;
		constexpr char d[] = "0123456789abcdef";
		for (size_t i = 0; i < n; ++i)
		{
			s += d[p[i] >> 4];
			s += d[p[i] & 15];
		}
		return s;
	}

	std::string unhex(std::string_view s)
	{
		if (s.size() % 2 || !chat::Hex(s, s.size())) return {};
		std::string out;
		for (size_t i = 0; i < s.size(); i += 2)
		{
			auto nib = [](char c)
			{
				return c <= '9' ? c - '0' : c - 'a' + 10;
			};
			out += char(nib(s[i]) * 16 + nib(s[i + 1]));
		}
		return out;
	}

	std::string random()
	{
		std::array<uint8_t, 16> b{};
		if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
		return hex(b.data(), b.size());
	}

	bool connected()
	{
		const auto t = lastContact.load();
		return t && GetTickCount64() - t < 5000;
	}

	bool atomic_file(const std::filesystem::path& path, std::string_view bytes)
	{
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) return false;
		auto temp = path;
		temp += L".tmp";
		{
			std::ofstream f(temp, std::ios::binary | std::ios::trunc);
			if (!f || !f.write(bytes.data(), std::streamsize(bytes.size())) || !f.flush()) return false;
		}
		return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}
#ifndef IW8X_CLIENT_ONLY

	bool save_service()
	{
		if (!service.Dirty()) return true;
		if (!atomic_file(L"players/social_backend.json", service.Save().dump(2))) return false;
		service.Saved();
		return true;
	}
#endif


	std::string credential()
	{
		const std::filesystem::path p = L"players/social_client.key";
		if (std::filesystem::exists(p))
		{
			std::ifstream f(p, std::ios::binary);
			std::string key((std::istreambuf_iterator<char>(f)), {});
			return chat::Hex(key, 32) ? key : std::string{};
		}
		auto key = random();
		return !key.empty() && atomic_file(p, key) ? key : std::string{};
	}

	uintptr_t* enqueue(uintptr_t* out, Kind kind, uintptr_t response, Json request)
	{
		*out = 0;
		if (tasks.size() >= 32) return out;
		{
			std::lock_guard lock(clientLock);
			if (stopping.load(std::memory_order_acquire) || outgoing.size() >= work_capacity) return out;
		}
		const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
		if (!object) return out;
		native<uintptr_t (*)(uintptr_t)>(0x20FDEF0)(object);
		write_impl<long>(object + 8, 2);
		write_impl<float>(object + 24, 10.0f);
		write_impl<int>(object + 28, 1);
		const auto id = ++sequence;
		request["request"] = std::to_string(id);
		tasks.emplace(id, Task{ object, response, kind, GetTickCount64() });
		*out = object;
		{
			std::lock_guard lock(clientLock);
			outgoing.push_back({ id, kind, std::move(request) });
		}
		wake_worker();
		LOG_INFO("Social", "native task queued request=%llu operation=%d", id, int(kind));
		return out;
	}

	std::atomic<ListFetchFn> originalListFetch{ nullptr };

	std::atomic<ListCreateFn> originalListCreate{ nullptr };

	std::atomic<ListAddFn> originalListAdd{ nullptr };

	std::atomic<ListRemoveFn> originalListRemove{ nullptr };

	std::atomic<RecentCountFn> originalRecentCount{ nullptr };

	unsigned recent_count(uintptr_t self, int controller)
	{
		if (controller != 0) return originalRecentCount.load()(self, controller);
		// Replay's 1B3E73A branch returns zero for a loaded list and dereferences a
		// null out-pointer for an unavailable one. Correct that native getter.
		const auto list = user_list_address(0);
		return read<uint8_t>(list + 98837) ? std::min(read<unsigned>(list + 48016), 200u) : 0u;
	}

	int own_user_list(const char* name, uintptr_t response, size_t offset)
	{
		std::array<char, 32> text{};
		if (!name || !safemem::ReadBytes(name, text.data(), text.size()) || !memchr(text.data(), 0, text.size())) return -1;
		const int type = social::UserLists::Type(text.data());
		return type >= 0 && response == user_list_address(type) + offset ? type : -1;
	}

	void pump_user_lists()
	{
		if (!connected()) return;
		static uint64_t next = 0;
		const auto now = GetTickCount64();
		if (now < next) return;
		next = now + 2000;
		// On a late sign-in the retail module registry may skip Init. Its native
		// initializer only assigns the three list types and is safe to repeat.
		native<bool (*)(uintptr_t)>(0x107A1A0)(native<uintptr_t (*)()>(0x1079F30)());
		for (int i = 0; i < 3; ++i)
		{
			const auto list = user_list_address(i);
			if (read<int>(list + 98832) != i || read<uint8_t>(list + 98836)) continue;
			if (read<uint8_t>(list + 98837))
			{
				native<void (*)(uintptr_t, int)>(0x107F580)(list, 0);
				continue;
			}
			// A delayed backend can exhaust the retail login-only retry budget.
			// Retry the real fetch once connected, without clearing native queues.
			write_impl<unsigned>(list + 98840, 0);
			native<bool (*)(uintptr_t, int, const char*)>(0x107FA70)(list, 0, "");
		}
	}

	uintptr_t local_username_search()
	{
		return native<uintptr_t (*)()>(0x1B371E0)() + 36584;
	}

	bool local_join_ready()
	{
		return verifiedJoinTarget && GetTickCount64() < verifiedJoinUntil && connected() && offlineauth::IsReady() && playlists::Ready() &&
			read<uint8_t>(base + 0xD305A34) && read<int>(base + 0xE6BDA40) == 2 &&
			read<uint64_t>(base + 0x11ACD290 + 216) == verifiedJoinTarget;
	}

	bool local_invite_authenticated()
	{
		const auto target = read<uint64_t>(base + 0x11ACD290 + 216);
		auto invite = incomingInvites.find(target);
		return read<int>(base + 0x11ACD290 + 192) == 0 && invite != incomingInvites.end() && invite->second > GetTickCount64() &&
			(backendFriends.contains(target) || (target == discordTarget && GetTickCount64() < discordUntil)) && connected() &&
			offlineauth::IsReady() && read<int>(base + 0xE6BDA40) == 2;
	}

	void user_details(uintptr_t details, uint64_t id, const std::string& name)
	{
		// Objects are already SDK-constructed; preserve virtual bases/refcounts.
		write_impl<uint64_t>(details + 32, id);
		memcpy(reinterpret_cast<void*>(details + 40), "uno", 4);
		native<void (*)(uintptr_t, const char*)>(0x20FA790)(details, name.c_str());
	}

	std::string base64_id(uint64_t id)
	{
		constexpr char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		unsigned value = 0, bits = 0;
		for (int i = 0; i < 8; ++i)
		{
			value = (value << 8) | unsigned((id >> (8 * i)) & 255);
			bits += 8;
			while (bits >= 6)
			{
				bits -= 6;
				out += a[(value >> bits) & 63];
			}
		}
		if (bits) out += a[(value << (6 - bits)) & 63];
		while (out.size() % 4)
			out += '=';
		return out;
	}

	Json rich_presence(const Json& p)
	{
		const auto id = chat::Id(p, "xuid");
		const auto activity = p.value("activity", std::string{});
		// Native context: 0 offline/not in game, 1 playing MP, 3 lobby, 4 party.
		int context = activity == "ingame" ? 1 : activity == "searching" ? 3 : p.value("online", false) ? 4 : 0;
		auto index = [](std::string name, uintptr_t table, int count)
		{
			for (auto& c : name)
				c = char(std::toupper(static_cast<unsigned char>(c)));
			name = "PRESENCE/" + name;
			for (int i = 0; i < count; ++i)
			{
				auto ptr = read<uintptr_t>(base + table + 8ull * i);
				std::array<char, 96> text{};
				if (ptr && safemem::ReadBytes(reinterpret_cast<void*>(ptr), text.data(), text.size()) &&
					memchr(text.data(), 0, text.size()) && name == text.data())
					return i;
			}
			return -1;
		};
		auto mapName = p.value("map", std::string{}), modeName = p.value("mode", std::string{});
		if (mapName == "mp_speed") mapName = "mp_m_speedball";
		if (modeName == "dmz") modeName = "br"; // Plunder uses the build's BR presence family.
		const int map = index(mapName, 0x5A08EC0, 70), mode = index(modeName, 0x5A09170, 30);
		// Context9 is Theater, not generic playing. Until registered mode metadata
		// arrives, use the native online status without an invented game label.
		if (context == 1 && mode < 0) context = 0;
		Json data = { { "version", 8 }, { "context", context }, { "gametype", std::max(mode, 0) }, { "mapindex", std::max(map, 0) },
			{ "sessionFlags", p.value("joinable", false) ? 0 : 4 }, { "flags", 0 }, { "inputType", 0 }, { "platformId", base64_id(id) },
			{ "clanTag", "" } };
		return { { "presence", { { "online", p.value("online", false) }, { "platform", "bnet" }, { "data", data } } } };
	}

	bool set_presence(uintptr_t info, const Json& p)
	{
		const auto json = rich_presence(p).dump();
		alignas(8) std::array<uint8_t, 32> parser{};
		native<void (*)(void*, const char*)>(0x20E4250)(parser.data(), json.c_str());
		const bool ok = native<bool (*)(uintptr_t, const char*, void*)>(0x21404F0)(info, "presence", parser.data());
		native<void (*)(void*)>(0x20E42B0)(parser.data());
		return ok;
	}

	void push_presence(const Json& p)
	{
		const auto id = chat::Id(p, "xuid");
		if (!id) return;
		friendAvailability[id] = p.value("online", false) ? p.value("availability", 2) : 4;
		if (!native<uintptr_t (*)(int, uint64_t)>(0x10584F0)(0, id)) return;
		const auto key = p.dump();
		if (presenceCache[id] == key) return;
		alignas(8) std::array<uint8_t, 240> value{};
		alignas(8) std::array<uint8_t, 16> handler{};
		native<void (*)(void*)>(0x212E330)(value.data());
		user_details(reinterpret_cast<uintptr_t>(value.data()) + 96, id, "");
		if (set_presence(reinterpret_cast<uintptr_t>(value.data()) + 24, p))
		{
			native<void (*)(void*, void*)>(0x1057D10)(handler.data(), value.data());
			presenceCache[id] = key;
			LOG_INFO("Presence", "native update user=%llu state=%s joinable=%d", id, p.value("activity", std::string{}).c_str(),
				int(p.value("joinable", false)));
		}
		native<void (*)(void*)>(0x2140450)(value.data() + 24);
	}
}

namespace social_runtime
{
	using namespace detail;

	bool BackendConnected()
	{
		return connected();
	}

	std::string PartySnapshot()
	{
		std::lock_guard lock(clientLock);
		return snapshot.dump();
	}

	bool JoinDiscord(uint64_t target, uint64_t validUntil)
	{
		const auto identity = offlineauth::GetIdentity();
		const auto now = GetTickCount64();
		if (server || !installed || !target || target == identity.xuid || !frontend_backend::Authenticated() || !connected() ||
			now >= validUntil || validUntil - now > 120000 || !native<bool (*)()>(game::kIsInFrontEndRVA)() ||
			native_matchmaking::Searching())
			return false;
		constexpr uint8_t expected[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
			0x89, 0x7c, 0x24, 0x20 };
		uint8_t actual[sizeof(expected)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0xF1E370), actual, sizeof(actual)) ||
			memcmp(actual, expected, sizeof(actual)))
			return false;
		// TU19 native invitation manager caller F5A0B0 passes this exact tuple:
		// platform IDs then recipient and sender UNO IDs. HSM rejects busy joins.
		const std::array<uint64_t, 4> invite{ 0, 0, identity.xuid, target };
		if (!native<bool (*)(uintptr_t, int, const void*)>(0xF1E370)(base + 0x11ACD290, 0, invite.data())) return false;
		discordTarget = target;
		discordUntil = validUntil;
		incomingInvites[target] = validUntil;
		return true;
	}

	void Bootstrap(const std::wstring& url)
	{
		if (!installed || server || url.empty() || stopping.load(std::memory_order_acquire)) return;
		// No retail auth or native menu is needed to authenticate this local key.
		const auto identity = offlineauth::GetIdentity();
		if (!identity.xuid) return;
		bool changed = false;
		{
			std::lock_guard lock(clientLock);
			changed = snapshot.is_null() || origin != url;
			if (snapshot.is_null())
				snapshot = { { "xuid", std::to_string(identity.xuid) }, { "name", identity.name }, { "host", "0" }, { "party", "" },
					{ "bytes", "" }, { "members", Json::array() }, { "game", "" }, { "endpoint", "native-session" },
					{ "activity", "menus" } };
			origin = url;
		}
		wake_worker(true, changed);
	}

	bool SubmitFrontend(std::string body, std::function<void(unsigned, std::string)> complete)
	{
		if (!installed || server || frontendReplies.size() >= 2) return false;
		auto request = Json::parse(body, nullptr, false);
		if (!request.is_object()) return false;
		{
			std::lock_guard lock(clientLock);
			if (stopping.load(std::memory_order_acquire) || outgoing.size() >= work_capacity) return false;
			const auto id = ++sequence;
			request["request"] = std::to_string(id);
			frontendReplies.emplace(id, std::move(complete));
			outgoing.push_back({ id, Kind::Frontend, std::move(request) });
		}
		wake_worker();
		return true;
	}

	void PumpFrontend()
	{
		if (stopping.load(std::memory_order_acquire)) return;
		for (utils::completion_budget budget; budget.next();)
		{
			Result r;
			{
				std::lock_guard lock(clientLock);
				const auto found = std::find_if(completed.begin(), completed.end(),
					[](const Result& item) { return frontendReplies.contains(item.id); });
				if (found == completed.end()) break;
				r = std::move(*found);
				completed.erase(found);
			}
			auto found = frontendReplies.find(r.id);
			if (found == frontendReplies.end()) continue;
			auto callback = std::move(found->second);
			frontendReplies.erase(found);
			callback(r.status, r.body.dump());
		}
	}

	int MatchmakingPing()
	{
		return connected() ? matchmakingLatency.Read(GetTickCount64()) : -1;
	}

	bool SubmitMatchmaking(std::string body, std::function<void(unsigned, std::string)> complete)
	{
		if (server || !installed || matchmakingReplies.size() >= 32) return false;
		auto request = Json::parse(body, nullptr, false);
		if (!request.is_object()) return false;
		{
			std::lock_guard lock(clientLock);
			if (stopping.load(std::memory_order_acquire) || outgoing.size() >= work_capacity) return false;
			const auto id = ++sequence;
			request["request"] = std::to_string(id);
			matchmakingReplies.emplace(id, std::move(complete));
			outgoing.push_back({ id, Kind::Matchmaking, std::move(request) });
		}
		wake_worker();
		return true;
	}
#ifndef IW8X_CLIENT_ONLY

	void PublishDedicated(std::string bytes, int playlist, const char* map, const char* mode, std::string endpoint, int capacity)
	{
		if (!installed || !server || backend_agent::Enabled()) return;
		static uint64_t next = 0;
		const auto now = GetTickCount64();
		if (now < next) return;
		next = now + 500;
		party_roster::Memory memory;
		// The existing matchmaking serializer uses uppercase; the social protocol
		// deliberately uses canonical lowercase for validation and equality.
		for (auto& c : bytes)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		social::Session s;
		s.bytes = std::move(bytes);
		s.playlist = playlist;
		s.map = map ? map : "";
		s.mode = mode ? mode : "";
		s.endpoint = std::move(endpoint);
		s.capacity = capacity;
		s.gameMode = native<uint8_t (*)()>(game::kComGameModeGetActiveGameModeRVA)();
		if (s.bytes.size() == 216) s.id = s.bytes.substr(168, 16);
		if (!s.id.empty())
			for (int i = 0; i < std::clamp(memory.Read<int>(base + 0xE17FCD0), 0, 200); ++i)
			{
				const auto client = memory.Read<uintptr_t>(base + 0xE17F690 + 8ull * i);
				if (!client || memory.Read<uint8_t>(client + 8) < 3 || memory.Read<int>(client + 214756) == 2) continue;
				const auto m = base + native_matchmaking::ServerPartyRVA() + 488ull * i;
				const auto status = memory.Read<uint8_t>(m + 2594);
				const auto id = memory.Read<uint64_t>(m + 2600);
				if (id && (status == 4 || status == 5)) s.members.insert(id);
			}
		// Disk-backed list mutations run on HTTP workers. A roster publication can
		// wait for the next tick; gameplay must never wait for those disk writes.
		std::unique_lock lock(serviceLock, std::try_to_lock);
		if (lock.owns_lock()) service.Publish(std::move(s), now);
	}
#endif


	void ClientPump(const std::wstring& url, std::string gameSession, bool searching)
	{
		if (!installed || server || !offlineauth::IsReady() || stopping.load(std::memory_order_acquire)) return;
		static std::wstring grantOrigin;
		if (grantOrigin != url)
		{
			grantOrigin = url;
			incomingInvites.erase(discordTarget);
			discordTarget = discordUntil = 0;
		}
		PumpFrontend();
		const auto now = GetTickCount64();
		for (utils::completion_budget budget; budget.next();)
		{
			Result r;
			{
				std::lock_guard lock(clientLock);
				if (completed.empty()) break;
				r = std::move(completed.front());
				completed.pop_front();
			}
			try
			{
				if (auto it = frontendReplies.find(r.id); it != frontendReplies.end())
				{
					auto callback = std::move(it->second);
					frontendReplies.erase(it);
					callback(r.status, r.body.dump());
				}
				else if (auto it = matchmakingReplies.find(r.id); it != matchmakingReplies.end())
				{
					auto callback = std::move(it->second);
					matchmakingReplies.erase(it);
					callback(r.status, r.body.dump());
				}
				else if (r.id)
					complete_impl(r);
				else if (r.status == 201)
				{
					revision = 0;
					presenceCache.clear();
					refreshLists = 7;
				}
				else if (r.status == 200)
					apply_sync(r.body);
			}
			catch (...)
			{
				LOG_WARN("Social", "discarded malformed service result");
				if (r.id) complete_impl({ r.id, 400, {} });
			}
		}
		std::vector<uint64_t> expired;
		for (const auto& [id, t] : tasks)
			if (now - t.at > 10000) expired.push_back(id);
		for (auto id : expired)
		{
			{
				std::lock_guard lock(clientLock);
				std::erase_if(outgoing, [id](const Work& work) { return work.id == id; });
			}
			complete_impl({ id, 408, {} });
		}
		if (!connected())
			for (auto id : backendFriends)
				push_presence(
					Json{ { "xuid", std::to_string(id) }, { "online", false }, { "activity", "offline" }, { "joinable", false } });
		// Native add/remove completion callbacks reset and fetch all three lists.
		// The remote task can complete one frame before those callbacks run. Never
		// launch a revision refresh in that gap: a second first-page callback would
		// append duplicates into the shared native cache (F55CD0).
		const bool relationshipBusy = refreshLists && (read<uint8_t>(base + 0xB8BD78B) || read<uint8_t>(base + 0xB8BD78C));
		if (connected() && refreshLists && !relationshipBusy && read<uintptr_t>(base + 0xB8BDDF8))
			for (int type = 0; type <= 2; ++type)
				if ((refreshLists & (1u << type)) && read<int>(base + 0xB8BDE08 + 40 * type) != 1)
				{
					native<void (*)(int, int)>(0xF54480)(type, 0);
					native<void (*)(uintptr_t, int)>(type == 0 ? 0xF54DA0 : type == 1 ? 0xF54FC0 : 0xF55210)(0, 0);
					refreshLists &= ~(1u << type);
				}
		if (connected()) native<void (*)(uintptr_t, int)>(0x10585E0)(0, 0);
		pump_user_lists();
		static uint64_t next = 0;
		if (now < next) return;
		next = now + 500;
		const auto identity = offlineauth::GetIdentity();
		if (!identity.xuid) return;
		const bool frontend = native<bool (*)()>(game::kIsInFrontEndRVA)();
		const auto party = base + 0xC796480;
		uint64_t host = 0;
		std::string session, bytes;
		Json members = Json::array();
		if (frontend && read<int>(party + 311116) == 1 && read<int>(party + 311120) == 1 && read<int>(base + 0xC7F1870) == 0)
		{
			auto data = read<uintptr_t>(party);
			std::array<uint8_t, 108> raw{};
			if (data && safemem::ReadBytes(reinterpret_cast<void*>(data + 4), raw.data(), raw.size()))
			{
				bytes = hex(raw.data(), raw.size());
				session = bytes.substr(168, 16);
				int h = read<int>(party + 311000);
				host = read<int>(party + 311112) == 1 ? identity.xuid : h >= 0 && h < 200 ? read<uint64_t>(party + 488ull * h + 2600) : 0;
				for (auto id : party_roster::Members(party))
					members.push_back(std::to_string(id));
			}
		}
		Json state = { { "xuid", std::to_string(identity.xuid) }, { "name", identity.name }, { "host", std::to_string(host) },
			{ "party", session }, { "bytes", bytes }, { "members", members }, { "game", gameSession }, { "endpoint", "native-session" },
			{ "activity",
				!frontend		? "ingame"
					: searching ? "searching"
								: "menus" } };
		{
			std::lock_guard lock(clientLock);
			snapshot = std::move(state);
			snapshotAt = now;
			origin = url;
		}
		wake_worker(true);
	}

	void Shutdown()
	{
		std::lock_guard shutdown_guard(shutdown_lock);
		scheduler::task_handle retiring;
		{
			std::lock_guard lock(worker_lock);
			worker_stopping = true;
			stopping.store(true, std::memory_order_release);
			worker_epoch.fetch_add(1, std::memory_order_acq_rel);
			retiring = std::move(worker);
			worker = {};
		}
		// A HTTP tick can hold clientLock; join only after all owner locks release.
		retiring.cancel(true);
		{
			std::lock_guard lock(clientLock);
			communicationsLease.clear();
			communicationsOrigin.clear();
			communicationsAt = 0;
		}
		lastContact = 0;
		matchmakingLatency.Reset();
		{
			std::lock_guard lock(worker_lock);
			// The next authorized Bootstrap/ClientPump can retain a new context.
			// Its epoch was advanced before joining the retired callback.
			stopping.store(false, std::memory_order_release);
			worker_stopping = false;
		}
	}

	bool HandleCommand(std::string_view command)
	{
		if (command == "userlists_status")
		{
			if (!server)
				for (int i = 0; i < 3; ++i)
				{
					const auto list = user_list_address(i);
					OP_INFO("UserLists | type=%d loaded=%u busy=%u count=%u adds=%u removes=%u failures=%u", i, read<uint8_t>(list + 98837),
						read<uint8_t>(list + 98836), read<unsigned>(list + 48016), read<uint16_t>(list + 16010), read<uint16_t>(list + 2),
						read<unsigned>(list + 98840));
				}
			return true;
		}
		if (command == "social_status")
		{
#ifndef IW8X_CLIENT_ONLY
			if (server)
			{
				std::lock_guard lock(serviceLock);
				for (const auto& r : service.Status(GetTickCount64()))
					OP_INFO("Social | %s %s state=%s friends=%zu joinable=%d", r["xuid"].get_ref<const std::string&>().c_str(),
						r["name"].get_ref<const std::string&>().c_str(), r["activity"].get_ref<const std::string&>().c_str(),
						r["friends"].get<size_t>(), int(r["joinable"].get<bool>()));
			}
			else
#endif

				OP_INFO("Social | backend=%d friends=%d tasks=%zu presence=%zu", int(connected()), read<int>(base + 0xB8BDE04),
					tasks.size(), presenceCache.size());
			return true;
		}
#ifndef IW8X_CLIENT_ONLY
		if (command.starts_with("friend_add "))
		{
			if (!server)
			{
				OP_WARN("friend_add is a dedicated-server command");
				return true;
			}
			std::istringstream input{ std::string(command.substr(11)) };
			std::string a, b, extra;
			input >> a >> b;
			const auto x = chat::Id(Json{ { "id", a } }, "id"), y = chat::Id(Json{ { "id", b } }, "id");
			std::lock_guard lock(serviceLock);
			auto before = service;
			bool ok = !(input >> extra) && service.Befriend(x, y) && save_service();
			if (!ok) service = std::move(before);
			OP_INFO("Friendship %s: %llu <-> %llu (both users must register first)", ok ? "saved" : "rejected", x, y);
			return true;
		}
#endif

		return false;
	}

	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (installed) return hook::Status::Installed;
		base = image;
		server = dedicated;
#ifndef IW8X_CLIENT_ONLY
		if (server && backend_agent::Enabled())
		{
			installed = true;
			return hook::Status::Installed;
		}
#endif

#ifndef IW8X_CLIENT_ONLY
		if (server)
		{
			const std::filesystem::path path = L"players/social_backend.json";
			if (std::filesystem::exists(path)) try
				{
					std::ifstream f(path);
					Json j;
					f >> j;
					storageReady = service.Load(j);
				}
				catch (...)
				{
					storageReady = false;
				}
			installed = true;
			LOG_INFO("Social", "dedicated social service initialized storage=%d", int(storageReady));
			return hook::Status::Installed;
		}
#endif

		// Generated from the exact Replay IDB; helpers and hook sites are checked
		// together before installing any adapter (see replay_social_signatures).
		for (const auto& s : kSocialBindings)
		{
			std::array<uint8_t, 16> actual{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + s.rva), actual.data(), 16) || memcmp(actual.data(), s.bytes, 16))
			{
				LOG_ERR("Social", "native binding mismatch RVA=%llx", s.rva);
				return hook::Status::NotReady;
			}
		}
		auto signature = [](uintptr_t rva)
		{
			for (const auto& s : kSocialBindings)
				if (s.rva == rva) return s.bytes;
			return static_cast<const unsigned char*>(nullptr);
		};
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0xF6FB90), &get_friends, signature(0xF6FB90), 16, originalFriends);
		batch.Add(reinterpret_cast<void*>(base + 0xF779E0), &subscribe, signature(0xF779E0), 16, originalSubscribe);
		batch.Add(reinterpret_cast<void*>(base + 0xF769A0), &message, signature(0xF769A0), 16, originalMessage);
		batch.Add(reinterpret_cast<void*>(base + 0xF712D0), &requests_impl, signature(0xF712D0), 16, originalRequests);
		batch.Add(reinterpret_cast<void*>(base + 0xF6D4E0), &add_friend, signature(0xF6D4E0), 16, originalAddFriend);
		batch.Add(reinterpret_cast<void*>(base + 0xF788D0), &search_username, signature(0xF788D0), 16, originalUsername);
		batch.Add(reinterpret_cast<void*>(base + 0x20FCDB0), &username_bearer, signature(0x20FCDB0), 16, originalUsernameBearer);
		batch.Add(reinterpret_cast<void*>(base + 0xF76170), &remove_friend, signature(0xF76170), 16, originalRemoveFriend);
		batch.Add(reinterpret_cast<void*>(base + 0xF5C750), &invite_impl, signature(0xF5C750), 16, originalInvite);
		batch.Add(reinterpret_cast<void*>(base + 0xF198E0), &fence, signature(0xF198E0), 16, originalFence);
		batch.Add(reinterpret_cast<void*>(base + 0x1597410), &flags, signature(0x1597410), 16, originalFlags);
		batch.Add(reinterpret_cast<void*>(base + 0xF1F6A0), &invite_auth, signature(0xF1F6A0), 16, originalInviteAuth);
		batch.Add(reinterpret_cast<void*>(base + 0xF1E840), &invite_network, signature(0xF1E840), 16, originalInviteNetwork);
		batch.Add(reinterpret_cast<void*>(base + 0x15284E0), &invite_requirements, signature(0x15284E0), 16, originalRequirements);
		batch.Add(reinterpret_cast<void*>(base + 0xF1EF20), &atomic_join, signature(0xF1EF20), 16, originalAtomicJoin);
		batch.Add(reinterpret_cast<void*>(base + 0xC88A30), &availability, signature(0xC88A30), 16, originalAvailability);
		batch.Add(reinterpret_cast<void*>(base + 0xC88B20), &pending_availability, signature(0xC88B20), 16, originalPendingAvailability);
		batch.Add(reinterpret_cast<void*>(base + 0xC88C10), &set_availability, signature(0xC88C10), 16, originalSetAvailability);
		if (!prepare_user_lists(batch)) return hook::Status::NotReady;
		const auto status = batch.Commit();
		installed = status == hook::Status::Installed;
		if (installed) LOG_INFO("Social", "native friends/presence/invitation and user-list task adapters installed");
		return status;
	}
}
