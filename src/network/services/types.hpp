#pragma once
#include <winsock2.h>
#include <windows.h>

#include "network/online_services.h"
#include "network/online_services_protocol.h"
#include "network/match_assignment.h"
#include "network/match_connection.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace online_services::detail
{
	struct NetConfig
	{
		int flow;
		uint8_t relayDisabled, signalForced, padding[2];
	};

	static_assert(sizeof(NetConfig) == 8);

	using StartFn = void (*)(const NetConfig*, void*);

	using CallbackFn = bool (*)(bool, int, int*);

	using NetErrorFn = void (*)(int);

	using SessionFn = void (*)(void*);

	using DialogStateFn = uintptr_t (*)();
}
