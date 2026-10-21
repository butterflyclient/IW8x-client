#include "diagnostics/island/island_renderer_adapter.hpp"

#include <atomic>
#include <cstring>

namespace island_renderer_adapter
{
	namespace
	{
		std::atomic<bool> opt_in{false};
		std::atomic<bool> experimental_less_equal{false};
	}

	void set_enabled(bool enabled) noexcept { opt_in.store(enabled, std::memory_order_release); }
	bool enabled() noexcept { return opt_in.load(std::memory_order_acquire); }
	void set_depth_preview(bool enabled) noexcept { experimental_less_equal.store(enabled, std::memory_order_release); }
	bool depth_preview() noexcept { return experimental_less_equal.load(std::memory_order_acquire); }

	decision classify(const draw_context& context) noexcept
	{
		const bool island = context.map_name &&
			std::strcmp(context.map_name, "mp_sm_island_1") == 0;
		const bool preview = enabled() && depth_preview() && island &&
			context.imported_island_material;
		const bool eligible = enabled() && island && context.imported_island_material &&
			(context.paired_prepass_verified || preview);
		// The preview is a deliberately imperfect Island-only control. Production
		// EQUAL depth is preserved unless both process flags explicitly opt in.
		return {eligible, preview};
	}
}
