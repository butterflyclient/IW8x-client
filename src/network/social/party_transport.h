#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
#include <string_view>
namespace party_transport
{
	// Configured before the native CommonAddr is hashed; native TLS/DTLS stays intact.
	hook::Status Install(uintptr_t base, bool enabled);
	bool AdvertisedAddress(void* sockaddr16);
	void NativePort(uint16_t port);
	bool DedicatedRoute(std::string_view host, uint16_t port);
	void ClearDedicatedRoute();
	void Retry();
	void Stop();
}
