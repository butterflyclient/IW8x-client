#pragma once
#include "thirdparty/json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
namespace camo_changer
{
	bool Initialize(uintptr_t base);
	// Called only by the existing G_CmdsMP_ClientCommand detour.
	void ClientCommand(int slot);
	// These access published values; no HTTP or UI thread reads server playerstate.
	nlohmann::json ServerStatus(uint64_t owner);
	void Pump();
	std::string Status();
	std::string Catalog();
	bool Submit(std::string_view action, std::string_view camo = {});
}
