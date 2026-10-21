#pragma once
#include <cstdint>
#include <string_view>
namespace weapon_loadouts
{
	// Client main thread, after resident DDL assets exist and before stats init.
	bool Prepare(uintptr_t base);
	bool Ready();
	// Available only after the verified metadata has been admitted. No guessed
	// prefix or arbitrary later-build reference is accepted as a ported weapon.
	const char* Category(std::string_view reference);
}
