#include "client/loader/component_loader.hpp"
#include "common/console.h"
#include "common/state.h"
#include "dedicated/dedicated.h"
#include "engine/addresses.hpp"
#include "engine/safemem.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace frontend_frame_limit
{
	namespace
	{
		using frame_fn = void(__fastcall*)();
		std::atomic<frame_fn> original{ nullptr };
		uintptr_t base = 0;
		constexpr auto frame_interval = std::chrono::nanoseconds(16'666'667);

		bool in_frontend()
		{
			return reinterpret_cast<bool(__fastcall*)()>(base + game::kIsInFrontEndRVA)();
		}

		void __fastcall frame()
		{
			const auto native_frame = original.load(std::memory_order_acquire);
			if (!in_frontend())
			{
				native_frame();
				return;
			}

			// Include native work and its own limiter in the 60 FPS frame budget.
			// The frontend-mode flag stays separate from the loaded 3D scene state.
			const auto deadline = std::chrono::steady_clock::now() + frame_interval;
			native_frame();
			if (in_frontend()) std::this_thread::sleep_until(deadline);
		}
	}

	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			if (dedicated::Enabled()) return hook::Status::Installed;
			base = state::moduleBase.load();
			uint8_t actual[sizeof(game::kIsInFrontEndPrologue)]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kIsInFrontEndRVA), actual, sizeof(actual)) ||
				std::memcmp(actual, game::kIsInFrontEndPrologue, sizeof(actual)))
				return hook::Status::NotReady;

			const auto result = hook::Install(reinterpret_cast<void*>(base + game::kComFrameRVA), &frame,
				game::kComFramePrologue, sizeof(game::kComFramePrologue), original);
			if (result == hook::Status::Installed)
				LOG_INFO("ClientFPS", "frontend capped at 60 FPS; gameplay frame-rate settings retained");
			return result;
		}
		component_priority priority() override { return component_priority::client_runtime; }
	};
}
REGISTER_COMPONENT(frontend_frame_limit::component)
