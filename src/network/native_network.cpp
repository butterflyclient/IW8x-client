#include <winsock2.h>

#include "engine/addresses.hpp"

#include <ws2tcpip.h>

#include <iphlpapi.h>
#include "network/native_network.h"

#include "common/deployment_profile.h"

#include "dedicated/dedicated.h"
#include "network/social/party_transport.h"

#include "common/console.h"

#include <intrin.h>

#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
namespace native_network
{
	namespace
	{
		uintptr_t g_base = 0;
		bool g_dedicated = false, g_autoServer = true, g_valid = false;
		bool g_partyRelay = false;
		uint16_t g_port = 3074, g_publicPort = 0;
		in_addr g_publicAddress{};
		std::atomic<uint16_t> g_boundPort{ 0 };
		std::atomic<bool> g_publicReady{ false };
		std::once_flag g_configuration;
		std::mutex g_installMutex;
		bool g_installed = false;
		using ParamsCtor = void* (*)(void*);
		using CommonCtor = void* (*)(void*, const void*, const void*, int, int);
		std::atomic<ParamsCtor> g_params{ nullptr };
		std::atomic<CommonCtor> g_common{ nullptr };

		bool RadminAddress(in_addr& result)
		{
			// Enumerate once at DLL startup, never while handling a frame or request.
			// Do not accidentally advertise Ethernet/Wi-Fi when the VPN is absent.
			ULONG size = 16384;
			std::vector<unsigned char> data(size);
			constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
			auto status = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()), &size);
			if (status == ERROR_BUFFER_OVERFLOW && size <= 1024 * 1024)
			{
				data.resize(size);
				status = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()), &size);
			}
			if (status != NO_ERROR) return false;
			bool found = false;
			for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()); adapter; adapter = adapter->Next)
			{
				if (adapter->OperStatus != IfOperStatusUp) continue;
				const std::wstring description = adapter->Description ? adapter->Description : L"";
				const std::wstring name = adapter->FriendlyName ? adapter->FriendlyName : L"";
				if (description.find(L"Radmin") == std::wstring::npos && name.find(L"Radmin") == std::wstring::npos) continue;
				for (auto* item = adapter->FirstUnicastAddress; item; item = item->Next)
				{
					if (item->Address.lpSockaddr->sa_family != AF_INET || item->DadState != IpDadStatePreferred) continue;
					const auto address = reinterpret_cast<const sockaddr_in*>(item->Address.lpSockaddr)->sin_addr;
					if ((ntohl(address.s_addr) >> 24) != 26) continue;
					if (found && address.s_addr != result.s_addr) return false; // Require an explicit override if ambiguous.
					result = address;
					found = true;
				}
			}
			return found;
		}

		void ReadConfiguration()
		{
			wchar_t path[MAX_PATH]{};
			if (!ConfigurationPath(path, MAX_PATH)) return;
			g_dedicated = dedicated::Enabled();
			g_autoServer = false; // Dedicated startup is explicit, including legacy INIs.
			const auto port =
				GetPrivateProfileIntW(L"network", g_dedicated ? L"dedicated_port" : L"client_port", g_dedicated ? 3075 : 3074, path);
			if (!port || port > 65535)
			{
				LOG_ERR("Network", "invalid configured gameplay port");
				return;
			}
			g_port = uint16_t(port);
			// A client's public party endpoint can be configured separately. It is never
			// inherited from the dedicated server on the same computer.
			wchar_t hostW[256]{};
			GetPrivateProfileStringW(L"network", g_dedicated ? L"advertised_host" : L"party_advertised_host", L"", hostW, 256, path);
			if (hostW[0])
			{
				char host[768]{};
				const auto publicPort =
					GetPrivateProfileIntW(L"network", g_dedicated ? L"advertised_port" : L"party_advertised_port", g_port, path);
				WSADATA wsa{};
				if (!publicPort || publicPort > 65535 ||
					!WideCharToMultiByte(CP_UTF8, 0, hostW, -1, host, sizeof(host), nullptr, nullptr) || WSAStartup(MAKEWORD(2, 2), &wsa))
				{
					LOG_ERR("Network", "invalid public endpoint configuration");
					return;
				}
				const bool resolved = ResolveIPv4(host, g_publicAddress);
				WSACleanup();
				if (!resolved)
				{
					LOG_ERR("Network", "cannot resolve advertised host %s to IPv4", host);
					return;
				}
				g_publicPort = uint16_t(publicPort);
			}
			else if (deployment::kRadmin)
			{
				if (!RadminAddress(g_publicAddress))
				{
					LOG_ERR("Network",
						"no unique active Radmin IPv4 address; connect Radmin and restart, or set the advertised address in %ls",
						deployment::kConfigFile);
					return;
				}
				g_publicPort = g_port;
			}
			g_partyRelay =
				!g_dedicated && !deployment::kRadmin && !g_publicPort && GetPrivateProfileIntW(L"network", L"party_relay", 1, path) != 0;
			g_valid = true;
			LOG_INFO("Network", "deployment=%s config=%ls auto_server=%d", deployment::kName, deployment::kConfigFile, g_autoServer);
		}

		void* Parameters(void* params)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
			auto* result = g_params.load(std::memory_order_acquire)(params);
			// dwNetStart's bdNetStartParams, TU19 RVA14EF30A. Other callers of this
			// constructor retain their native defaults. +2 is a host-order UDP port.
			if (caller == 0x14ef30f)
			{
				memcpy(static_cast<uint8_t*>(params) + 2, &g_port, sizeof(g_port));
				g_boundPort = 0;
				g_publicReady = false;
				party_transport::NativePort(0);
				LOG_INFO("Network", "requesting native gameplay UDP %u (%s)", unsigned(g_port), g_dedicated ? "dedicated" : "client");
			}
			return result;
		}

		void* CommonAddress(void* object, const void* locals, const void* publicAddress, int nat, int platform)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
			const bool localOffline = caller == 0x217bbe2;
			// bdAddr is152 bytes, starting with sockaddr_storage. Pass the public
			// address through the native constructor BEFORE it computes its hash and
			// publishes the shared bdCommonAddr to NetInit/socket-router. Patching only
			// the108-byte reply would leave native address equality/hash inconsistent.
			alignas(16) std::array<uint8_t, 152> advertised{};
			if (localOffline && g_publicPort)
			{
				memcpy(advertised.data(), publicAddress, advertised.size());
				const sockaddr_in address{ AF_INET, htons(g_publicPort), g_publicAddress, {} };
				memcpy(advertised.data(), &address, sizeof(address));
				publicAddress = advertised.data();
			}
			else if (localOffline && g_partyRelay)
			{
				memcpy(advertised.data(), publicAddress, advertised.size());
				if (party_transport::AdvertisedAddress(advertised.data())) publicAddress = advertised.data();
			}
			auto* result = g_common.load(std::memory_order_acquire)(object, locals, publicAddress, nat, platform);
			if (localOffline)
			{
				uint32_t count = 0;
				memcpy(&count, static_cast<uint8_t*>(object) + 776, sizeof(count));
				sockaddr_in local{};
				if (count) memcpy(&local, static_cast<uint8_t*>(object) + 16, sizeof(local));
				const auto port = local.sin_family == AF_INET ? ntohs(local.sin_port) : 0;
				g_boundPort = port;
				party_transport::NativePort(uint16_t(port));
				// Native code retries a random nearby port on collision. Never publish a
				// WAN assignment to a port that the configured forwarding rule won't reach.
				g_publicReady = port != 0 && (!g_publicPort || port == g_port);
				if (g_publicPort)
				{
					char ip[INET_ADDRSTRLEN]{};
					inet_ntop(AF_INET, &g_publicAddress, ip, sizeof(ip));
					LOG_INFO("Network", "native gameplay bound UDP %u; advertised %s:%u; publication=%s", unsigned(port), ip,
						unsigned(g_publicPort), g_publicReady ? "ready" : "blocked");
					if (!g_publicReady)
						LOG_ERR("Network", "configured UDP %u unavailable; free that port and restart before WAN matchmaking",
							unsigned(g_port));
				}
				else
				{
					LOG_INFO("Network", "native gameplay bound UDP %u; public address unconfigured", unsigned(port));
				}
			}
			return result;
		}
	}

	bool ConfigurationPath(wchar_t* path, size_t capacity)
	{
		HMODULE module = nullptr;
		if (capacity > MAX_PATH ||
			!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&Install), &module))
			return false;
		const auto length = GetModuleFileNameW(module, path, DWORD(capacity));
		if (!length || length >= capacity) return false;
		auto* slash = wcsrchr(path, L'\\');
		return slash && wcscpy_s(slash + 1, capacity - (slash + 1 - path), deployment::kConfigFile) == 0;
	}

	bool AutoServerEnabled()
	{
		std::call_once(g_configuration, ReadConfiguration);
		return g_valid && g_autoServer;
	}

	bool PublicationReady()
	{
		return !g_publicPort || g_publicReady.load();
	}

	void PrintStatus()
	{
		char ip[INET_ADDRSTRLEN]{};
		inet_ntop(AF_INET, &g_publicAddress, ip, sizeof(ip));
		OP_INFO("Network | gameplay UDP=%u requested=%u advertised=%s:%u publication=%s auto_server=%d", unsigned(g_boundPort.load()),
			unsigned(g_port), g_publicPort ? ip : "unconfigured", unsigned(g_publicPort), PublicationReady() ? "allowed" : "blocked",
			g_autoServer);
	}

	hook::Status Install(uintptr_t base)
	{
		// Dedicated NET_Start can run before the normal post_unpack component
		// pass. Install this pair at splash, then let online_services::Install
		// reuse it without trying to hook already-detoured code a second time.
		std::lock_guard installLock(g_installMutex);
		if (g_installed) return base == g_base ? hook::Status::Installed : hook::Status::Failed;
		std::call_once(g_configuration, ReadConfiguration);
		if (!g_valid) return hook::Status::Failed;
		g_base = base;
		const auto relay = g_dedicated ? hook::Status::Installed : party_transport::Install(base, g_partyRelay);
		if (relay != hook::Status::Installed) return relay;
		// Exact IW8 Replay1.20.4 bytes and caller verification: see
		// ida-reports/replay_public_endpoint_hooks.json, not an eightx address port.
		constexpr uint8_t params[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20 };
		constexpr uint8_t common[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20 };
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0x217c040), &Parameters, params, sizeof(params), g_params);
		batch.Add(reinterpret_cast<void*>(base + 0x20ec390), &CommonAddress, common, sizeof(common), g_common);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed)
		{
			g_installed = true;
			LOG_INFO("Network", "native endpoint hooks installed; dedicated=%d requested UDP=%u", int(g_dedicated), unsigned(g_port));
		}
		return status;
	}
}
