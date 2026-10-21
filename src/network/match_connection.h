#pragma once
#include "network/online_services_protocol.h"

#include <array>
namespace matchmaking
{
	using WireEndpoint = std::array<uint8_t, 6>;

	// TU19 CommonAddr deserialize20EC6B0 consumes five optional local addresses;
	// hash20EC5C0 uses the public address when present. Keep bytes30..107 identical,
	// including native security ID/key, NAT/platform and public identity.
	inline online_services::protocol::Session RouteSession(online_services::protocol::Session session, const WireEndpoint& endpoint)
	{
		std::fill(session.begin(), session.begin() + 30, 0);
		std::copy(endpoint.begin(), endpoint.end(), session.begin());
		return session;
	}

	// Only the authenticated HTTP peer determines the same-machine shortcut.
	// Remote peers use the published endpoint; never prefer an installed VPN NIC.
	// Retain the public identity and DTLS material even for loopback transport.
	inline online_services::protocol::Session NativeRouteSession(
		online_services::protocol::Session session, const std::array<uint8_t, 4>& peer)
	{
		WireEndpoint endpoint{};
		std::copy_n(session.begin() + 30, 6, endpoint.begin());
		// Offline bdCommonAddr has no public endpoint. Its actual bound port is
		// in the local addresses (live TU19: locals UDP3075, public port zero).
		// Never turn that missing public endpoint into loopback:0. Remote LAN
		// peers retain native local-address selection when no public route exists.
		if (endpoint[4] == 0 && endpoint[5] == 0)
		{
			if (peer[0] != 127) return session;
			bool found = false;
			for (size_t offset = 0; offset < 30; offset += 6)
			{
				if ((session[offset + 4] || session[offset + 5]) &&
					(session[offset] || session[offset + 1] || session[offset + 2] || session[offset + 3]))
				{
					std::copy_n(session.begin() + offset, 6, endpoint.begin());
					found = true;
					break;
				}
			}
			if (!found) return session;
		}
		if (peer[0] == 127)
		{
			endpoint[0] = 127;
			endpoint[1] = 0;
			endpoint[2] = 0;
			endpoint[3] = 1;
		}
		return RouteSession(session, endpoint);
	}
	enum class ConnectionPhase
	{
		Idle,
		TryPublic,
		TryLan,
		Connected,
		Failed
	};
	enum class ConnectionAction
	{
		None,
		TryLan,
		Failed,
		Connected
	};

	inline bool TransportJoinFailure(std::string_view reason)
	{
		return reason == "EXE/HOSTUNREACH" || reason == "EXE/TIMEDOUT" || reason == "EXE/LOSTCONNECTION" ||
			reason == "XBOXLIVE/CANTJOINSESSION";
	}

	// The menu scene runs a local frontEntclient in CA_ACTIVE(9). It is safe
	// to retry there once the native join has finished cleaning up. Loading a
	// remote map or an unfinished frontend scene must never trigger a retry.
	inline bool ConnectionIdle(bool frontend, bool sceneReady, int clientState, int joinState)
	{
		return frontend && joinState == 0 && (clientState == 0 || (sceneReady && clientState == 9));
	}

	struct Connection
	{
		ConnectionPhase phase = ConnectionPhase::Idle;
		uint64_t began = 0;
		unsigned failure = 0;
		bool hasLan = false;

		void Start(uint64_t now, unsigned serial, bool fallback)
		{
			phase = ConnectionPhase::TryPublic;
			began = now;
			failure = serial;
			hasLan = fallback;
		}

		void Cancel()
		{
			*this = {};
		}

		bool Pending() const
		{
			return phase == ConnectionPhase::TryPublic || phase == ConnectionPhase::TryLan;
		}

		ConnectionAction Advance(uint64_t now, unsigned serial, bool active, bool idle)
		{
			if (phase != ConnectionPhase::TryPublic && phase != ConnectionPhase::TryLan) return ConnectionAction::None;
			if (active)
			{
				phase = ConnectionPhase::Connected;
				return ConnectionAction::Connected;
			}
			// Never interrupt an in-flight native handshake/loading transition.
			// Native transport owns its timeout. A local timer alone must not
			// manufacture a failure or retry a rejected/cancelled assignment.
			if (!idle || serial == failure) return ConnectionAction::None;
			if (phase == ConnectionPhase::TryPublic && hasLan)
			{
				phase = ConnectionPhase::TryLan;
				began = now;
				failure = serial;
				return ConnectionAction::TryLan;
			}
			phase = ConnectionPhase::Failed;
			return ConnectionAction::Failed;
		}
	};
}
