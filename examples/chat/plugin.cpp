#include "plugin/sdk/gsc.hpp"

#include <charconv>
#include <string>
#include <string_view>

namespace
{

	void log(const iw8x::api& api, std::string_view message)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view(message));
	}

	std::string join_args(const iw8x_string* args, uint32_t count, uint32_t first)
	{
		std::string out;

		for (uint32_t i = first; i < count; ++i)
		{
			if (i != first) out.push_back(' ');

			out.append(args[i].data, args[i].size);
		}

		return out;
	}

	bool parse_client_num(iw8x_string arg, uint32_t& value)
	{
		if (!arg.data || !arg.size) return false;

		const auto* first = arg.data;
		const auto* last = arg.data + arg.size;
		const auto result = std::from_chars(first, last, value);

		return result.ec == std::errc{} && result.ptr == last;
	}

	std::string rainbow_text(std::string_view text)
	{
		std::string out;
		out.reserve(text.size() * 4);

		unsigned color = 0;

		for (const char ch : text)
		{
			if (ch == ' ')
			{
				// Extra gap between words.
				out += " ";
				continue;
			}

			out += '^';
			out += static_cast<char>('0' + color);
			out += ' ';
			out += ch;
			out += ' ';

			color = (color + 1) % 8;
		}

		// Remove the final separator space.
		if (!out.empty() && out.back() == ' ') out.pop_back();

		return out;
	}

} // namespace

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		iw8x::api api(host, owner);

		constexpr uint64_t needed = IW8X_CAP_CHAT | IW8X_CAP_PLAYERS | IW8X_CAP_COMMANDS | IW8X_CAP_GAME_TASKS;

		if ((host->capabilities() & needed) != needed)
		{
			log(api, "Chat example requires CHAT, PLAYERS, COMMANDS and GAME_TASKS capabilities");
			return 0;
		}

		api.command("iw8x_say",
			[api](const iw8x_string* args, uint32_t count)
			{
				if (count < 2)
				{
					log(api, "usage: iw8x_say <message>");
					return;
				}

				auto message = rainbow_text(join_args(args, count, 1));

				api.game_thread(
					[api, message = std::move(message)]
					{
						try
						{
							api.broadcast(message);
						}
						catch (const std::exception& error)
						{
							log(api, std::string("iw8x_say failed: ") + error.what());
						}
					});
			});

		api.command("iw8x_tell",
			[api](const iw8x_string* args, uint32_t count)
			{
				uint32_t client_num{};

				if (count < 3 || !parse_client_num(args[1], client_num))
				{
					log(api, "usage: iw8x_tell <clientnum> <message>");
					return;
				}

				iw8x_player target{};

				for (const auto& player : api.players())
				{
					if (player.client_num() == client_num)
					{
						target = player.ref();
						break;
					}
				}

				if (!target.generation)
				{
					log(api, "iw8x_tell: client is not connected");
					return;
				}

				auto message = rainbow_text(join_args(args, count, 2));

				api.game_thread(
					[api, target, message = std::move(message)]
					{
						try
						{
							api.tell(api.player_from(target), message);
						}
						catch (const std::exception& error)
						{
							log(api, std::string("iw8x_tell failed: ") + error.what());
						}
					});
			});

		api.command("iw8x_welcome",
			[api](const iw8x_string*, uint32_t)
			{
				auto message = rainbow_text("welcome player");

				api.game_thread(
					[api, message = std::move(message)]
					{
						try
						{
							api.broadcast(message);
						}
						catch (const std::exception& error)
						{
							log(api, std::string("iw8x_welcome failed: ") + error.what());
						}
					});
			});

		log(api, "Stock text-chat example loaded; commands: iw8x_say, iw8x_tell, iw8x_welcome");

		return 1;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void IW8X_PluginUnload() noexcept
{
}
