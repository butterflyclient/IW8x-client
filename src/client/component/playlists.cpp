#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "network/playlists.h"

namespace playlists_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			return playlists::Install(state::moduleBase.load());
		}
		component_priority priority() override { return component_priority::playlists; }
	};
}
REGISTER_COMPONENT(playlists_component::component)
