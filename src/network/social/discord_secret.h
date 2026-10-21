#pragma once
#include <string>
#include <string_view>
namespace discord120
{
	struct Secret
	{
		std::string scope, token;
	};

	inline bool Hex(std::string_view text, size_t size)
	{
		if (text.size() != size) return false;
		for (auto c : text)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
		return true;
	}

	inline bool Parse(std::string_view text, Secret& out)
	{
		constexpr std::string_view prefix = "magma|120|";
		if (text.size() != prefix.size() + 16 + 1 + 32 || !text.starts_with(prefix) || text[prefix.size() + 16] != '|') return false;
		const auto scope = text.substr(prefix.size(), 16), token = text.substr(prefix.size() + 17);
		if (!Hex(scope, 16) || !Hex(token, 32)) return false;
		out = { std::string(scope), std::string(token) };
		return true;
	}

	inline std::string Make(std::string_view scope, std::string_view token)
	{
		return Hex(scope, 16) && Hex(token, 32) ? "magma|120|" + std::string(scope) + "|" + std::string(token) : std::string{};
	}
}
