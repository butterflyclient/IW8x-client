#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "auth/dvar_patches.h"

namespace dvars_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			return dvars::Install(state::moduleBase.load());
		}
		component_priority priority() override { return component_priority::dvars; }
	};
}
REGISTER_COMPONENT(dvars_component::component)
