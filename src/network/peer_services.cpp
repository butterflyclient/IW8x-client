#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>
#include <bcrypt.h>
#include "network/peer_services.h"
#include "network/online_services.h"
#include "network/native_network.h"

#include "common/console.h"

#include <mutex>
#include <array>
namespace peers
{
	namespace
	{
		std::mutex lock;
		bool dedicated = false;
		SOCKET listener = INVALID_SOCKET;
		uint16_t listenPort = 27127;
		sockaddr_in directory{};
		wire::Directory entries;
		wire::Packet published{};
		uint64_t publishedAt = 0, registerAt = 0, retryAt = 0;
		Request pending;
		Endpoint lookupSource{};

		Endpoint Address(const sockaddr_in& address)
		{
			return { address.sin_addr.s_addr, ntohs(address.sin_port) };
		}

		sockaddr_in Address(Endpoint endpoint)
		{
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = endpoint.ip;
			address.sin_port = htons(endpoint.port);
			return address;
		}

		std::string Label(Endpoint endpoint)
		{
			const auto address = Address(endpoint);
			char ip[INET_ADDRSTRLEN]{};
			inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
			return std::string(ip) + ":" + std::to_string(endpoint.port);
		}

		uint64_t Nonce()
		{
			uint64_t value = 0;
			if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
			return value;
		}

		bool ParseAddress(std::string_view text, Endpoint& result, uint16_t defaultPort)
		{
			const auto colon = text.find(':');
			unsigned port = defaultPort;
			if (colon != std::string_view::npos)
			{
				const auto number = text.substr(colon + 1);
				const auto parse = std::from_chars(number.data(), number.data() + number.size(), port);
				if (number.empty() || parse.ec != std::errc{} || parse.ptr != number.data() + number.size() || !port || port > 65535)
					return false;
				text = text.substr(0, colon);
			}
			in_addr ip{};
			if (!native_network::ResolveIPv4(text, ip)) return false;
			result = { ip.s_addr, uint16_t(port) };
			return true;
		}

		void Send(SOCKET socket, const wire::Packet& packet, Endpoint target)
		{
			const auto bytes = wire::Encode(packet);
			const auto address = Address(target);
			sendto(socket, bytes.data(), int(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		}

		void Broadcast(SOCKET socket, const wire::Packet& query)
		{
			// One directed broadcast per active IPv4 interface, only for an explicit
			// XUID lookup. Radmin peers may also use a common directory if broadcast is
			// unavailable. This is not a scan of individual addresses.
			ULONG size = 16384;
			std::vector<unsigned char> data(size);
			auto result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
				reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()), &size);
			if (result == ERROR_BUFFER_OVERFLOW && size <= 1024 * 1024)
			{
				data.resize(size);
				result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
					reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()), &size);
			}
			if (result != NO_ERROR) return;
			for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()); adapter; adapter = adapter->Next)
			{
				if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
				for (auto* item = adapter->FirstUnicastAddress; item; item = item->Next)
				{
					if (item->Address.lpSockaddr->sa_family != AF_INET || !item->OnLinkPrefixLength || item->OnLinkPrefixLength > 30)
						continue;
					auto* addr = reinterpret_cast<sockaddr_in*>(item->Address.lpSockaddr);
					const uint32_t mask = 0xffffffffu << (32 - item->OnLinkPrefixLength);
					const uint32_t ip = htonl(ntohl(addr->sin_addr.s_addr) | ~mask);
					Send(socket, query, { ip, 27127 });
					Send(socket, query, { ip, 27126 });
				}
			}
		}

		bool ReceiveLocked(SOCKET socket, std::string_view bytes, const sockaddr_in& from)
		{
			if (bytes.size() < 4 || bytes.substr(0, 4) != "RPLY") return false;
			const auto packet = wire::Parse(bytes);
			if (!packet.valid || from.sin_family != AF_INET) return true;
			const auto now = GetTickCount64();
			const auto source = Address(from);
			if (packet.kind == wire::Kind::Query)
			{
				if (!published.xuid || (packet.xuid && packet.xuid != published.xuid)) return true;
				auto reply = published;
				reply.kind = wire::Kind::Reply;
				reply.nonce = packet.nonce;
				if (now - publishedAt > 3000) reply.state = wire::State::Unavailable;
				const auto target = Address(directory);
				reply.ip = (ntohl(target.ip) >> 24) == 127 ? 0 : target.ip;
				reply.port = target.port;
				Send(socket, reply, source);
				return true;
			}
			if (dedicated)
			{
				if (packet.kind == wire::Kind::Register)
					entries.Register({ packet.xuid, now, source.ip, source.port });
				else if (packet.kind == wire::Kind::Lookup)
				{
					if (const auto* entry = entries.Find(packet.xuid, now))
					{
						wire::Packet reply;
						reply.kind = wire::Kind::Endpoint;
						reply.nonce = packet.nonce;
						reply.xuid = packet.xuid;
						reply.ip = entry->ip;
						reply.port = entry->port;
						Send(socket, reply, source);
					}
				}
				return true; // A server never accepts a remotely supplied join.
			}
			if (packet.kind == wire::Kind::Endpoint && pending.nonce && !pending.Expired(now) && packet.nonce == pending.nonce &&
				packet.xuid == pending.xuid && source == lookupSource)
			{
				// A host's local client registers through loopback. A remote requester
				// must reach that client through the directory's Radmin/LAN address.
				Endpoint endpoint{ (ntohl(packet.ip) >> 24) == 127 ? source.ip : packet.ip, packet.port };
				if (std::find(pending.targets.begin(), pending.targets.end(), endpoint) == pending.targets.end() &&
					pending.targets.size() < 8)
					pending.targets.push_back(endpoint);
				retryAt = 0;
				return true;
			}
			if (pending.Accept(packet, source, published.xuid, now))
			{
				entries.Register({ packet.xuid, now, source.ip, source.port });
				LOG_INFO("Peers", "session reply xuid=%llu kind=%u from=%s", packet.xuid, unsigned(packet.state), Label(source).c_str());
			}
			return true;
		}
	}

	void Initialize(bool isDedicated, const sockaddr_in& host)
	{
		std::lock_guard guard(lock);
		dedicated = isDedicated;
		directory = host;
		entries = {};
		pending = {};
		published = {};
		publishedAt = registerAt = retryAt = 0;
		if (dedicated) return;
		wchar_t path[MAX_PATH]{};
		if (native_network::ConfigurationPath(path, MAX_PATH))
		{
			const auto configured = GetPrivateProfileIntW(L"network", L"peer_port", 27127, path);
			if (!configured || configured > 65535)
			{
				OP_ERR("Invalid network peer_port");
				return;
			}
			listenPort = uint16_t(configured);
		}
		listener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (listener == INVALID_SOCKET)
		{
			OP_ERR("Peer listener socket failed: WSA=%d", WSAGetLastError());
			return;
		}
		const BOOL yes = TRUE;
		u_long nonblocking = 1;
		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_port = htons(listenPort);
		if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes)) == SOCKET_ERROR ||
			bind(listener, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR ||
			ioctlsocket(listener, FIONBIO, &nonblocking) == SOCKET_ERROR)
		{
			OP_ERR("Peer listener cannot bind UDP %u: WSA=%d; connect unavailable", unsigned(listenPort), WSAGetLastError());
			closesocket(listener);
			listener = INVALID_SOCKET;
			return;
		}
		setsockopt(listener, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
		OP_INFO("Native party session listener ready | UDP %u", unsigned(listenPort));
	}

	void Shutdown()
	{
		std::lock_guard guard(lock);
		if (listener != INVALID_SOCKET) closesocket(listener);
		listener = INVALID_SOCKET;
		pending = {};
		published = {};
	}

	void SetDirectory(const sockaddr_in& host)
	{
		std::lock_guard guard(lock);
		directory = host;
		registerAt = 0;
	}

	void Publish(uint64_t xuid, wire::State state, int playlist, const wire::Session& session)
	{
		std::lock_guard guard(lock);
		published.xuid = xuid;
		published.state = state;
		published.playlist = uint8_t(std::clamp(playlist, 0, 128));
		published.session = session;
		publishedAt = GetTickCount64();
	}

	bool Receive(SOCKET socket, std::string_view bytes, const sockaddr_in& from)
	{
		std::lock_guard guard(lock);
		return ReceiveLocked(socket, bytes, from);
	}

	void Tick(SOCKET)
	{
		std::lock_guard guard(lock);
		if (dedicated || listener == INVALID_SOCKET) return;
		const auto now = GetTickCount64();
		// Bounded receive work; no engine calls or shared game pointers on this thread.
		for (unsigned i = 0; i < 32; ++i)
		{
			char bytes[512];
			sockaddr_in from{};
			int size = sizeof(from);
			const int count = recvfrom(listener, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&from), &size);
			if (count == SOCKET_ERROR) break;
			ReceiveLocked(listener, std::string_view(bytes, count), from);
		}
		if (published.xuid && now >= registerAt)
		{
			wire::Packet registration;
			registration.kind = wire::Kind::Register;
			registration.nonce = Nonce();
			registration.xuid = published.xuid;
			if (registration.nonce) Send(listener, registration, Address(directory));
			registerAt = now + 3000;
		}
		if (pending.Expired(now))
		{
			OP_WARN("Connect timed out: no joinable native session. Check peer_status, server address and UDP forwarding.");
			pending = {};
		}
		if (pending.nonce && !pending.Ready(now) && now >= retryAt)
		{
			wire::Packet query;
			query.kind = wire::Kind::Query;
			query.nonce = pending.nonce;
			query.xuid = pending.xuid;
			for (const auto target : pending.targets)
				Send(listener, query, target);
			if (pending.discovery)
			{
				Broadcast(listener, query);
				query.kind = wire::Kind::Lookup;
				Send(listener, query, lookupSource);
			}
			retryAt = now + 1000;
		}
	}

	bool TakeJoin(Join& result)
	{
		std::lock_guard guard(lock);
		if (!pending.Ready(GetTickCount64())) return false;
		result.packet = pending.candidate;
		result.endpoint = Address(pending.candidateFrom);
		pending = {};
		return true;
	}

	void Cancel()
	{
		std::lock_guard guard(lock);
		pending = {};
	}

	bool HandleCommand(std::string_view command)
	{
		if (command == "peer_status")
		{
			std::lock_guard guard(lock);
			OP_INFO("Peer | XUID=%llu state=%u listener=%s directory=%s connecting=%d", published.xuid, unsigned(published.state),
				dedicated ? "dedicated"
					: listener == INVALID_SOCKET ? "unavailable"
					: std::to_string(listenPort).c_str(),
				Label(Address(directory)).c_str(), pending.nonce != 0);
			return true;
		}
		if (command == "connect_cancel")
		{
			Cancel();
			OP_INFO("Pending connect cancelled");
			return true;
		}
		if (command == "mm_host" || command.starts_with("mm_host "))
		{
			Endpoint target;
			if (command.size() <= 8 || !ParseAddress(command.substr(8), target, 27126))
				OP_WARN("Usage: mm_host <hostname-or-IPv4[:port]>");
			else
				online_services::SetMatchHost(Address(target));
			return true;
		}
		if (command != "connect" && !command.starts_with("connect ")) return false;
		if (dedicated)
		{
			OP_WARN("connect is a client command");
			return true;
		}
		const auto text = command.size() > 8 ? command.substr(8) : std::string_view{};
		Endpoint target;
		uint64_t xuid = 0;
		const bool byXuid = ParseXuid(text, xuid);
		const bool byAddress = !byXuid && ParseAddress(text, target, 27127);
		if (!byAddress && !byXuid)
		{
			OP_WARN("Usage: connect <hostname-or-IPv4[:port] or decimal/0x XUID>");
			return true;
		}
		// Cancel only after valid syntax, and never while holding the peer lock.
		if (!online_services::CanConnectPeer()) return true;
		online_services::CancelMatchmaking();
		std::lock_guard guard(lock);
		if (listener == INVALID_SOCKET)
		{
			OP_ERR("Peer listener is unavailable; use online_start or check UDP 27127");
			return true;
		}
		if (xuid && xuid == published.xuid)
		{
			OP_WARN("Cannot connect to your own XUID");
			return true;
		}
		pending = {};
		pending.nonce = Nonce();
		pending.xuid = xuid;
		pending.started = GetTickCount64();
		retryAt = 0;
		if (!pending.nonce)
		{
			OP_ERR("Could not create a connect request nonce");
			return true;
		}
		if (byAddress)
		{
			pending.targets.push_back(target);
			pending.preferParty = text.find(':') == std::string_view::npos;
			if (pending.preferParty) pending.targets.push_back({ target.ip, 27126 });
		}
		else
		{
			pending.discovery = true;
			lookupSource = Address(directory);
			if (const auto* cached = entries.Find(xuid, pending.started)) pending.targets.push_back({ cached->ip, cached->port });
		}
		OP_INFO("Retrieving native session for %.*s", int(text.size()), text.data());
		return true;
	}
}
