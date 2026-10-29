#pragma once

namespace client::mp_dvar_registration
{
	template <typename Ready, typename Register>
	bool ensure(Ready ready, Register register_dvars)
	{
		if (ready()) return true;
		register_dvars();
		return ready();
	}
}
