#include "engine/console.hpp"
#include "engine/safemem.h"

#include <array>
#include <cstring>

namespace engine::console
{
	namespace
	{
		uintptr_t image_base{};
		template <typename T> T native(uintptr_t rva)
		{
			return reinterpret_cast<T>(image_base + rva);
		}
		struct binding
		{
			uintptr_t rva;
			const char* bytes;
			size_t size;
		};
		constexpr binding bindings[]{
			{ 0x1965330, "\x4C\x8B\xDC\x48\x81\xEC\xA8\x00\x00\x00", 10 },
			{ 0x1964CD0, "\x48\x8B\x44\x24\x50\x48\x8B\x0D\x74\x96\x7E\x0F", 12 },
			{ 0x1932DA0, "\x4C\x8B\xDC\x4D\x89\x43\x18\x53\x56\x41\x55", 11 },
			{ 0x1BA0D90, "\x83\xF9\x02\x75\x08", 5 },
			{ 0x15B0EF0, "\x48\x83\xEC\x28\xBA\x01\x00\x00\x00", 9 },
			{ 0x15B18C0, "\x48\x83\xEC\x28\x48\x8D\x0D\x05\x48\x8F\x0D", 11 },
			{ 0x1297BB0, "\x48\x89\x5C\x24\x08\x57\x48\x83\xEC\x20", 10 },
			{ 0x13E6590, "\x48\x89\x5C\x24\x10\x56\x48\x83\xEC\x20", 10 },
			{ 0x13E63A0, "\x48\x83\xEC\x28\xE8\x17\x03\x00\x00", 9 },
			{ 0x13E5D10, "\x48\x83\xEC\x38\x0F\x10\x41\x28", 8 },
			{ 0x13E5CF0, "\x48\x83\xEC\x38\x0F\x10\x41\x48", 8 },
			{ 0x13E5D30, "\xE9\x0B\x00\x00\x00", 5 },
			// Pins the stock console's white material slot at 0xEEB0C70.
			{ 0x15AD586, "\x48\x8B\x05\xE3\x36\x90\x0D", 7 },
		};
	}

	bool initialize(uintptr_t base)
	{
		for (const auto& binding : bindings)
		{
			std::array<char, 16> bytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), bytes.data(), binding.size) ||
				std::memcmp(bytes.data(), binding.bytes, binding.size)) return false;
		}
		image_base = base;
		return true;
	}

	bool active()
	{
		return image_base && native<bool (*)(int)>(0x15B0EF0)(0);
	}
	void toggle()
	{
		native<void (*)()>(0x15B18C0)();
	}

	frame get_frame()
	{
		frame result;
		if (!image_base) return result;
		const auto mode = *native<const int*>(0xEF2DE9C);
		const auto* placement = native<const float*>(mode == 2 ? 0xEF2DF10 : 0xEF2DEA0);
		// Enum 5 is the renderer-owned Fira Mono Regular console font.
		result.font = native<void* (*)(int)>(0x1BA0D90)(5);
		result.white = *native<void**>(0xEEB0C70);
		result.width = placement[8];
		result.height = placement[9];
		if (result.font)
		{
			result.font_height = *reinterpret_cast<const int*>(static_cast<const char*>(result.font) + 8);
			if (result.font_height > 0) result.scale = 18.0f / result.font_height;
		}
		return result;
	}

	void draw_text(const frame& frame, const char* text, float x, float y, const float* color)
	{
		using draw = void (*)(const char*, int, void*, int, float, float, float, float, float, const float*);
		native<draw>(0x1965330)(text, 0x7FFF, frame.font, frame.font_height, x, y, frame.scale, frame.scale, 0, color);
	}
	float text_width(const frame& frame, const char* text)
	{
		return native<int (*)(const char*, int, void*)>(0x1932DA0)(text, 0, frame.font) * frame.scale;
	}
	void draw_rect(const frame& frame, float x, float y, float width, float height, const float* color)
	{
		using draw = void (*)(float, float, float, float, float, float, float, float, const float*, void*);
		native<draw>(0x1964CD0)(x, y, width, height, 0, 0, 0, 0, color, frame.white);
	}
	void for_each_command(void (*callback)(const char*))
	{
		native<void (*)(decltype(callback))>(0x1297BB0)(callback);
	}
	void for_each_dvar(void (*callback)(const char*))
	{
		native<void (*)(decltype(callback))>(0x13E6590)(callback);
	}

	dvar_info describe_dvar(const char* name)
	{
		dvar_info info;
		const auto* dvar = native<const char* (*)(const char*)>(0x13E63A0)(name);
		if (!dvar) return info;
		info.exists = true;
		info.current = native<const char* (*)(const void*)>(0x13E5D10)(dvar);
		info.reset = native<const char* (*)(const void*)>(0x13E5CF0)(dvar);
		std::array<char, 2048> domain{};
		const auto* description = native<const char* (*)(const void*, char*, size_t, int*)>(0x13E5D30)(dvar, domain.data(), domain.size(), nullptr);
		info.domain = description ? description : "";
		const auto type = static_cast<unsigned char>(dvar[0x1D]);
		if (type == 0) info.values = { "0", "1" };
		else if (type == 8)
		{
			int count{};
			const char* const* strings{};
			if (safemem::ReadBytes(dvar + 0x58, &count, sizeof(count)) && count > 0 && count <= 256 &&
				safemem::ReadBytes(dvar + 0x60, &strings, sizeof(strings)) && strings)
			{
				for (int i = 0; i < count; ++i)
				{
					const char* value{};
					char text[256]{};
					if (safemem::ReadBytes(strings + i, &value, sizeof(value)) && safemem::ReadString(value, text, sizeof(text)))
						info.values.emplace_back(text);
				}
			}
		}
		return info;
	}
}
