#include "network/social/runtime/runtime.hpp"

namespace social_runtime
{
	using namespace detail;

	// The DLL and standalone backend use the same service implementations.
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_tournament_members.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_discord_request.inl"
#endif
	std::string CommunicationsCredential(const std::wstring& url)
	{
		std::lock_guard lock(clientLock);
		const auto now = GetTickCount64();
		return communicationsOrigin == url && communicationsAt && now >= communicationsAt && now - communicationsAt < 5000
			? communicationsLease
			: std::string{};
	}

#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_authorize_communications.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_communications_live.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_communications_allowed.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_blocked.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_authorize_matchmaking.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_matchmaking_party_ready.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_request.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/social_runtime_resolve_account_name.inl"
#endif
}
