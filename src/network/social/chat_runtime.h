#pragma once
#include "engine/inline_hook.h"

#include "network/match_http.h"

#include <string>
#include <string_view>
namespace chat_runtime
{
	hook::Status Install(uintptr_t base, bool dedicated);
	void ClientPump(const std::wstring& origin, std::string gameSession);
	void PublishDedicated(std::string session);
	match_http::Reply Request(std::string_view path, std::string_view body, uint32_t ip);
	void Shutdown();
	bool VoicePermission();
	bool HandleCommand(std::string_view command);
	// Called by the existing TU19 DW instant-message adapter for CHAT: packets.
	uintptr_t* SendWhisper(uintptr_t* output, uint64_t target, const void* bytes, unsigned size);
}
