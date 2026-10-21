#pragma once
#include "network/profile/settings_profile.h"
namespace settings_backend
{
	void ResetCache();
	unsigned Request(uint64_t owner, const settings120::Json& request, settings120::Json& response);
}
