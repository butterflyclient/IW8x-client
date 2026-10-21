#pragma once
#include "network/commerce/custom_mod_store.h"
#include "network/backend/customization_names.h"

#include <algorithm>
namespace text_validation
{
	using custom_mods::Json;

	inline Json Defaults()
	{
		return { { "version", 1 }, { "blocked_terms", Json::array() } };
	}

	inline bool Policy(const Json& policy)
	{
		try
		{
			if (policy.at("version") != 1 || !policy.at("blocked_terms").is_array() || policy.at("blocked_terms").size() > 512)
				return false;
			for (const auto& value : policy.at("blocked_terms"))
			{
				if (!value.is_string()) return false;
				const auto term = value.get<std::string>();
				if (term.empty() || term.size() > 48 || term.front() == ' ' || term.back() == ' ') return false;
				for (char c : term)
					if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ')) return false;
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	inline std::string Check(std::string_view text, const Json& policy)
	{
		// Validate UTF-8 and count codepoints, independently of the native keyboard.
		if (text.empty() || text.size() > customization_names::ModBytes) return "length";
		bool visible = false;
		for (size_t i = 0; i < text.size();)
		{
			const auto first = static_cast<unsigned char>(text[i++]);
			uint32_t code = first;
			int extra = 0;
			if (first >= 0xC2 && first <= 0xDF)
			{
				code = first & 31;
				extra = 1;
			}
			else if (first >= 0xE0 && first <= 0xEF)
			{
				code = first & 15;
				extra = 2;
			}
			else if (first >= 0xF0 && first <= 0xF4)
			{
				code = first & 7;
				extra = 3;
			}
			else if (first >= 128)
				return "characters";
			if (i + extra > text.size()) return "characters";
			for (int k = 0; k < extra; ++k)
			{
				const auto c = static_cast<unsigned char>(text[i++]);
				if ((c & 0xC0) != 0x80) return "characters";
				code = (code << 6) | (c & 63);
			}
			if ((extra == 1 && code < 128) || (extra == 2 && code < 2048) || (extra == 3 && code < 65536) || code > 0x10FFFF ||
				(code >= 0xD800 && code <= 0xDFFF) || code < 32 || code == 127 || code == 0x2028 || code == 0x2029 ||
				(code >= 0x202A && code <= 0x202E) || (code >= 0x2066 && code <= 0x2069))
				return "characters";
			if (code != 32) visible = true;
		}
		if (!visible) return "length";
		std::string words = " ";
		for (unsigned char c : text)
		{
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 128)
				words += char(c);
			else if (words.back() != ' ')
				words += ' ';
		}
		if (words.back() != ' ') words += ' ';
		for (const auto& value : policy.at("blocked_terms"))
			if (words.find(" " + value.get<std::string>() + " ") != std::string::npos) return "profanity";
		return "allowed";
	}
}
