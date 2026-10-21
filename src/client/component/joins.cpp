#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "diagnostics/join.h"

namespace joins_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			return joindiag::Install(state::moduleBase.load());
		}
		component_priority priority() override { return component_priority::joins; }
	};
}
REGISTER_COMPONENT(joins_component::component)
