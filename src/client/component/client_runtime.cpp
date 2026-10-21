#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "dedicated/dedicated.h"
#include "client/online_frontend.h"
#include "client/lua/lua_injection.h"
#include "engine/noclip.h"
#include "client/client_console.h"
#include "client/console/game_console.hpp"

namespace client_runtime
{
	class component final : public component_interface
	{
		unsigned step_ = 0;
	public:
		hook::Status post_unpack() override
		{
			const auto base = state::moduleBase.load();
			// Retain partial progress when a verified native binding is not ready.
			while (step_ < 4)
			{
				auto result = hook::Status::Installed;
				if (step_ == 0 && !dedicated::Enabled()) result = online_frontend::Install(base);
				if (step_ == 1 && !dedicated::Enabled()) result = lua_injection::Install(base);
				if (step_ == 2) result = noclip::Install(base);
				if (step_ == 3 && !dedicated::Enabled()) result = clientconsole::Install(base);
				if (result != hook::Status::Installed) return result;
				++step_;
			}
			return hook::Status::Installed;
		}
		component_priority priority() override { return component_priority::client_runtime; }
		void pre_destroy() override
		{
			game_console::shutdown();
		}
	};
}
REGISTER_COMPONENT(client_runtime::component)
