#pragma once
#include "network/social/runtime/types.hpp"
namespace social_runtime::detail
{
	extern uintptr_t base;

	extern bool installed, server, storageReady;

	extern social::Backend service;

	extern party_relay120::Service relayService;

	extern std::mutex serviceLock, clientLock;

	extern std::atomic<bool> stopping;

	extern std::atomic<uint64_t> lastContact, eventAck;

	extern matchmaking::BackendLatency matchmakingLatency;

	extern std::wstring origin;

	extern std::wstring communicationsOrigin;

	extern std::string communicationsLease;

	extern uint64_t communicationsAt;

	extern Json snapshot;

	extern uint64_t snapshotAt;

	extern uint64_t sequence, revision;

	extern unsigned refreshLists;

	extern std::deque<Work> outgoing;

	extern std::deque<Result> completed;

	extern std::map<uint64_t, Task> tasks;

	extern std::atomic<FriendsFn> originalFriends;

	extern std::atomic<SubscribeFn> originalSubscribe;

	extern std::atomic<MessageFn> originalMessage;

	extern std::atomic<RequestsFn> originalRequests;

	extern std::atomic<SubscribeFn> originalAddFriend;

	extern std::atomic<RemoveFn> originalRemoveFriend;

	extern std::atomic<UsernameFn> originalUsername;

	extern std::atomic<UsernameBearerFn> originalUsernameBearer;

	extern std::atomic<InviteFn> originalInvite;

	extern std::atomic<FenceFn> originalFence;

	extern std::atomic<FlagsFn> originalFlags;

	extern std::atomic<AuthFn> originalInviteAuth;

	extern std::atomic<NetworkInviteFn> originalInviteNetwork;

	extern std::atomic<RequirementsFn> originalRequirements;

	extern std::atomic<AuthFn> originalAtomicJoin;

	extern std::atomic<AvailabilityGetFn> originalAvailability, originalPendingAvailability;

	extern std::atomic<AvailabilitySetFn> originalSetAvailability;

	extern int ownAvailability;

	extern uint64_t pendingAvailability;

	extern std::map<uint64_t, int> friendAvailability;

	extern std::set<uint64_t> backendFriends;

	extern uint64_t verifiedJoinTarget, verifiedJoinUntil;

	extern Json resolvedGame;

	extern std::map<uint64_t, uint64_t> incomingInvites;

	extern bool backendClockReady;

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
	void write_impl(uintptr_t address, T v)
	{
		memcpy(reinterpret_cast<void*>(address), &v, sizeof(v));
	}

	std::string hex(const void* data, size_t n);

	std::string unhex(std::string_view s);

	std::string random();

	bool connected();

	bool atomic_file(const std::filesystem::path& path, std::string_view bytes);

	bool save_service();

	std::string credential();

	uintptr_t* enqueue(uintptr_t* out, Kind kind, uintptr_t response, Json request);

	extern std::atomic<ListFetchFn> originalListFetch;

	extern std::atomic<ListCreateFn> originalListCreate;

	extern std::atomic<ListAddFn> originalListAdd;

	extern std::atomic<ListRemoveFn> originalListRemove;

	extern std::atomic<RecentCountFn> originalRecentCount;

	inline constexpr const char* userListNames[] = { "recentPlayers", "blocklistPlayers", "mutedPlayers" };

	unsigned recent_count(uintptr_t self, int controller);

	int own_user_list(const char* name, uintptr_t response, size_t offset);

	void pump_user_lists();

	uintptr_t local_username_search();

	bool local_join_ready();

	bool local_invite_authenticated();

	void user_details(uintptr_t details, uint64_t id, const std::string& name);

	std::string base64_id(uint64_t id);

	Json rich_presence(const Json& p);

	bool set_presence(uintptr_t info, const Json& p);

	void push_presence(const Json& p);
}
