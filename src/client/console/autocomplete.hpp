#pragma once

#include <deque>
#include <string>
#include <vector>

namespace game_console::autocomplete
{
	enum class match_type
	{
		command,
		dvar,
		argument
	};
	struct match
	{
		std::string name;
		match_type type{};
	};
	struct result
	{
		std::string head, token, command;
		std::vector<match> matches;
		int selected = -1;
	};
	void refresh();
	result query(const std::string& input);
	std::string complete(result& result, bool reverse);
	std::string ghost_text(const result& result);
	std::string prepare_command(std::string input);
	void add_history(const std::string& input);
	const std::deque<std::string>& history();
	void clear_history();
}
