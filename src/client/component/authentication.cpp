#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "auth/offline_auth.h"
#include <windows.h>

namespace authentication_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_load() override
		{
			HMODULE module{};
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&state::moduleBase), &module))
				return hook::Status::Failed;
			return offlineauth::Prepare(module) ? hook::Status::Installed : hook::Status::Failed;
		}
		hook::Status post_unpack() override
		{
			return offlineauth::Install(state::moduleBase.load());
		}
		component_priority priority() override { return component_priority::authentication; }
	};
}
REGISTER_COMPONENT(authentication_component::component)
