#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "dedicated/dedicated.h"
#include "diagnostics/fastfile.h"
#include "client/frontend/menu_art1645.h"
#include "client/frontend/custom_map_names.h"

#include "dedicated/assets/bap.h"
#include "diagnostics/escape/escape_shapes.h"
#include "diagnostics/night/night_files.h"
#include "diagnostics/night/night_entry_pool.h"
#include "diagnostics/night/night_animtrees.h"
#include "diagnostics/night/night_loot.h"
#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
#include "diagnostics/night/night_geometry.h"
#include "diagnostics/night/night_light_bits.h"
#include "diagnostics/night/night_frame_lights.h"
#include "client/frontend/frontend_animtree128.h"
#include "gsc/compiled/compat/script_link_diag.h"
#include "client/frontend/frontend_render128.h"
#endif

namespace content
{
	class component final : public component_interface
	{
		unsigned step_ = 0;
	public:
		hook::Status post_unpack() override
		{
			const auto base = state::moduleBase.load();
			while (step_ < 15)
			{
				auto status = hook::Status::Installed;
				switch (step_)
				{
				case 0: status = fastfile::Install(base); break;
				case 1: status = escape_shapes::Install(base); break;
					case 2:
						// Dedicated BAP hosts use the auxiliary entry router rather
						// than relocating the native stock table. Other converted-map
						// runtimes still must validate the expanded table.
						// IW8X_CLIENT_ONLY does not link the dedicated BAP startup
						// module. Do not reference its Enabled() symbol in a client
						// target: the dedicated-router exemption cannot apply there.
#ifndef IW8X_CLIENT_ONLY
						if (night_files::Enabled() && !(dedicated::Enabled() && bap::Enabled()) && !night_entry_pool::Report())
#else
						if (night_files::Enabled() && !night_entry_pool::Report())
#endif
							status = hook::Status::Failed;
						break;
				case 3: if (night_files::Enabled()) status = bap::InstallNightCollision(base); break;
				case 5: status = night_animtrees::Install(base); break;
				case 6: status = night_loot::Install(base); break;
#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
				case 4: status = night_geometry::Install(base); break;
				case 7: if (!dedicated::Enabled()) status = night_light_bits::Install(base); break;
				case 8: if (!dedicated::Enabled()) status = night_frame_lights::Install(base); break;
				case 9: if (!dedicated::Enabled()) status = frontend_animtree128::Install(base); break;
				case 10: if (!dedicated::Enabled()) status = script_link_diag::Install(base); break;
				case 11: if (!dedicated::Enabled()) status = frontend_render128::Install(base); break;
#endif
				case 12: if (!dedicated::Enabled()) status = custom_map_names::install(base); break;
#ifdef IW8X_ISLAND_PORT_PROBE
				case 13:
					// The experimental Island DX12 pipeline hooks significantly
					// degrade client performance. Keep the normal custom-content
					// loader, but never install those renderer detours.
					break;
#endif
					case 14:
							// Progression independently verifies stock StringTable_Lookup
							// bytes at 0x13E2BA0 before installing its own handlers.
							// Installing the Island parent detour here (priority 70)
							// made progression (priority 40) reject the modified prologue.
							// The Island hook is installed after progression's verified
							// binding stage; never weaken either component's byte checks.
							status = menu_art1645::Install(base);
						break;
				}
				if (status != hook::Status::Installed) return status;
				++step_;
			}
			return hook::Status::Installed;
		}
		component_priority priority() override { return component_priority::content; }
	};
}
REGISTER_COMPONENT(content::component)
