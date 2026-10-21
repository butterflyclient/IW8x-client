#pragma once
#include "network/services/types.hpp"
namespace online_services::detail
{
	extern SOCKET g_socket;

	extern HANDLE g_thread;

	extern sockaddr_in g_server;

	bool FatalSocketError(int error);

	bool SendPacketLocked(const char* data, int size);

	void ParsePacket(std::string_view bytes);

	DWORD WINAPI Worker(void*);
}
