// IW7-Mod's native in-game console layout, controls and bounded scrollback.
// Replay supplies drawing/input bindings; the existing client routes commands.
#include "client/console/game_console.hpp"
#include "client/console/autocomplete.hpp"
#include "client/client_console.h"
#include "engine/console.hpp"
#include "engine/safemem.h"
#include "common/console.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace game_console
{
	namespace
	{
		constexpr float white[]{ 1, 1, 1, 1 }, title[]{ .9f, .9f, 0, 1 }, ghost[]{ .5f, .5f, .5f, 1 };
		constexpr float input_color[]{ .2f, .2f, .2f, .9f }, hint_color[]{ .3f, .3f, .3f, 1 };
		constexpr float output_color[]{ .25f, .25f, .25f, .85f }, bar_color[]{ .5f, .5f, .5f, .6f };
		constexpr float command_color[]{ .8f, .8f, 1, 1 }, dvar_color[]{ 1, 1, .8f, 1 }, selected_color[]{ 1, .8f, .3f, 1 };
		constexpr float highlight[]{ 1, 1, 1, .12f };
		constexpr float line_height = 18;
		using char_event_fn = void (*)(int, unsigned);
		using draw_console_fn = void (*)(int);
		std::atomic<char_event_fn> original_char{};
		std::atomic<draw_console_fn> original_draw{};
		std::atomic<bool> ready{}, open{};
		uintptr_t image_base{};
		submit_command submit{};
		char buffer[256]{};
		int cursor{}, history_index = -1, scroll{}, visible_lines = 1;
		bool output_visible{}, suggestions_valid{};
		autocomplete::result suggestions;
		engine::console::dvar_info details;
		std::vector<std::string> domain_lines;
		std::string details_name;
		std::mutex output_mutex;
		std::deque<std::string> output;
		uint64_t output_generation{};
		uint64_t wrapped_generation = UINT64_MAX;
		int wrapped_columns{};
		std::vector<std::string> wrapped_output;

		void clear_input()
		{
			buffer[0] = 0;
			cursor = 0;
			suggestions_valid = false;
		}
		void set_buffer(const std::string& text)
		{
			strncpy_s(buffer, text.c_str(), _TRUNCATE);
			cursor = static_cast<int>(std::strlen(buffer));
			suggestions_valid = false;
		}
		void synchronize()
		{
			const auto active = engine::console::active();
			if (open.exchange(active) == active) return;
			clear_input();
			history_index = -1;
			output_visible = false;
			if (active) autocomplete::refresh();
		}
		void toggle()
		{
			engine::console::toggle();
			synchronize();
		}
		void clear_output()
		{
			std::lock_guard lock(output_mutex);
			output.clear();
			++output_generation;
			scroll = 0;
		}
		void update_suggestions()
		{
			if (suggestions_valid) return;
			suggestions = autocomplete::query(buffer);
			details_name = suggestions.matches.size() == 1 && suggestions.matches[0].type == autocomplete::match_type::dvar
				? suggestions.matches[0].name : suggestions.command;
			details = details_name.empty() ? engine::console::dvar_info{} : engine::console::describe_dvar(details_name.c_str());
			domain_lines.clear();
			std::string_view domain = details.domain;
			while (!domain.empty() && domain_lines.size() < 16)
			{
				const auto end = domain.find('\n');
				domain_lines.emplace_back(domain.substr(0, end));
				if (end == domain.npos) break;
				domain.remove_prefix(end + 1);
			}
			suggestions_valid = true;
		}
		void execute()
		{
			const auto command = autocomplete::prepare_command(buffer);
			if (command.empty()) return;
			print("]" + std::string(buffer));
			autocomplete::add_history(buffer);
			if (command == "clear")
			{
				clear_output();
				autocomplete::clear_history();
			}
			else if (!submit(command)) print("^1Command queue is full; retry the command.");
			clear_input();
			history_index = -1;
			// Native/plugin registrations and values can change after a command.
			autocomplete::refresh();
		}
		void insert(unsigned c)
		{
			const auto length = std::strlen(buffer);
			if (c < 32 || c > 126 || length >= sizeof(buffer) - 1) return;
			std::memmove(buffer + cursor + 1, buffer + cursor, length + 1 - cursor);
			buffer[cursor++] = static_cast<char>(c);
			suggestions_valid = false;
		}
		void character(unsigned c)
		{
			if (c == 8)
			{
				if (cursor > 0)
				{
					std::memmove(buffer + cursor - 1, buffer + cursor, std::strlen(buffer) + 1 - cursor);
					--cursor;
					suggestions_valid = false;
				}
			}
			else if (c == 3)
			{
				clear_input(); clear_output(); autocomplete::clear_history(); history_index = -1;
			}
			else if (c == 22 && OpenClipboard(nullptr))
			{
				const auto handle = GetClipboardData(CF_UNICODETEXT);
				if (handle)
				{
					const auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
					if (text)
					{
						for (size_t i = 0; text[i] && i < 4096; ++i) insert(text[i]);
						GlobalUnlock(handle);
					}
				}
				CloseClipboard();
			}
			else if (c != '`' && c != '~') insert(c);
		}
		void char_event(int client, unsigned c)
		{
			if (ready.load() && client == 0 && reinterpret_cast<bool (*)()>(image_base + 0x12CF280)())
			{
				synchronize();
				if (open.load())
				{
					character(c);
					return;
				}
				if (c == '`' || c == '~') return;
			}
			original_char.load()(client, c);
		}
		void draw_native_console(int client)
		{
			// Replay's no-game-mode frontend uses Con_DrawConsole instead of DrawOverlay.
			if (client == 0 && reinterpret_cast<bool (*)()>(image_base + 0x12CF280)())
			{
				draw();
			}
		}

		void draw_box(const engine::console::frame& frame, float x, float y, float width, float height, const float* color)
		{
			// IW7's translucent panels and two-pixel dark borders.
			const float dark[]{ color[0] * .5f, color[1] * .5f, color[2] * .5f, color[3] };
			engine::console::draw_rect(frame, x, y, width, height, color);
			engine::console::draw_rect(frame, x, y, 2, height, dark);
			engine::console::draw_rect(frame, x + width - 2, y, 2, height, dark);
			engine::console::draw_rect(frame, x, y, width, 2, dark);
			engine::console::draw_rect(frame, x, y + height - 2, width, 2, dark);
		}
		void draw_output(const engine::console::frame& frame)
		{
			const auto character_width = std::max(1.0f, engine::console::text_width(frame, "W"));
			const int columns = std::clamp(static_cast<int>((frame.width - 48) / character_width), 10, 512);
			{
				std::lock_guard lock(output_mutex);
				if (wrapped_generation != output_generation || wrapped_columns != columns)
				{
					wrapped_output.clear();
					for (const auto& line : output)
					{
						std::string part;
						std::string color;
						int count{};
						for (size_t i = 0; i < line.size(); ++i)
						{
							if (line[i] == '^' && i + 1 < line.size() && line[i + 1] >= '0' && line[i + 1] <= '9')
							{
								color = line.substr(i, 2); part += color; ++i; continue;
							}
							part += line[i];
							if (++count >= columns)
							{
								wrapped_output.push_back(part);
								part = color;
								count = 0;
							}
						}
						if (count || line.empty()) wrapped_output.push_back(part);
					}
					wrapped_generation = output_generation;
					wrapped_columns = columns;
				}
			}
			visible_lines = std::clamp(static_cast<int>((frame.height - 90) / line_height), 1, 120);
			const auto total = static_cast<int>(wrapped_output.size());
			scroll = std::clamp(scroll, 0, std::max(0, total - visible_lines));
			const int end = total - scroll, first = std::max(0, end - visible_lines);
			draw_box(frame, 6, 38, frame.width - 12, frame.height - 44, output_color);
			const float height = frame.height - 84;
			draw_box(frame, frame.width - 24, 44, 10, height, bar_color);
			const float slider = height * std::min(1.0f, static_cast<float>(visible_lines) / std::max(total, 1));
			const float top = total <= visible_lines ? 0 : (height - slider) * first / (total - visible_lines);
			draw_box(frame, frame.width - 24, 44 + top, 10, slider, white);
			float y = 44 + line_height * (visible_lines - (end - first));
			for (int i = first; i < end; ++i)
			{
				y += line_height;
				engine::console::draw_text(frame, wrapped_output[i].c_str(), 12, y, white);
			}
			engine::console::draw_text(frame, "IW8x", 12, frame.height - 12, title);
		}

		void draw_input(const engine::console::frame& frame)
		{
			draw_box(frame, 6, 6, frame.width - 12, 30, input_color);
			constexpr const char* prompt = "IW8x:>";
			engine::console::draw_text(frame, prompt, 12, 30, title);
			const auto x = 18 + engine::console::text_width(frame, prompt);
			const float width = frame.width - x - 16;
			int first{};
			while (first < cursor && engine::console::text_width(frame, std::string(buffer + first, cursor - first).c_str()) > width)
				++first;
			std::string text = buffer + first;
			while (!text.empty() && engine::console::text_width(frame, text.c_str()) > width) text.pop_back();
			engine::console::draw_text(frame, text.c_str(), x, 30, white);
			const auto cursor_x = x + engine::console::text_width(frame, std::string(buffer + first, cursor - first).c_str());
			if ((GetTickCount64() / 500) % 2 == 0) engine::console::draw_text(frame, "|", cursor_x, 30, white);
			if (!buffer[0]) return;
			update_suggestions();
			if (cursor == std::strlen(buffer) && suggestions.selected < 0)
			{
				const auto completion = autocomplete::ghost_text(suggestions);
				if (cursor_x + engine::console::text_width(frame, completion.c_str()) < frame.width - 12)
					engine::console::draw_text(frame, completion.c_str(), cursor_x, 30, ghost);
			}
			float y = 39;
			if (details.exists)
			{
				const auto count = std::min<int>(static_cast<int>(domain_lines.size()), std::max(0, int((frame.height - y - 72) / line_height)));
				const float height = 12 + (2 + count) * line_height;
				draw_box(frame, x - 6, y, frame.width - x, height, hint_color);
				engine::console::draw_text(frame, details_name.c_str(), x, y + 24, dvar_color);
				engine::console::draw_text(frame, details.current.c_str(), x + (frame.width - x) / 4, y + 24, dvar_color);
				engine::console::draw_text(frame, "  default", x, y + 42, ghost);
				engine::console::draw_text(frame, details.reset.c_str(), x + (frame.width - x) / 4, y + 42, ghost);
				for (int i = 0; i < count; ++i) engine::console::draw_text(frame, domain_lines[i].c_str(), x, y + 60 + i * line_height, command_color);
				y += height + 3;
			}
			const auto& matches = suggestions.matches;
			if (matches.empty() || (matches.size() == 1 && matches[0].type == autocomplete::match_type::dvar)) return;
			const auto count = std::min<int>({ 24, static_cast<int>(matches.size()), std::max(0, int((frame.height - y - 36) / line_height)) });
			if (count == 0) return;
			const int offset = std::max(0, suggestions.selected - count + 1);
			draw_box(frame, x - 6, y, frame.width - x, count * line_height + 30, hint_color);
			for (int i = 0; i < count; ++i)
			{
				const auto& match = matches[offset + i];
				const bool selected = suggestions.selected == offset + i;
				if (selected) engine::console::draw_rect(frame, x - 2, y + 7 + i * line_height, frame.width - x - 8, line_height, highlight);
				const auto* color = selected ? selected_color : match.type == autocomplete::match_type::dvar ? dvar_color : command_color;
				engine::console::draw_text(frame, match.name.c_str(), x, y + 24 + i * line_height, color);
			}
			if (matches.size() > count) engine::console::draw_text(frame, "... more matches (Tab / Shift+Tab to cycle)", x, y + 24 + count * line_height, ghost);
		}
	}

	void draw()
	{
		if (!ready.load()) return;
		clientconsole::pump_commands();
		synchronize();
		if (!open.load()) return;
		const auto frame = engine::console::get_frame();
		if (!frame.font || !frame.white || frame.scale <= 0 || frame.width < 320 || frame.height < 100) return;
		if (output_visible) draw_output(frame);
		draw_input(frame);
	}

	bool key_event(int key, bool down, int virtual_key, bool shift)
	{
		if (!ready.load()) return false;
		synchronize();
		if (key == '`' || virtual_key == VK_OEM_3 || key == 0xA0 || virtual_key == VK_F7)
		{
			if (down)
			{
				if (shift)
				{
					if (!open.load()) toggle();
					output_visible = !output_visible;
					scroll = 0;
				}
				else toggle();
			}
			return true;
		}
		if (!open.load()) return false;
		if (!down) return false;
		if (key == 27 || virtual_key == VK_ESCAPE) toggle();
		else if (key == 13 || virtual_key == VK_RETURN) execute();
		else if (key == 9 || virtual_key == VK_TAB)
		{
			update_suggestions();
			const auto completed = autocomplete::complete(suggestions, shift);
			strncpy_s(buffer, completed.c_str(), _TRUNCATE);
			cursor = static_cast<int>(std::strlen(buffer));
		}
		else if (key == 132 || key == 133 || virtual_key == VK_UP || virtual_key == VK_DOWN)
		{
			const auto& history = autocomplete::history();
			if (key == 132 || virtual_key == VK_UP) history_index = std::min(history_index + 1, static_cast<int>(history.size()) - 1);
			else history_index = std::max(-1, history_index - 1);
			set_buffer(history_index < 0 ? std::string{} : history[history_index]);
		}
		else if (key == 134 || virtual_key == VK_LEFT) cursor = std::max(0, cursor - 1);
		else if (key == 135 || virtual_key == VK_RIGHT) cursor = std::min(static_cast<int>(std::strlen(buffer)), cursor + 1);
		else if (key == 152 || virtual_key == VK_HOME) cursor = 0;
		else if (key == 153 || virtual_key == VK_END) cursor = static_cast<int>(std::strlen(buffer));
		else if (key == 149 || virtual_key == VK_DELETE)
		{
			const auto length = std::strlen(buffer);
			if (cursor < length) std::memmove(buffer + cursor, buffer + cursor + 1, length - cursor);
			suggestions_valid = false;
		}
		else if (key == 193 || key == 151 || virtual_key == VK_PRIOR) scroll += key == 193 ? 1 : visible_lines;
		else if (key == 192 || key == 150 || virtual_key == VK_NEXT) scroll = std::max(0, scroll - (key == 192 ? 1 : visible_lines));
		return true;
	}

	void print(std::string_view text)
	{
		if (!ready.load() || text.empty()) return;
		std::lock_guard lock(output_mutex);
		while (!text.empty())
		{
			const auto end = text.find('\n');
			auto line = text.substr(0, std::min(end, size_t(4096)));
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
			if (output.empty() || output.back() != line)
			{
				output.emplace_back(line);
				++output_generation;
			}
			if (output.size() > 512) output.pop_front();
			if (end == text.npos) break;
			text.remove_prefix(end + 1);
		}
	}
	void print_engine(const char* text, int flags)
	{
		// Closed consoles keep errors/warnings without adding a per-frame trace scan.
		if (!ready.load() || (!open.load() && flags < 2)) return;
		char line[2048]{};
		if (safemem::ReadString(text, line, sizeof(line))) print(line);
	}

	hook::Status install(uintptr_t base, submit_command handler)
	{
		if (ready.load()) return hook::Status::Installed;
		if (!handler || !engine::console::initialize(base)) return hook::Status::Failed;
		image_base = base; submit = handler;
		constexpr unsigned char char_bytes[]{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x63, 0xF9 };
		constexpr unsigned char draw_bytes[]{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, 0xE0, 0xFD, 0x97, 0x0D };
		auto status = hook::Install(reinterpret_cast<void*>(base + 0x15BD370), &char_event, char_bytes, sizeof(char_bytes), original_char);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(base + 0x15AE0B0), &draw_native_console, draw_bytes, sizeof(draw_bytes), original_draw);
		if (status == hook::Status::Installed)
		{
			ready.store(true);
			::console::SetOutputHandler(&print);
		}
		return status;
	}
	void shutdown()
	{
		ready.store(false);
		open.store(false);
		::console::SetOutputHandler(nullptr);
	}
}
