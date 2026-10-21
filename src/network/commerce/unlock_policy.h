#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
namespace unlock_policy
{
	inline std::string Name(std::string_view text)
	{
		const auto begin = text.find_first_not_of(" \t");
		if (begin == text.npos) return {};
		text = text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
		if (text.ends_with("#0")) text.remove_suffix(2);
		if (text.empty() || text.size() > 35) return {};
		std::string normalized;
		for (unsigned char c : text)
		{
			if (c < 32 || c == 127 || c == '#') return {};
			normalized += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		}
		return normalized;
	}

	struct Command
	{
		bool enabled;
		std::string name;
	};

	inline std::optional<Command> Parse(std::string_view text)
	{
		for (auto prefix : { std::string_view("unlock all "), std::string_view("relock all ") })
			if (text.starts_with(prefix)) return Command{ prefix.front() == 'u', Name(text.substr(prefix.size())) };
		return std::nullopt;
	}

	inline bool Grants(bool ready, bool authenticated, bool enabled, int item)
	{
		return ready && authenticated && enabled && item > 0 && item <= 100000000;
	}
}
