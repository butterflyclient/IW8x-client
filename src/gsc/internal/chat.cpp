#include "chat.hpp"
#include "players.hpp"

#include "engine/image.hpp"
#include "engine/addresses.hpp"

#include <array>
#include <string>
namespace iw8x::gsc::internal
{
	namespace
	{
		constexpr size_t kMaxChatMessage = 120;
		namespace native = game::addresses::gsc_api;
		constexpr int kReliableServerCommand = native::reliable_server_command;

		iw8x_status validate_message(std::string_view message) noexcept
		{
			if (message.empty()) return IW8X_INVALID_ARGUMENT;
			if (message.size() > kMaxChatMessage) return IW8X_LIMIT;
			for (const auto ch : message)
			{
				const auto byte = static_cast<unsigned char>(ch);
				if (byte < 0x20 || ch == '"') return IW8X_INVALID_ARGUMENT;
			}
			return IW8X_OK;
		}

		std::string stock_chat_command(std::string_view message, uint32_t sender_client)
		{
			// Retail G_CmdsMP_SayTo uses: %c "\x1f%s%c%c%s" %i. The client T case
			// passes argv(1) to CG_ServerCmdMP_Chat and argv(2) as its block-list sender.
			std::string command{ "T \"\x1fServer^7: " };
			command.append(message);
			command += "\" ";
			command += std::to_string(sender_client);
			return command;
		}

		using send_server_command_fn = void (*)(void*, int, const char*);

		iw8x_status send_to_client(uint32_t client_num, std::string_view message)
		{
			auto* client = client_at(client_num);
			if (!client) return IW8X_STALE;
			// Native SV_Game_BroadcastServerCommand only sends to states >= CS_ACTIVE (4).
			if (client_state(client) < 4) return IW8X_BUSY;

			// client_at() returns a live engine-owned SvClientMP pointer from the fixed
			// retail pointer table.  Avoid VirtualQuery for its vtable on every chat
			// recipient; that protection query was dominating broadcast latency.
			auto** vtable = *reinterpret_cast<void***>(client);
			if (!vtable || !vtable[native::reliable_command_vtable_slot]) return IW8X_INTERNAL_ERROR;

			const auto command = stock_chat_command(message, client_num);
			reinterpret_cast<send_server_command_fn>(vtable[native::reliable_command_vtable_slot])(
				client, kReliableServerCommand, command.c_str());
			return IW8X_OK;
		}
	}

	bool chat_available() noexcept
	{
		return game::initialized();
	}

	iw8x_status broadcast_chat(std::string_view message)
	{
		const auto valid = validate_message(message);
		if (valid != IW8X_OK) return valid;
		for (const auto player : connected_players())
		{
			if (validate_player(player) != IW8X_OK) continue;
			const auto status = send_to_client(player.client_num, message);
			if (status != IW8X_OK && status != IW8X_BUSY && status != IW8X_STALE) return status;
		}
		return IW8X_OK;
	}

	iw8x_status tell_chat(iw8x_player player, std::string_view message)
	{
		const auto valid = validate_message(message);
		if (valid != IW8X_OK) return valid;
		const auto player_status = validate_player(player);
		if (player_status != IW8X_OK) return player_status;
		return send_to_client(player.client_num, message);
	}
}
