#pragma once
#include <winsock2.h>

#include "network/peer_protocol.h"
#include "network/peer_request.h"

#include <string_view>
namespace peers
{
	void Initialize(bool dedicated, const sockaddr_in& directory);
	void Shutdown(); // after the socket worker exits
	void SetDirectory(const sockaddr_in& directory);
	void Tick(SOCKET socket); // network worker only
	bool Receive(SOCKET socket, std::string_view bytes, const sockaddr_in& from);
	void Publish(uint64_t xuid, wire::State state, int playlist, const wire::Session& session); // game thread

	struct Join
	{
		wire::Packet packet;
		sockaddr_in endpoint{};
	};

	bool TakeJoin(Join& result); // game thread only
	bool HandleCommand(std::string_view command);
	void Cancel();
}
