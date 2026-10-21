#pragma once

#include <cstdint>

// Opt-in, map-scoped policy for a future validated render-state translation.
// The adapter never changes native state until a caller explicitly enables it.
namespace island_renderer_adapter
{
	struct draw_context
	{
		const char* map_name{};
		bool imported_island_material{};
		bool paired_prepass_verified{};
	};

	struct decision
	{
		bool eligible{};
		bool translate_depth{};
	};

	void set_enabled(bool enabled) noexcept;
	bool enabled() noexcept;
		// Explicit experimental main-pass LESS_EQUAL compatibility mode.
		// This may reveal missing prepass coverage, but is NOT a geometry fix.
		void set_depth_preview(bool enabled) noexcept;
		bool depth_preview() noexcept;
	decision classify(const draw_context& context) noexcept;
}
