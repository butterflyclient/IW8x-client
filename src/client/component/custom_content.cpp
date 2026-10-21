#include "client/loader/component_loader.hpp"
#include "common/state.h"
#include "diagnostics/night/night_script_range.h"
#include "diagnostics/night/night_builtins.h"
#include "diagnostics/night/night_physics.h"
#include "diagnostics/night/night_files.h"
#include "network/native_network.h"
#include "dedicated/dedicated.h"
#include "common/console.h"
#include <windows.h>
#include <filesystem>

namespace custom_content
{
	// The original hook priority must precede joins (priority 80). Joins
	// detours Com_Error_Internal, whose unmodified prologue NightBuiltins
	// validates at 0x12AB1C0. Moving these hooks below content (priority 70)
	// would cause a deterministic signature failure during startup.
	constexpr auto kPriority = static_cast<component_priority>(110);
	static_assert(static_cast<unsigned>(kPriority) >
		static_cast<unsigned>(component_priority::joins));

	class component final : public component_interface
	{
		unsigned step_ = 0;
		bool registry_ready_ = false;
	public:
		hook::Status post_unpack() override
		{
			if (dedicated::Enabled()) return hook::Status::Installed;
			wchar_t config[MAX_PATH]{};
			if (!native_network::ConfigurationPath(config, MAX_PATH)) return hook::Status::Failed;
			if (!night_files::requested(config)) return hook::Status::Installed;
			// Previously the map registry was initialized only inside
			// fastfile::Install (content priority 70), long after this component
			// at priority 110. Consequently MapEnabled("mp_escape2") was false,
			// NightPhysics silently skipped its hooks, and EscapeShapes later
			// failed fast on the first verified new shape. Prepare the exact
			// checksum-verified registry BEFORE installing the port hooks.
			// fastfile::Install will independently revalidate the same files.
			if (!registry_ready_)
			{
				wchar_t image[MAX_PATH]{};
				const auto length = GetModuleFileNameW(nullptr, image, MAX_PATH);
				if (!length || length >= MAX_PATH ||
					!night_files::Initialize(std::filesystem::path(image).parent_path(), config))
					return hook::Status::Failed;
				registry_ready_ = true;
				LOG_INFO("CustomContent", "verified map registry prepared before GSC and physics hooks");
			}
			while (step_ < 3)
			{
				const auto base = state::moduleBase.load();
				const auto status = step_ == 0 ? night_script_range::Install(base)
					: step_ == 1 ? night_builtins::Install(base) : night_physics::Install(base);
				if (status != hook::Status::Installed) return status;
				++step_;
			}
			return hook::Status::Installed;
		}
		component_priority priority() override { return kPriority; }
	};
}
REGISTER_COMPONENT(custom_content::component)
