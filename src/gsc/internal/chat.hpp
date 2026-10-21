#pragma once
#include "plugin/sdk/abi.h"

#include <string_view>
namespace iw8x::gsc::internal
{
	bool chat_available() noexcept;
	iw8x_status broadcast_chat(std::string_view message);
	iw8x_status tell_chat(iw8x_player player, std::string_view message);
}
