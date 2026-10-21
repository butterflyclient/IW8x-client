#pragma once
#include "network/commerce/custom_mod_store.h"
namespace custom_mod_backend
{
	bool Start();
	unsigned Request(uint64_t xuid, const custom_mods::Json& request, custom_mods::Json& response);
	void Reset();
	void Pump();

	struct View
	{
		bool ready = false, pending = false;
		uint64_t revision = 0;
		std::string status;
	};

	View Status();
	std::string Field(const std::string& path);
	bool Change(std::string_view json);
	bool Command(std::string_view command, bool server);
}
