#include "gsc/internal/state.hpp"

#include <algorithm>
#include <iterator>
namespace iw8x::gsc::internal
{
	namespace
	{
		struct entry
		{
			std::string_view name;
			uint32_t token;
		};

		constexpr entry tokens[]{
#include "gsc/internal/stock_script_tokens.inc"
		};

		uint32_t find_token(std::string_view name)
		{
			const auto it = std::lower_bound(std::begin(tokens), std::end(tokens), name,
				[](const entry& item, std::string_view key)
				{
					return item.name < key;
				});
			return it != std::end(tokens) && it->name == name ? it->token : 0;
		}
	}

	uint32_t script_function_token(iw8x_string function)
	{
		return find_token(checked_name(function));
	}

	uint32_t script_module_token(std::string_view normalized_path)
	{
		return find_token(normalized_path);
	}

	std::string_view script_function_name(uint32_t token)
	{
		const auto it = std::find_if(std::begin(tokens), std::end(tokens),
			[token](const entry& item)
			{
				return item.token == token;
			});
		return it == std::end(tokens) ? std::string_view{ "<unknown>" } : it->name;
	}
}
