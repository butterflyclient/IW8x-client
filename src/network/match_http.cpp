#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>

#include "network/match_http.h"

#include <array>
#include <atomic>
#include <charconv>
#include <algorithm>
namespace match_http
{
	namespace
	{
		constexpr size_t kLimit = 16384;
		constexpr size_t kReplyLimit = 131072; // Three200-member rosters plus12 messages, including JSON escaping.
#ifndef IW8X_CLIENT_ONLY
		std::atomic<bool> stopping{ false };
		SOCKET listener = INVALID_SOCKET;
		HANDLE worker = nullptr;
		Handler serve;

#endif

		struct Internet
		{
			HINTERNET value = nullptr;

			~Internet()
			{
				if (value) WinHttpCloseHandle(value);
			}

			operator HINTERNET() const
			{
				return value;
			}
		};
#ifndef IW8X_CLIENT_ONLY

		std::string Lower(std::string_view value)
		{
			std::string s(value);
			for (auto& c : s)
				if (c >= 'A' && c <= 'Z') c += 32;
			return s;
		}
#endif
#ifndef IW8X_CLIENT_ONLY


		void Connection(SOCKET client, uint32_t peer)
		{
			DWORD timeout = 1500;
			setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
			setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
			const auto deadline = GetTickCount64() + 3000;
			std::string data;
			size_t headerEnd = std::string::npos, length = 0;
			std::string path;
			while (!stopping && GetTickCount64() < deadline && data.size() < 262144 + 4096)
			{
				char bytes[2048];
				const int n = recv(client, bytes, sizeof(bytes), 0);
				if (n <= 0) return;
				data.append(bytes, n);
				if (headerEnd == std::string::npos)
				{
					const auto end = data.find("\r\n\r\n");
					if (end == std::string::npos)
					{
						if (data.size() > 4096) return;
						continue;
					}
					if (end > 4096) return;
					const auto line = data.find("\r\n");
					if (line == std::string::npos || !data.starts_with("POST ")) return;
					const auto space = data.find(' ', 5);
					if (space == std::string::npos || data.substr(space, line - space) != " HTTP/1.1") return;
					path = data.substr(5, space - 5);
					bool found = false;
					for (size_t i = line + 2; i < end;)
					{
						const auto next = data.find("\r\n", i), colon = data.find(':', i);
						if (next == std::string::npos || colon == std::string::npos || colon >= next) return;
						auto key = Lower(std::string_view(data).substr(i, colon - i));
						auto value = std::string_view(data).substr(colon + 1, next - colon - 1);
						while (!value.empty() && value.front() == ' ')
							value.remove_prefix(1);
						if (key == "transfer-encoding" || key == "expect") return;
						if (key == "content-length")
						{
							if (found) return;
							found = true;
							auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
							if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
								length > (path == "/v1/host/poll" ? 262144 : kLimit))
								return;
						}
						i = next + 2;
					}
					if (!found) return;
					headerEnd = end + 4;
				}
				if (data.size() < headerEnd + length) continue;
				if (data.size() != headerEnd + length) return; // No pipelined requests or ambiguous framing.
				Reply reply;
				try
				{
					reply = serve(path, std::string_view(data).substr(headerEnd, length), peer);
				}
				catch (...)
				{
					reply = { 500, "{\"error\":\"internal error\"}" };
				}
				const size_t replyLimit = path == "/v1/playlists" ? kPlaylistReplyLimit
					: path == "/v1/frontend" ? 8 * 1024 * 1024
					: kReplyLimit;
				if (reply.body.size() > replyLimit) reply = { 500, "{\"error\":\"response too large\"}" };
				auto wire = "HTTP/1.1 " + std::to_string(reply.status) +
					" Response\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
					std::to_string(reply.body.size()) + "\r\n\r\n" + reply.body;
				for (size_t sent = 0; sent < wire.size() && !stopping && GetTickCount64() < deadline;)
				{
					const int n = send(client, wire.data() + sent, int(wire.size() - sent), 0);
					if (n <= 0) break;
					sent += n;
				}
				return;
			}
		}
#endif

	}
#ifndef IW8X_CLIENT_ONLY

	bool Start(uint16_t port, Handler handler)
	{
		if (worker || !port) return false;
		listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return false;
		BOOL exclusive = TRUE;
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive)) ||
			bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 16))
		{
			closesocket(listener);
			listener = INVALID_SOCKET;
			return false;
		}
		serve = std::move(handler);
		stopping = false;
		worker = CreateThread(
			nullptr, 0,
			[](void*) -> DWORD
			{
				while (!stopping)
				{
					fd_set ready;
					FD_ZERO(&ready);
					FD_SET(listener, &ready);
					timeval wait{ 0, 100000 };
					if (select(0, &ready, nullptr, nullptr, &wait) <= 0) continue;
					sockaddr_in peer{};
					int length = sizeof(peer);
					SOCKET client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &length);
					if (client == INVALID_SOCKET) continue;
					Connection(client, peer.sin_addr.s_addr);
					closesocket(client);
				}
				return 0;
			},
			nullptr, 0, nullptr);
		if (!worker)
		{
			closesocket(listener);
			listener = INVALID_SOCKET;
			serve = {};
			return false;
		}
		return true;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void Stop()
	{
		stopping = true;
		if (worker)
		{
			WaitForSingleObject(worker, INFINITE);
			CloseHandle(worker);
			worker = nullptr;
		}
		if (listener != INVALID_SOCKET) closesocket(listener);
		listener = INVALID_SOCKET;
		serve = {};
	}
#endif


	Reply Post(const std::wstring& origin, std::wstring_view path, std::string_view body)
	{
		if (origin.size() > 2048 || body.size() > (path == L"/v1/host/poll" ? 262144 : kLimit)) return { 0, "invalid HTTP request size" };
		URL_COMPONENTS url{};
		url.dwStructSize = sizeof(url);
		url.dwHostNameLength = url.dwUrlPathLength = url.dwUserNameLength = url.dwPasswordLength = url.dwExtraInfoLength = DWORD(-1);
		if (!WinHttpCrackUrl(origin.c_str(), 0, 0, &url) || (url.nScheme != INTERNET_SCHEME_HTTP && url.nScheme != INTERNET_SCHEME_HTTPS) ||
			url.dwUserNameLength || url.dwPasswordLength || url.dwExtraInfoLength || url.dwUrlPathLength > 1)
			return { 0, "invalid matchmaking HTTP origin" };
		Internet session{ WinHttpOpen(L"IW8ReplayMatchmaking/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0) };
		if (!session.value) return { 0, "WinHttpOpen failed" };
		if (!WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000)) return { 0, "WinHTTP timeout setup failed" };
		const std::wstring host(url.lpszHostName, url.dwHostNameLength), resource(path);
		Internet connection{ WinHttpConnect(session, host.c_str(), url.nPort, 0) };
		if (!connection.value) return { 0, "WinHttpConnect failed" };
		Internet request{ WinHttpOpenRequest(connection, L"POST", resource.c_str(), nullptr, nullptr, nullptr,
			url.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) };
		if (!request.value) return { 0, "WinHttpOpenRequest failed" };
		DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
		if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)))
			return { 0, "WinHTTP option setup failed" };
		const auto began = GetTickCount64();
		if (!WinHttpSendRequest(request, L"Content-Type: application/json\r\n", DWORD(-1), const_cast<char*>(body.data()),
				DWORD(body.size()), DWORD(body.size()), 0) ||
			!WinHttpReceiveResponse(request, nullptr))
			return { 0, "HTTP request failed Win32=" + std::to_string(GetLastError()) };
		DWORD status = 0, size = sizeof(status);
		if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr))
			return { 0, "missing HTTP status" };
		// Error replies carry authenticated conflict/recovery envelopes too. Keep
		// their bounded body; callers still decide which statuses they accept.
		Reply reply{ status, {} };
		for (;;)
		{
			if (GetTickCount64() - began > 6000) return { 0, "HTTP response deadline exceeded" };
			char bytes[2048];
			DWORD got = 0;
			if (!WinHttpReadData(request, bytes, sizeof(bytes), &got)) return { 0, "HTTP response read failed" };
			if (!got) break;
			const size_t replyLimit = path == L"/v1/playlists" ? kPlaylistReplyLimit
				: path == L"/v1/frontend" ? 8 * 1024 * 1024
				: kReplyLimit;
			if (reply.body.size() + got > replyLimit) return { 0, "HTTP response exceeds limit" };
			reply.body.append(bytes, got);
		}
		return reply;
	}
}
