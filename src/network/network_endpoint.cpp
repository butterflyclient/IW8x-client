#include <winsock2.h>
#include <ws2tcpip.h>

#include "network/native_network.h"

#include <string>
namespace native_network
{
	bool ResolveIPv4(std::string_view host, in_addr& address)
	{
		if (host.empty() || host.size() > 253 || host.find_first_of("\r\n\t /\\:") != std::string_view::npos ||
			host.find('\0') != std::string_view::npos)
			return false;
		const std::string text(host);
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		addrinfo* list = nullptr;
		if (getaddrinfo(text.c_str(), nullptr, &hints, &list)) return false;
		bool valid = false;
		for (const auto* item = list; item; item = item->ai_next)
		{
			if (item->ai_family != AF_INET || item->ai_addrlen < sizeof(sockaddr_in)) continue;
			const auto ip = reinterpret_cast<const sockaddr_in*>(item->ai_addr)->sin_addr;
			const auto value = ntohl(ip.s_addr);
			if (!value || value == INADDR_BROADCAST || IN_MULTICAST(value)) continue;
			address = ip;
			valid = true;
			break;
		}
		freeaddrinfo(list);
		return valid;
	}
}
