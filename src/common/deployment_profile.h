#pragma once
// Separate build products and configuration namespaces keep a distributed
// client from inheriting the host's loopback endpoint, identity or auto-server.

namespace deployment
{
#if defined(IW8X_CLIENT_ONLY)
	inline constexpr bool kClientOnly = true;
	inline constexpr bool kRadmin = false;
	inline constexpr auto kName = "IW8x client";
	inline constexpr auto kConfigFile = L"mw120rproxy.online.ini";
	inline constexpr auto kBackendHost = L"127.0.0.1";
#elif defined(REPLAY_REMOTE_CLIENT)
	inline constexpr bool kClientOnly = true;
	inline constexpr bool kRadmin = true;
	inline constexpr auto kName = "Radmin client";
	inline constexpr auto kConfigFile = L"mw120rproxy.radmin-client.ini";
	inline constexpr auto kBackendHost = L"26.173.90.18";
#elif defined(REPLAY_RADMIN_HOST)
	inline constexpr bool kClientOnly = false;
	inline constexpr bool kRadmin = true;
	inline constexpr auto kName = "Radmin host";
	inline constexpr auto kConfigFile = L"mw120rproxy.radmin-host.ini";
	inline constexpr auto kBackendHost = L"127.0.0.1";
#else
	inline constexpr bool kClientOnly = false;
	inline constexpr bool kRadmin = false;
	inline constexpr auto kName = "standard host/client";
	inline constexpr auto kConfigFile = L"mw120rproxy.online.ini";
	inline constexpr auto kBackendHost = L"127.0.0.1";
#endif
	// Full-party matchmaking and native task cancellation are the normal provider.
	// An existing explicit INI route is still honored by online_services.
	inline constexpr auto kMatchmaking = L"native";

	inline constexpr bool AutoServer(bool requested)
	{
		return !kClientOnly && requested;
	}
}
