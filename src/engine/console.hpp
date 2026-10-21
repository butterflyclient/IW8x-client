#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::console
{
	struct frame
	{
		void* font{};
		void* white{};
		float width{}, height{}, scale{};
		int font_height{};
	};

	struct dvar_info
	{
		bool exists{};
		std::string current, reset, domain;
		std::vector<std::string> values;
	};

	bool initialize(uintptr_t base);
	bool active();
	void toggle();
	frame get_frame();
	void draw_text(const frame& frame, const char* text, float x, float y, const float* color);
	float text_width(const frame& frame, const char* text);
	void draw_rect(const frame& frame, float x, float y, float width, float height, const float* color);
	void for_each_command(void (*callback)(const char*));
	void for_each_dvar(void (*callback)(const char*));
	dvar_info describe_dvar(const char* name);
}
