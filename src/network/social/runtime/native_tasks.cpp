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
	void release(uintptr_t object)
	{
		if (_InterlockedDecrement(reinterpret_cast<volatile long*>(object + 8)) == 0)
			reinterpret_cast<void (*)(uintptr_t, unsigned)>(read<uintptr_t>(read<uintptr_t>(object)))(object, 1);
	}

	uintptr_t user_list_address(int type)
	{
		return native<uintptr_t (*)()>(0x1079F30)() + 64 + 98856ull * type;
	}

	uintptr_t* list_fetch(uintptr_t self, uintptr_t* out, const char* name, const char* page, uintptr_t response)
	{
		const auto type = own_user_list(name, response, 48032);
		if (type < 0) return originalListFetch.load()(self, out, name, page, response);
		// One complete bounded page. Any unexpected continuation fails explicitly.
		if (page && read<char>(reinterpret_cast<uintptr_t>(page)))
		{
			*out = 0;
			return out;
		}
		LOG_INFO("UserLists", "fetch %s", userListNames[type]);
		return enqueue(out, Kind::list_fetch, response, { { "list", userListNames[type] }, { "op", "fetch" }, { "page", "" } });
	}

	uintptr_t* list_create(uintptr_t self, uintptr_t* out, const char* name, uintptr_t response)
	{
		const auto type = own_user_list(name, response, 98792);
		if (type < 0) return originalListCreate.load()(self, out, name, response);
		return enqueue(out, Kind::list_mutation, response, { { "list", userListNames[type] }, { "op", "create" } });
	}

	uintptr_t* list_mutation(uintptr_t* out, int type, uintptr_t rows, unsigned count, uintptr_t response, bool remove)
	{
		*out = 0;
		if (!rows || !count || count > 200) return out;
		Json ids = Json::array();
		std::set<uint64_t> seen;
		for (unsigned i = 0; i < count; ++i)
		{
			const auto item = rows + 248ull * i;
			const auto id = read<uint64_t>(item + 64);
			std::array<char, 4> provider{};
			if (!id || !safemem::ReadBytes(reinterpret_cast<void*>(item + 72), provider.data(), 4) || memcmp(provider.data(), "uno", 4) ||
				!seen.insert(id).second)
				return out;
			ids.push_back(std::to_string(id));
		}
		LOG_INFO("UserLists", "%s %s count=%u", remove ? "remove" : "add", userListNames[type], count);
		return enqueue(
			out, Kind::list_mutation, response, { { "list", userListNames[type] }, { "op", remove ? "remove" : "add" }, { "users", ids } });
	}

	uintptr_t* list_add(
		uintptr_t self, uintptr_t* out, const char* name, uintptr_t rows, unsigned count, bool trim, unsigned maximum, uintptr_t response)
	{
		const auto type = own_user_list(name, response, 98712);
		if (type < 0) return originalListAdd.load()(self, out, name, rows, count, trim, maximum, response);
		return list_mutation(out, type, rows, count, response, false);
	}

	uintptr_t* list_remove(uintptr_t self, uintptr_t* out, const char* name, uintptr_t rows, unsigned count, uintptr_t response)
	{
		const auto type = own_user_list(name, response, 98752);
		if (type < 0) return originalListRemove.load()(self, out, name, rows, count, response);
		return list_mutation(out, type, rows, count, response, true);
	}

	bool apply_user_list(uintptr_t response, const Json& body)
	{
		const int type = social::UserLists::Type(body.value("list", std::string{}));
		if (type < 0 || response != user_list_address(type) + 48032 || !body.contains("users") || !body["users"].is_array() ||
			body["users"].size() > 200 || !body.value("page", std::string{}).empty())
			return false;
		std::set<uint64_t> ids;
		for (const auto& row : body["users"])
		{
			const auto id = chat::Id(row, "xuid");
			if (!id || !ids.insert(id).second || !chat::Text(row.value("name", std::string{}), 63) || !row.contains("time") ||
				!row["time"].is_number_unsigned() || !row["time"].get<uint64_t>() || row["time"].get<uint64_t>() > UINT32_MAX)
				return false;
		}
		write_impl<unsigned>(response + 49640, 0);
		write_impl<char>(response + 49648, 0);
		unsigned index = 0;
		for (const auto& row : body["users"])
		{
			const auto item = response + 40 + 248ull * index++;
			write_impl<uint64_t>(item, row["time"].get<uint64_t>());
			// SDK-constructed bdUserListEntity embeds bdUserDetails at +32.
			// Keep its virtual bases/refcounts; use the native username setter.
			write_impl<uint64_t>(item + 64, chat::Id(row, "xuid"));
			memcpy(reinterpret_cast<void*>(item + 72), "uno", 4);
			native<void (*)(uintptr_t, const char*)>(0x20FA790)(item + 32, row["name"].get_ref<const std::string&>().c_str());
		}
		write_impl<unsigned>(response + 49640, index);
		LOG_INFO("UserLists", "received %s count=%u; native completion pending", userListNames[type], index);
		return true;
	}

	bool prepare_user_lists(hook::Batch& batch)
	{
		for (const auto& s : kUserListBindings)
		{
			std::array<uint8_t, 16> actual{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + s.rva), actual.data(), 16) || memcmp(actual.data(), s.bytes, 16))
			{
				LOG_ERR("UserLists", "binding mismatch RVA=%llx", s.rva);
				return false;
			}
		}
		auto sig = [](uintptr_t rva)
		{
			for (const auto& s : kUserListBindings)
				if (s.rva == rva) return s.bytes;
			return static_cast<const unsigned char*>(nullptr);
		};
		batch.Add(reinterpret_cast<void*>(base + 0xF6CEE0), &list_fetch, sig(0xF6CEE0), 16, originalListFetch);
		batch.Add(reinterpret_cast<void*>(base + 0xF6CDC0), &list_create, sig(0xF6CDC0), 16, originalListCreate);
		batch.Add(reinterpret_cast<void*>(base + 0xF6CC30), &list_add, sig(0xF6CC30), 16, originalListAdd);
		batch.Add(reinterpret_cast<void*>(base + 0xF6D010), &list_remove, sig(0xF6D010), 16, originalListRemove);
		batch.Add(reinterpret_cast<void*>(base + 0x1B3E710), &recent_count, sig(0x1B3E710), 16, originalRecentCount);
		return true;
	}

	int availability(uint64_t id)
	{
		if (id == offlineauth::GetIdentity().xuid) return connected() ? ownAvailability : 4;
		auto it = friendAvailability.find(id);
		return it != friendAvailability.end() ? (connected() ? it->second : 4) : originalAvailability.load()(id);
	}

	int pending_availability(uint64_t id)
	{
		return id == offlineauth::GetIdentity().xuid ? availability(id) : originalPendingAvailability.load()(id);
	}

	char set_availability(int value)
	{
		if (!offlineauth::IsReady()) return originalSetAvailability.load()(value);
		if (value < 1 || value > 4 || !connected() || pendingAvailability || value == ownAvailability) return 0;
		pendingAvailability = ++sequence;
		{
			std::lock_guard lock(clientLock);
			outgoing.push_back({ pendingAvailability, Kind::availability,
				Json{ { "availability", value }, { "request", std::to_string(pendingAvailability) } } });
		}
		// This platform setter has no bdRemoteTask return. Publish its native UI
		// notification only after the service acknowledges and persists the change.
		return 0;
	}

	void apply_availability(int value)
	{
		if (value < 1 || value > 4 || value == ownAvailability) return;
		ownAvailability = value;
		native<void (*)(unsigned, int)>(0x1B95F20)(0, value);
		const auto lua = read<uintptr_t>(base + 0x11868880);
		if (lua && native<bool (*)(int, const char*, uintptr_t)>(0x19BCC30)(0, "bnet_online_status_set", lua))
		{
			native<void (*)(const char*, int64_t, uintptr_t)>(0x19CE4F0)("onlineStatus", value, lua);
			native<void (*)(uintptr_t)>(0x19BDA00)(lua);
		}
		LOG_INFO("Presence", "native availability updated from service: %d", value);
	}

	uintptr_t* get_friends(uintptr_t self, uintptr_t* out, uintptr_t response, const char* page)
	{
		if (response != native<uintptr_t (*)(int)>(0xF56420)(0)) return originalFriends.load()(self, out, response, page);
		return enqueue(out, Kind::Friends, response, Json::object());
	}

	uintptr_t username_bearer(uintptr_t request, const char* value)
	{
		const auto search = local_username_search();
		// E52A60 builds retail credentials before the DW wrapper. With the local
		// provider its login result is absent, producing pointer8 and a strlen AV.
		// This request is authenticated by our worker's saved key/lease instead;
		// keep its unused retail field empty, without changing global auth state.
		if (reinterpret_cast<uintptr_t>(_ReturnAddress()) - base == 0xE52BBA && read<uintptr_t>(search + 24) &&
			read<uintptr_t>(search + 32) == base + 0xF542D0)
			return originalUsernameBearer.load()(request, "");
		return originalUsernameBearer.load()(request, value);
	}

	uintptr_t* search_username(uintptr_t self, uintptr_t* out, uintptr_t request, uintptr_t response)
	{
		const auto search = local_username_search();
		// DWUsernameSearch also serves clans. Adapt only OnlineFriends' result and
		// callback; preserve all other consumers and their native task ownership.
		if (!response || response != read<uintptr_t>(search + 24) || read<uintptr_t>(search + 32) != base + 0xF542D0)
			return originalUsername.load()(self, out, request, response);
		std::array<char, 256> name{};
		std::array<char, 11> provider{};
		if (!request || !safemem::ReadBytes(reinterpret_cast<void*>(request + 52), name.data(), name.size()) ||
			!safemem::ReadBytes(reinterpret_cast<void*>(request + 41), provider.data(), provider.size()) ||
			!memchr(name.data(), 0, name.size()) || !memchr(provider.data(), 0, provider.size()))
		{
			*out = 0;
			return out;
		}
		// Do not copy the retail bearer token at request+308.
		return enqueue(out, Kind::Username, response,
			Json{ { "username", name.data() }, { "provider", provider.data() }, { "substring", read<uint8_t>(request + 8) != 0 } });
	}

	uintptr_t* subscribe(uintptr_t self, uintptr_t* out, uintptr_t accounts, unsigned count, uintptr_t response)
	{
		if (response != native<uintptr_t (*)()>(0x1058F80)()) return originalSubscribe.load()(self, out, accounts, count, response);
		Json ids = Json::array();
		if (!accounts || count > 100)
		{
			*out = 0;
			return out;
		}
		for (unsigned i = 0; i < count; ++i)
			ids.push_back(std::to_string(read<uint64_t>(accounts + 72ull * i + 48)));
		return enqueue(out, Kind::subscribe, response, Json{ { "users", ids } });
	}

	uintptr_t* message(uintptr_t self, uintptr_t* out, uint64_t target, const void* data, unsigned n)
	{
		if (data && n >= 6 && n <= 505 && !memcmp(data, "CHAT:", 5)) return chat_runtime::SendWhisper(out, target, data, n);
		if (!data || n < 6 || n > 384 || memcmp(data, "INV:", 5)) return originalMessage.load()(self, out, target, data, n);
		return enqueue(out, Kind::message, 0, Json{ { "target", std::to_string(target) }, { "bytes", hex(data, n) } });
	}

	uintptr_t* requests_impl(uintptr_t self, uintptr_t* out, uintptr_t response, bool sent)
	{
		if (response != base + (sent ? 0xB8CD4B0 : 0xB8C5990)) return originalRequests.load()(self, out, response, sent);
		return enqueue(out, Kind::requests_impl, response, Json{ { "outgoing", sent } });
	}

	uintptr_t* add_friend(uintptr_t self, uintptr_t* out, uintptr_t accounts, unsigned count, uintptr_t response)
	{
		if (response != base + 0xB8B6CF0) return originalAddFriend.load()(self, out, accounts, count, response);
		// Stock single-player friend/accept actions submit one UNO account.
		if (!accounts || count != 1)
		{
			*out = 0;
			return out;
		}
		return enqueue(out, Kind::add_friend, response, Json{ { "target", std::to_string(read<uint64_t>(accounts + 48)) } });
	}

	uintptr_t* remove_friend(uintptr_t self, uintptr_t* out, uintptr_t account, uintptr_t response)
	{
		if (response != base + 0xB8B6CC8) return originalRemoveFriend.load()(self, out, account, response);
		auto* result = enqueue(out, Kind::remove_friend, response, Json{ { "target", std::to_string(read<uint64_t>(account + 48)) } });
		// F76170 owns the MSVC by-value AccountID copy's virtual base destruction.
		native<void (*)(uintptr_t)>(0x20F64A0)(account + 56);
		native<void (*)(uintptr_t)>(0x20EBB50)(account + 56);
		return result;
	}

	uintptr_t invite_impl(uintptr_t self, unsigned controller, uintptr_t ids, uintptr_t platforms, int count)
	{
		uintptr_t result = 0;
		for (int i = 0; i < std::clamp(count, 0, 16); ++i)
		{
			const auto id = read<uint64_t>(ids + 8ull * i);
			if (controller == 0 && backendFriends.contains(id))
				result = native<uintptr_t (*)(uintptr_t, unsigned, const uint64_t*, int)>(0xF5C390)(self, controller, &id, 1);
			else
				result = originalInvite.load()(self, controller, ids + 8ull * i, platforms + 8ull * i, 1);
		}
		return result;
	}

	uintptr_t atomic_join(uintptr_t self)
	{
		if (self == base + 0x11ACD290 && !resolvedGame.is_null() && local_join_ready())
		{
			const auto bytes = resolvedGame.value("bytes", std::string{});
			const bool matches = hex(reinterpret_cast<void*>(self + 256), 108) == bytes;
			if (!matches ||
				!online_services::PrepareSocialGameJoin(resolvedGame.value("playlist", -1), reinterpret_cast<void*>(self + 256)))
			{
				LOG_WARN("Social", "resolved game admission rejected: session/playlist unavailable");
				resolvedGame = nullptr;
				return native<HsmEventFn>(0x1B302C0)(self, 1);
			}
			resolvedGame = nullptr;
		}
		return originalAtomicJoin.load()(self);
	}

	uintptr_t invite_network()
	{
		if (local_invite_authenticated()) return 0; // Keep the initialized direct transport; no retail auth restart.
		return originalInviteNetwork.load()();
	}

	uintptr_t invite_requirements(unsigned controller)
	{
		// The online-requirements parent enters before the auth child. Its retail
		// implementation promotes OFFLINE networking to retail ONLINE (15284E0), which
		// destroys the live direct transport. An authenticated backend invitation
		// already has a local provider; retain it and let the native HSM continue.
		if (controller == 0 && local_invite_authenticated())
		{
			LOG_INFO("Social", "invitation retains authenticated local provider and native transport");
			return 0;
		}
		return originalRequirements.load()(controller);
	}

	uintptr_t invite_auth(uintptr_t self)
	{
		if (self != base + 0x11ACD290 || !local_invite_authenticated()) return originalInviteAuth.load()(self);
		// Same transition F1F6A0 emits after successful provider authentication.
		// The provider here authenticated the persisted credential and live lease.
		native<void (*)(uintptr_t, int)>(0x1B302C0)(self, 29);
		write_impl<int>(self + 664, -1);
		const auto state = native<int (*)(uintptr_t)>(0x1B2F890)(self);
		write_impl<int>(self + 656, state);
		write_impl<int>(self + 660, 29);
		return state;
	}

	bool fence(int controller, uint64_t mask)
	{
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
		// Only the Join Friend prerequisite query uses this local-provider fence.
		// It requires an authenticated, current native session response, not merely
		// a UI request or a blanket Demonware success state.
		if (controller == 0 && mask == 128 && caller == 0x1527665 && local_join_ready()) return true;
		return originalFence.load()(controller, mask);
	}

	unsigned flags(int controller)
	{
		auto value = originalFlags.load()(controller);
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
		if (frontend_backend::Required()) return frontend_backend::DataRequirements(controller, value, caller);
		if (reinterpret_cast<uintptr_t>(_ReturnAddress()) - base == 0x1527EFC)
		{
			const auto original = value;
			if (controller == 0 && local_join_ready())
			{
				if (backendClockReady) value &= ~0x10u; // Real server UTC received in sync.
				value &= ~0x200u; // Retail data-center QoS is not used by this resolved local session.
				if (local_progression::Get(0).ready)
				{
					// Fixed 1.20 assets + restored earned inventory are this title
					// provider's data source. Do not mark the retail CDN/marketplace
					// tasks complete or alter their global caches.
					value &= ~0x30000u;
				}
			}
			static unsigned last = ~0u;
			if (value != last)
			{
				last = value;
				LOG_INFO("Social", "native join requirements=%x -> %x local_session=%d", original, value, int(local_join_ready()));
			}
		}
		return value;
	}

	bool apply_friends(uintptr_t response, const Json& body, bool requests)
	{
		if (!body.contains("friends") || !body["friends"].is_array() || body["friends"].size() > 100) return false;
		const auto& rows = body["friends"];
		std::set<uint64_t> ids;
		for (const auto& r : rows)
			if (!chat::Id(r, "xuid") || !chat::Text(r.value("name", std::string{}), 35) || !ids.insert(chat::Id(r, "xuid")).second)
				return false;
		native<void (*)(uintptr_t)>(requests ? 0x21127A0 : 0x2112150)(response);
		write_impl<char>(response + 8865, 0);
		memcpy(reinterpret_cast<void*>(response + 8848), "cod-shared", 11);
		for (size_t i = 0; i < rows.size(); ++i)
		{
			const auto& r = rows[i];
			const auto item = response + 9896 + i * 216;
			friendAvailability[chat::Id(r, "xuid")] = r.value("availability", 4);
			user_details(item, chat::Id(r, "xuid"), r["name"]);
			if (!set_presence(item + 144, r)) return false;
		}
		write_impl<unsigned>(response + 31496, unsigned(rows.size()));
		if (!requests)
		{
			for (auto old : backendFriends)
				if (!ids.contains(old))
					push_presence(
						Json{ { "xuid", std::to_string(old) }, { "online", false }, { "activity", "offline" }, { "joinable", false } });
			backendFriends = std::move(ids);
		}
		LOG_INFO("Friends", "native response contains %zu friends", rows.size());
		return true;
	}

	bool apply_subscriptions(uintptr_t response, const Json& body)
	{
		if (!body.contains("presence") || !body["presence"].is_array() || body["presence"].size() > 100) return false;
		const auto& rows = body["presence"];
		for (const auto& r : rows)
			if (!chat::Id(r, "xuid")) return false;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			const auto item = response + 8048 + 240 * i;
			const auto& r = rows[i];
			user_details(item + 96, chat::Id(r, "xuid"), "");
			if (!set_presence(item + 24, r)) return false;
		}
		write_impl<unsigned>(response + 32048, unsigned(rows.size()));
		return true;
	}

	bool apply_username(uintptr_t response, const Json& body)
	{
		if (!body.contains("users") || !body["users"].is_array() || body["users"].size() > 1) return false;
		const auto& rows = body["users"];
		const auto capacity = read<unsigned>(response + 52);
		const auto buffer = read<uintptr_t>(response + 40);
		write_impl<unsigned>(response + 48, 0);
		if (rows.size() > capacity || (!rows.empty() && !buffer)) return false;
		for (const auto& r : rows)
		{
			const auto id = chat::Id(r, "xuid");
			const auto name = r.value("name", std::string{});
			if (!id || !chat::Text(name, 35)) return false;
			// Use the game's linked-account deserializer on its constructed result
			// buffer. The native completion finds UNO and submits AddFriend itself.
			Json user = { { "accounts",
				Json::array(
					{ Json{ { "provider", "uno" }, { "accountID", id }, { "username", name }, { "created", "" }, { "updated", "" } } }) } };
			const auto text = user.dump();
			alignas(8) std::array<uint8_t, 32> parser{};
			native<void (*)(void*, const char*)>(0x20E4250)(parser.data(), text.c_str());
			const bool ok = native<bool (*)(uintptr_t, void*)>(0x20FCF50)(buffer, parser.data());
			native<void (*)(void*)>(0x20E42B0)(parser.data());
			if (!ok) return false;
		}
		write_impl<unsigned>(response + 48, unsigned(rows.size()));
		return true;
	}

	void complete_impl(const Result& r)
	{
		if (r.id == pendingAvailability)
		{
			pendingAvailability = 0;
			if (r.status == 200 && r.body.value("accepted", false) && chat::Id(r.body, "request") == r.id)
				apply_availability(r.body.value("availability", 0));
			else
				LOG_WARN("Presence", "availability update failed status=%u", r.status);
			return;
		}
		auto it = tasks.find(r.id);
		if (it == tasks.end()) return;
		const auto t = it->second;
		bool ok = r.status == 200;
		if (read<int>(t.object + 28) == 1)
		{
			if (ok && t.kind == Kind::Friends)
				ok = apply_friends(t.response, r.body);
			else if (ok && t.kind == Kind::requests_impl)
				ok = apply_friends(t.response, r.body, true);
			else if (ok && t.kind == Kind::subscribe)
				ok = apply_subscriptions(t.response, r.body);
			else if (ok && t.kind == Kind::Username)
				ok = apply_username(t.response, r.body);
			else if (ok && t.kind == Kind::list_fetch)
				ok = apply_user_list(t.response, r.body);
			else if (ok)
				ok = r.body.value("accepted", false) && chat::Id(r.body, "request") == r.id;
			write_impl<int>(t.object + 80, ok ? 0 : 1);
			write_impl<int>(t.object + 28, 2);
			LOG_INFO("Social", "native task completed request=%llu success=%d http=%u; native callback pending", r.id, int(ok), r.status);
		}
		release(t.object);
		tasks.erase(it);
	}

	void apply_sync(const Json& body)
	{
		if (!body.contains("presence") || !body["presence"].is_array() || body["presence"].size() > 100 || !body.contains("events") ||
			!body["events"].is_array() || body["events"].size() > 16)
			return;
		const auto nextRevision = body.value("revision", uint64_t{});
		if (nextRevision != revision)
		{
			revision = nextRevision;
			refreshLists = 7;
		}
		backendClockReady = body.value("server_time", uint64_t{}) >= 1577836800ull;
		apply_availability(body.value("availability", 0));
		for (const auto& p : body["presence"])
			push_presence(p);
		for (const auto& e : body["events"])
		{
			const auto id = chat::Id(e, "id"), from = chat::Id(e, "from");
			if (!id || id <= eventAck || !from) continue;
			const auto encoded = e.value("bytes", std::string{});
			// Native callbacks accept a pointer without a length. Validate again at
			// this trust boundary so a truncated service response cannot overread.
			if (!social::ValidInviteEnvelope(encoded))
			{
				eventAck = id;
				LOG_WARN("Social", "discarded malformed invitation event=%llu", id);
				continue;
			}
			const auto data = unhex(encoded);
			const unsigned op = uint8_t(data[5]);
			if (op == 1)
			{
				resolvedGame = nullptr;
				if (data.size() == 138 && data[6] == 1)
				{
					verifiedJoinTarget = from;
					verifiedJoinUntil = GetTickCount64() + 60000;
					if (e.contains("resolved") && e["resolved"].is_object() && e["resolved"].value("kind", std::string{}) == "game")
					{
						const auto& r = e["resolved"];
						social::Session s;
						s.bytes = r.value("bytes", std::string{});
						s.gameMode = uint8_t(data[9]);
						if (!chat::Hex(s.bytes, 216) || social::GameJoinReply(s) != hex(data.data(), data.size())) continue;
						resolvedGame = r;
						LOG_INFO("Social", "backend resolved friend=%llu session=%s map=%s mode=%s", from,
							r.value("session", std::string{}).c_str(), r.value("map", std::string{}).c_str(),
							r.value("mode", std::string{}).c_str());
					}
				}
				native<void (*)(uintptr_t, int, uint64_t, const void*)>(0xF1E480)(base + 0x11ACD290, 0, from, data.data());
			}
			else if (op == 0 || op == 2)
			{
				if (op == 2) incomingInvites[from] = GetTickCount64() + 300000;
				native<void (*)(uintptr_t, int, uint64_t, const char*, const void*)>(0xF5ABE0)(
					base + 0x415B600, 0, from, e.value("name", std::string{}).c_str(), data.data());
			}
			else
				continue;
			eventAck = id;
			LOG_INFO("Social", "native invitation callback event=%llu sender=%llu opcode=%u", id, from, op);
		}
		lastContact = GetTickCount64();
	}
}
