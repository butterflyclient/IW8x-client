#pragma once
#include "network/profile/profile_store.h"
namespace profile_backend
{
	void ResetCache();
	unsigned Request(uint64_t owner, const profile_store::Json& request, profile_store::Json& response);
}
