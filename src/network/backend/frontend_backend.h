#pragma once
#include "network/match_http.h"

#include "engine/inline_hook.h"
namespace frontend_backend
{
	hook::Status Install(uintptr_t base, bool dedicated);
	void Configure(bool dedicated, const std::wstring& origin);
	void Pump();
	void Shutdown();
	bool Required();
	bool Authenticated();
	bool Premium();
	bool ServerPlaylistAllowed(int playlist);
	std::wstring Origin();
	// Provider-specific requirements consumed by the stock online-data fence.
	unsigned DataRequirements(int controller, unsigned nativeFlags, uintptr_t caller);
	match_http::Reply Request(std::string_view body, uint32_t peer);
	bool HandleCommand(std::string_view command);
}
