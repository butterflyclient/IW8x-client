#pragma once
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
namespace social_runtime::detail
{
	using social::Json;

	enum class Kind
	{
		Friends,
		subscribe,
		message,
		requests_impl,
		add_friend,
		remove_friend,
		availability,
		Matchmaking,
		Username,
		Frontend,
		list_fetch,
		list_mutation
	};

	struct Work
	{
		uint64_t id;
		Kind kind;
		Json body;
	};

	struct Result
	{
		uint64_t id;
		unsigned status;
		Json body;
	};

	struct Task
	{
		uintptr_t object = 0, response = 0;
		Kind kind;
		uint64_t at = 0;
	};

	using FriendsFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t, const char*);

	using SubscribeFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t, unsigned, uintptr_t);

	using MessageFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uint64_t, const void*, unsigned);

	using RequestsFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t, bool);

	using RemoveFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t, uintptr_t);

	using UsernameFn = uintptr_t* (*)(uintptr_t, uintptr_t*, uintptr_t, uintptr_t);

	using UsernameBearerFn = uintptr_t (*)(uintptr_t, const char*);

	using InviteFn = uintptr_t (*)(uintptr_t, unsigned, uintptr_t, uintptr_t, int);

	using FenceFn = bool (*)(int, uint64_t);

	using FlagsFn = unsigned (*)(int);

	using AuthFn = uintptr_t (*)(uintptr_t);

	using NetworkInviteFn = uintptr_t (*)();

	using RequirementsFn = uintptr_t (*)(unsigned);

	using HsmEventFn = uintptr_t (*)(uintptr_t, int);

	using AvailabilityGetFn = int (*)(uint64_t);

	using AvailabilitySetFn = char (*)(int);

// Included inside social_runtime's private namespace after Enqueue. Verified
// Replay service wrappers: retain UserList queues, TaskManager callbacks and
// Social/voice UI. HTTP runs on the existing worker, never the engine thread.
#include "network/social/user_list_bindings.h"
	using ListFetchFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, const char*, uintptr_t);

	using ListCreateFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, uintptr_t);

	using ListAddFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, uintptr_t, unsigned, bool, unsigned, uintptr_t);

	using ListRemoveFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, uintptr_t, unsigned, uintptr_t);

	using RecentCountFn = unsigned (*)(uintptr_t, int);
}
