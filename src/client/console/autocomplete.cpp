// IW7-Mod console completion/history adapted to Replay's native name iterators.
#include "client/console/autocomplete.hpp"
#include "engine/console.hpp"
#include "engine/safemem.h"
#include "common/commands/command.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace game_console::autocomplete
{
	namespace
	{
		struct name_entry
		{
			std::string folded;
			match value;
		};
		std::vector<name_entry> catalog;
		std::deque<std::string> commands;
		match_type collecting{};
		std::string lower(std::string value)
		{
			for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return value;
		}
		void collect(const char* name)
		{
			char text[192]{};
			if (catalog.size() < 32768 && safemem::ReadString(name, text, sizeof(text))) catalog.push_back({ lower(text), { text, collecting } });
		}
		std::string quote(const std::string& text)
		{
			return text.find_first_of(" \t;") == text.npos ? text : '"' + text + '"';
		}
	}

	void refresh()
	{
		catalog.clear();
		collecting = match_type::dvar;
		engine::console::for_each_dvar(&collect);
		collecting = match_type::command;
		engine::console::for_each_command(&collect);
		for (const auto& name : command::names()) collect(name.c_str());
		// These frontend adapters intentionally do not register a retail Cmd node.
		for (const auto* name : { "clear", "loglevel", "join_status", "connectsession", "connectsessioninvite", "playlist",
			"online_status", "online_start", "online_stop", "mm_start", "mm_stop", "addbot", "addbots", "mw_noclip" }) collect(name);
		std::sort(catalog.begin(), catalog.end(), [](const auto& a, const auto& b) { return a.folded < b.folded; });
		catalog.erase(std::unique(catalog.begin(), catalog.end(), [](const auto& a, const auto& b)
		{
			return a.folded == b.folded;
		}), catalog.end());
	}

	result query(const std::string& input)
	{
		result result;
		// Keep semicolon-separated commands and quoted arguments intact.
		size_t segment{}, start{};
		bool quoted{};
		for (size_t i = 0; i < input.size(); ++i)
		{
			if (input[i] == '"') quoted = !quoted;
			else if (input[i] == ';' && !quoted) segment = i + 1;
		}
		std::vector<std::string> words;
		quoted = false;
		start = segment;
		for (size_t i = segment; i <= input.size(); ++i)
		{
			if (i < input.size() && input[i] == '"') quoted = !quoted;
			if (i == input.size() || (!quoted && (input[i] == ' ' || input[i] == '\t')))
			{
				if (i > start) words.emplace_back(input.substr(start, i - start));
				if (i < input.size()) start = i + 1;
			}
		}
		result.head = input.substr(0, start);
		result.token = input.substr(start);
		auto prefix = lower(result.token);
		if (!prefix.empty() && (prefix.front() == '/' || prefix.front() == '\\')) prefix.erase(0, 1);
		const auto argument = words.size() - ((!result.token.empty() && !words.empty()) ? 1 : 0);
		if (!words.empty()) result.command = lower(words.front());
		if (argument == 0)
		{
			for (const auto& entry : catalog)
				if (entry.folded.starts_with(prefix)) result.matches.push_back(entry.value);
		}
		else
		{
			const bool set = result.command == "set" || result.command == "seta" || result.command == "sets" ||
				result.command == "setu" || result.command == "toggle" || result.command == "reset";
			if (set && argument == 1)
			{
				for (const auto& entry : catalog)
					if (entry.value.type == match_type::dvar && entry.folded.starts_with(prefix)) result.matches.push_back(entry.value);
			}
			else if ((!set && argument == 1) || (set && argument == 2 && words.size() >= 2))
			{
				result.command = set ? words[1] : words[0];
				const auto info = engine::console::describe_dvar(result.command.c_str());
				for (const auto& value : info.values)
					if (lower(quote(value)).starts_with(prefix)) result.matches.push_back({ value, match_type::argument });
			}
		}
		return result;
	}

	std::string complete(result& result, bool reverse)
	{
		if (result.matches.empty()) return result.head + result.token;
		const auto count = static_cast<int>(result.matches.size());
		result.selected = result.selected < 0 ? (reverse ? count - 1 : 0) : (result.selected + (reverse ? count - 1 : 1)) % count;
		return result.head + quote(result.matches[result.selected].name);
	}
	std::string ghost_text(const result& result)
	{
		if (result.matches.empty()) return {};
		const auto index = result.selected < 0 ? 0 : result.selected;
		const auto name = quote(result.matches[index].name);
		return lower(name).starts_with(lower(result.token)) ? name.substr(result.token.size()) : std::string{};
	}
	std::string prepare_command(std::string input)
	{
		const auto first = input.find_first_not_of(" \t");
		if (first == input.npos) return {};
		input.erase(0, first);
		if (input.front() == '/' || input.front() == '\\') input.erase(0, 1);
		return input;
	}
	void add_history(const std::string& input)
	{
		if (commands.empty() || commands.front() != input) commands.push_front(input);
		if (commands.size() > 64) commands.pop_back();
	}
	const std::deque<std::string>& history()
	{
		return commands;
	}
	void clear_history()
	{
		commands.clear();
	}
}
