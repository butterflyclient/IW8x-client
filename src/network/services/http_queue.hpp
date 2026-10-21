#pragma once
#include "network/services/types.hpp"
namespace online_services::detail
{
	// Both transports use the same queue, lease, allocation and native-session
	// snapshot. Merely polling /v1/server never reaches this mutation path.
	match_queue::Packet ProcessQueueLocked(const match_queue::Packet& query, const sockaddr_in& from, bool authenticated = false);

	matchmaking::Endpoint SessionEndpoint(const protocol::Session& session, size_t offset);

	match_http::Reply HttpRequest(std::string_view path, std::string_view body, uint32_t ip);

	void PollHttp();
}
