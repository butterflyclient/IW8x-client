#pragma once
#include "engine/inline_hook.h"

#include "network/match_http.h"
namespace publisher_variables
{
	hook::Status Install(uintptr_t base, bool dedicated);
	void Configure(const std::wstring& origin);
	void Shutdown();
	match_http::Reply Request(std::string_view body);
	bool HandleCommand(std::string_view command);
}
