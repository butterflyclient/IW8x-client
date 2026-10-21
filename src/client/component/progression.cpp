#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "auth/profile/local_progression.h"
#include "dedicated/dedicated.h"
#include "diagnostics/night/night_files.h"

namespace progression_component
{
	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
				const auto base = state::moduleBase.load();
				const auto status = local_progression::Install(base, dedicated::Enabled());
				if (status != hook::Status::Installed) return status;
				// The native parent lookup shares StringTable_Lookup with
				// progression. Delay the Island-only hook until the progression
				// signature verification has succeeded, preserving both guards.
				return night_files::InstallIslandNativeZoneParent(base);
		}
		component_priority priority() override { return component_priority::progression; }
	};
}
REGISTER_COMPONENT(progression_component::component)
