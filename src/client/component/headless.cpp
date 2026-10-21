#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "dedicated/headless/headless.h"
#include "dedicated/dedicated.h"
#ifndef IW8X_CLIENT_ONLY

namespace headless_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			return dedicated::Enabled() ? headless::Install(state::moduleBase.load()) : hook::Status::Installed;
		}
		component_priority priority() override { return component_priority::headless; }
	};
}
REGISTER_COMPONENT(headless_component::component)
#endif
