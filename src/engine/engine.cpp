#include "engine/engine.hpp"
#include "common/state.h"

#include <cstring>

namespace engine
{
	void draw_text_top_right(const char* text, float scale, float opacity)
	{
		using draw_text_fn = void(__fastcall*)(const void*, const char*, int, void*,
			float, float, int, int, float, const float*, int);
		using text_width_fn = int(__fastcall*)(const char*, int, void*, float);

		const auto base = state::moduleBase.load();
		const auto mode = *reinterpret_cast<const int*>(base + 0xef2de9c);
		const auto* placement = reinterpret_cast<const float*>(base + (mode == 2 ? 0xef2df10 : 0xef2dea0));

		auto* font = reinterpret_cast<void*(__fastcall*)(int)>(base + 0x1ba0d90)(4);
		if (!font || placement[0] <= 0.0f || placement[1] <= 0.0f) return;

		const auto length = static_cast<int>(std::strlen(text));
		const float color[] = { 1.0f, 1.0f, 1.0f, opacity };
		constexpr float shadow[] = { 0.0f, 0.0f, 0.0f, 0.8f };

		const auto width = reinterpret_cast<text_width_fn>(base + 0x1b9ec40)(text, length, font, scale);
		const auto x = -static_cast<float>(width) - 12.0f;
		const auto y = 12.0f + 32.0f * scale;
		const auto draw_text = reinterpret_cast<draw_text_fn>(base + 0x1b9cbe0);

		draw_text(placement, text, length, font, x + 1.0f, y + 1.0f, 10, 8, scale, shadow, 0);
		draw_text(placement, text, length, font, x, y, 10, 8, scale, color, 0);
	}
}
