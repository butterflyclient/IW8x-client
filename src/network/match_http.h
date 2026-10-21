#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
namespace match_http
{
	// Only the title-content route accepts a larger response. Other API limits stay fixed.
	inline constexpr size_t kPlaylistReplyLimit = 0xc0000;

	struct Reply
	{
		unsigned status = 500;
		std::string body;
	};

	// Called on the HTTP worker, never on a game thread. Peer is network-order IPv4.
	using Handler = std::function<Reply(std::string_view, std::string_view, uint32_t)>;
	bool Start(uint16_t port, Handler handler);
	// TU43 explicit interface binding (network-order IPv4).
	bool StartBound(uint16_t port, Handler handler, uint32_t address);
	void Stop();
	// Bounded WinHTTP operation; caller must be a background worker. TLS keeps normal
	// certificate validation. Redirects and ambient Windows credentials are disabled.
	Reply Post(const std::wstring& origin, std::wstring_view path, std::string_view body);
}
