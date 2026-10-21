#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "network/online_services.h"

namespace online_services_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override { return online_services::Install(state::moduleBase.load()); }
		void pre_destroy() override { online_services::Stop(); }
		component_priority priority() override { return component_priority::online_services; }
	};
}
REGISTER_COMPONENT(online_services_component::component)
