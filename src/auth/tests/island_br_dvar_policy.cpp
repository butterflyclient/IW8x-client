#include "auth/island_br_dvar_policy.hpp"

#include <iostream>
#include <stdexcept>

int main()
{
	using namespace dvars::island_br_policy;
	unsigned checks{};
	const auto verify = [&](const bool condition)
	{
		if (!condition) throw std::runtime_error("Island BR dvar script permission guard failed");
		++checks;
	};
	for (const auto allowed : source_script_settings)
	{
		verify(name_allowed(allowed));
		verify(restore_allowed(allowed, external_flag, external_string_type));
		verify(!restore_allowed(allowed, external_flag | script_permission_flag, external_string_type));
		verify(!restore_allowed(allowed, external_flag, 7));
		verify(!restore_allowed(allowed, 0, external_string_type));
		verify(!restore_allowed(allowed, 0x200, external_string_type));
		// The actual Replay 1.20 Scr_SetDvar source passes 0x80488 to
		// native SetDvar (caller RVA 0x12325BD). A source external
		// string with 0x110 still fails; our exact-name permission must
		// intersect this native mask without affecting other variables.
		verify((external_flag & replay_native_script_set_mask) == 0);
		verify(((external_flag | 0x10) & replay_native_script_set_mask) == 0);
		verify(((external_flag | script_permission_flag) & replay_native_script_set_mask) == script_permission_flag);
	}
	for (const auto denied : { "scr_br_radarHide", "scr_br_other", "r_fullscreen", "com_lan_lobby_enabled" })
	{
		verify(!name_allowed(denied));
		verify(!restore_allowed(denied, external_flag, external_string_type));
	}
	std::cout << "Island source BR exact-name policy against Replay 1.20.4 native Dvar mask: " << checks << " checks passed\n";
}
