#include <winsock2.h>
#include <ws2tcpip.h>
#include "network/social/party_transport.h"
#include "network/social/party_relay.h"
#include "network/backend/frontend_backend.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "client/component/scheduler.hpp"
#include <intrin.h>
#include <memory>
#include <mutex>
#include <optional>
#include <deque>
#include <array>
namespace party_transport
{
	namespace
	{
		using chat::Json;
		uintptr_t base = 0;
		uint64_t own = 0;
		bool installed = false;
		bool relayEnabled = false;
		sockaddr_in dedicatedRoute{};
		uint64_t dedicatedRouteUntil = 0;
		std::atomic<bool> stopping{ false };
		std::atomic<uint16_t> nativePort{ 0 };
		std::atomic<uint64_t> authenticatedAt{ 0 };
		std::mutex queueLock, workerLock, shutdown_lock;
		scheduler::task_handle worker;
		bool worker_stopping{};
		bool admission_warning{};
		uint64_t generation = 0;

		struct Packet
		{
			std::string endpoint, bytes;
			uint64_t at;
		};

		std::deque<Packet> outgoing, incoming;

		void wake_worker()
		{
			std::lock_guard guard(workerLock);
			worker.wake();
		}

		// TU19 bdPlatformSocket packet descriptor: sockaddr pointer, payload pointer,
		// byte count, padding. Both bdSocket wrappers construct this exact 24-byte row.
		struct NativePacket
		{
			const sockaddr_in* address;
			const char* data;
			uint32_t size, padding;
		};

		static_assert(sizeof(NativePacket) == 24);
		using SendFn = int (*)(int, const NativePacket*);
		using BatchFn = int (*)(int, const NativePacket*, unsigned, unsigned*);
		using ReceiveFn = int (*)(int, void*, char*, int);
		using RouteFn = bool (*)(uintptr_t, uintptr_t, uintptr_t, bool);
		std::atomic<SendFn> originalSend{ nullptr };
		std::atomic<BatchFn> originalBatch{ nullptr };
		std::atomic<ReceiveFn> originalReceive{ nullptr };
		std::atomic<RouteFn> originalRoute{ nullptr };

		bool Virtual(const sockaddr_in& address)
		{
			return address.sin_family == AF_INET && address.sin_port && (ntohl(address.sin_addr.s_addr) & 0xfffe0000u) == 0xc6120000u;
		}

		bool Active()
		{
			const auto at = authenticatedAt.load(), now = GetTickCount64();
			return nativePort.load() && at && now >= at && now - at < 5000 && !stopping.load();
		}

		bool OwnSocket(int socket)
		{
			const auto port = nativePort.load();
			if (!port || socket == -1) return false;
			sockaddr_in bound{};
			int size = sizeof(bound);
			return getsockname(SOCKET(socket), reinterpret_cast<sockaddr*>(&bound), &size) == 0 && bound.sin_family == AF_INET &&
				ntohs(bound.sin_port) == port;
		}

		std::string Endpoint(const sockaddr_in& address)
		{
			std::array<uint8_t, 6> bytes{};
			memcpy(bytes.data(), &address.sin_addr, 4);
			memcpy(bytes.data() + 4, &address.sin_port, 2);
			return party_relay120::Hex(bytes.data(), bytes.size());
		}

		int Send(int socket, const NativePacket* packet)
		{
			if (!packet || !packet->address || !Virtual(*packet->address) || !OwnSocket(socket)) return originalSend.load()(socket, packet);
			if (!Active()) return -5;
			if (!packet->data || !packet->size || packet->size > 2048) return -6;
			try
			{
				{
					std::lock_guard lock(queueLock);
					if (!Active()) return -5;
					if (outgoing.size() >= 256) return -2;
					outgoing.push_back({ Endpoint(*packet->address), party_relay120::Hex(packet->data, packet->size), GetTickCount64() });
				}
				wake_worker();
				return int(packet->size); // Native UDP admission, never a delivery receipt.
			}
			catch (...)
			{
				return -2;
			}
		}

		int SendBatch(int socket, const NativePacket* packets, unsigned count, unsigned* sent)
		{
			if (!packets || !sent || !count || count > 128 || !OwnSocket(socket)) return originalBatch.load()(socket, packets, count, sent);
			bool relay = false;
			for (unsigned i = 0; i < count; ++i)
				if (packets[i].address && Virtual(*packets[i].address))
				{
					relay = true;
					break;
				}
			if (!relay) return originalBatch.load()(socket, packets, count, sent);
			// Mixed native/virtual rows preserve ordering and native partial-count semantics.
			*sent = 0;
			for (unsigned i = 0; i < count; ++i)
			{
				const auto result = Send(socket, packets + i);
				if (result < 0) return result;
				++*sent;
			}
			return 0;
		}

		int Receive(int socket, void* address, char* data, int size)
		{
			const auto result = originalReceive.load()(socket, address, data, size);
			// Both ordinary recvfrom and RIO reach this SDK boundary. First drain real
			// traffic; only replace the SDK's proven would-block result (-2).
			if (result != -2 || !address || !data || size <= 0 || !Active() || !OwnSocket(socket)) return result;
			std::lock_guard lock(queueLock);
			while (!incoming.empty())
			{
				auto packet = std::move(incoming.front());
				incoming.pop_front();
				if (!Active() || GetTickCount64() - packet.at >= 3000) continue;
				if (packet.bytes.size() > size_t(size)) return -6;
				sockaddr_storage from{};
				auto& ip = *reinterpret_cast<sockaddr_in*>(&from);
				ip.sin_family = AF_INET;
				memcpy(&ip.sin_addr, packet.endpoint.data(), 4);
				memcpy(&ip.sin_port, packet.endpoint.data() + 4, 2);
				if (!Virtual(ip)) continue;
				memcpy(address, &from, sizeof(from));
				memcpy(data, packet.bytes.data(), packet.bytes.size());
				return int(packet.bytes.size());
			}
			return result;
		}

		bool RouteAt(uintptr_t caller, uintptr_t self, uintptr_t reference, uintptr_t listener, bool throttled)
		{
			uintptr_t common = 0, table = 0, callback = 0;
			alignas(16) std::array<uint8_t, 152> address{};
			const auto read = [](uintptr_t at, void* out, size_t n)
			{
				return safemem::ReadBytes(reinterpret_cast<void*>(at), out, n);
			};
			bool resolved = false;
			if (caller == 0x218D3BE && read(reference, &common, 8) && common && read(listener, &table, 8) &&
				read(table + 8, &callback, 8) && callback == base + 0x218DFB0)
			{
				resolved = Active() && read(common + 784, address.data(), address.size()) &&
					Virtual(*reinterpret_cast<const sockaddr_in*>(address.data()));
				if (!resolved && read(common + 16, address.data(), address.size()))
				{
					const auto& candidate = *reinterpret_cast<const sockaddr_in*>(address.data());
					std::lock_guard lock(queueLock);
					resolved = dedicatedRouteUntil && GetTickCount64() < dedicatedRouteUntil && candidate.sin_family == AF_INET &&
						candidate.sin_port == dedicatedRoute.sin_port && candidate.sin_addr.s_addr == dedicatedRoute.sin_addr.s_addr;
				}
			}
			if (resolved)
			{
				// This callback consumes the existing by-value common-address reference
				// exactly once, and creates/connects the stock DTLS association.
				reinterpret_cast<void (*)(uintptr_t, uintptr_t, const void*)>(callback)(listener, reference, address.data());
				return true;
			}
			return originalRoute.load()(self, reference, listener, throttled);
		}

		bool Route(uintptr_t self, uintptr_t reference, uintptr_t listener, bool throttled)
		{
			return RouteAt(reinterpret_cast<uintptr_t>(_ReturnAddress()) - base, self, reference, listener, throttled);
		}

		struct worker_context
		{
			std::wstring current_origin;
			std::string credential, stream;
			uint64_t ack{}, serial{}, observed_generation{};
		};

		std::optional<scheduler::milliseconds> worker_tick(worker_context& context)
		{
			if (stopping.load()) return std::nullopt;
			auto& current_origin = context.current_origin;
			auto& credential = context.credential;
			auto& stream = context.stream;
			auto& ack = context.ack;
			auto& serial = context.serial;
			auto& observed_generation = context.observed_generation;
			try
			{
				const auto origin = frontend_backend::Origin();
				const auto lease = social_runtime::CommunicationsCredential(origin);
				uint64_t epoch;
				Json packets = Json::array();
				{
					std::lock_guard lock(queueLock);
					epoch = generation;
					if (origin != current_origin || lease != credential || epoch != observed_generation)
					{
						current_origin = origin;
						credential = lease;
						observed_generation = epoch;
						stream.clear();
						ack = 0;
						outgoing.clear();
						incoming.clear();
						authenticatedAt = 0;
					}
					if (!lease.empty() && nativePort.load())
					{
						authenticatedAt = GetTickCount64();
						// Three maximum-size hex packets fit the existing 16-KiB HTTP
						// request cap; no global HTTP size increase for this feature.
						while (!outgoing.empty() && packets.size() < 3)
						{
							auto packet = std::move(outgoing.front());
							outgoing.pop_front();
							if (GetTickCount64() - packet.at < 3000)
								packets.push_back({ { "to", packet.endpoint }, { "bytes", packet.bytes } });
						}
					}
				}
				if (lease.empty() || !nativePort.load() || origin.empty())
				{
					return scheduler::milliseconds{ nativePort.load() ? 50 : 3600000 };
				}
				const auto request = ++serial;
				Json body = { { "version", 1 }, { "schema", 1 }, { "xuid", std::to_string(own) }, { "lease", lease },
					{ "request", std::to_string(request) }, { "ack", std::to_string(ack) }, { "stream", stream },
					{ "packets", packets } };
				const auto response = match_http::Post(origin, L"/v1/party-relay", body.dump());
				if (response.status == 200) try
					{
						auto reply = Json::parse(response.body);
						if (reply.at("title") != "mw2019-tu19" || reply.at("schema") != 1 || chat::Id(reply, "request") != request ||
							reply.at("endpoint") != party_relay120::Endpoint(own) || reply.at("stream") != lease ||
							!reply.at("packets").is_array() || reply.at("packets").size() > 24)
							throw std::runtime_error("relay response");
						std::deque<Packet> received;
						auto nextAck = stream == lease ? ack : 0;
						for (const auto& packet : reply.at("packets"))
						{
							const auto seq = chat::Id(packet, "seq");
							if (seq <= nextAck) continue;
							const auto from = packet.at("from").get<std::string>(), hex = packet.at("bytes").get<std::string>();
							if (!chat::Hex(from, 12) || hex.empty() || hex.size() > 4096 || hex.size() % 2 ||
								!chat::Hex(hex, hex.size()))
								throw std::runtime_error("relay packet");
							received.push_back({ party_relay120::Unhex(from), party_relay120::Unhex(hex), GetTickCount64() });
							nextAck = seq;
						}
						// A response from a retired provider/lease/socket cannot re-enter the native SDK.
						if (frontend_backend::Origin() == origin && social_runtime::CommunicationsCredential(origin) == lease)
						{
							std::lock_guard lock(queueLock);
							if (generation == epoch && !stopping.load())
							{
								for (auto& packet : received)
								{
									if (incoming.size() >= 128) incoming.pop_front();
									incoming.push_back(std::move(packet));
								}
								stream = lease;
								ack = nextAck;
							}
						}
					}
					catch (...)
					{
						LOG_WARN("PartyRelay", "invalid relay response discarded");
					}
				else if (response.status == 401)
				{
					authenticatedAt = 0;
				}
				return stopping.load() ? std::nullopt : std::optional{ scheduler::milliseconds{ response.status == 200 ? 20 : 100 } };
			}
			catch (...)
			{
				LOG_ERR("PartyRelay", "transport worker stopped after an internal error");
			}
			authenticatedAt = 0;
			return std::nullopt;
		}

		void ensure_worker_locked()
		{
			if (worker || worker_stopping || !installed || !relayEnabled || !nativePort.load()) return;
			stopping = false;
			const auto context = std::make_shared<worker_context>();
			worker = scheduler::schedule([context] { return worker_tick(*context); });
			if (!worker && !admission_warning)
				LOG_WARN("PartyRelay", "shared scheduler could not admit the relay worker; retry remains armed");
			admission_warning = !worker;
		}

	}

// Exact target bytes/signatures generated from the independently traced TU19 SDK.
#include "network/social/party_transport_bindings.inc"
	hook::Status Install(uintptr_t image, bool enabled)
	{
		if (installed) return hook::Status::Installed;
		relayEnabled = enabled;
		base = image;
		own = offlineauth::GetIdentity().xuid;
		if (!own) return hook::Status::NotReady;
		uint8_t callback[sizeof(kRelayCallbackBytes)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x218DFB0), callback, sizeof(callback)) ||
			memcmp(callback, kRelayCallbackBytes, sizeof(callback)))
			return hook::Status::NotReady;
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0x21997F0), Route, kRelayRouteBytes, sizeof(kRelayRouteBytes), originalRoute);
		batch.Add(reinterpret_cast<void*>(base + 0x2181B70), Send, kRelaySendBytes, sizeof(kRelaySendBytes), originalSend);
		batch.Add(reinterpret_cast<void*>(base + 0x2181950), SendBatch, kRelayBatchBytes, sizeof(kRelayBatchBytes), originalBatch);
		batch.Add(reinterpret_cast<void*>(base + 0x21815D0), Receive, kRelayReceiveBytes, sizeof(kRelayReceiveBytes), originalReceive);
		const auto status = batch.Commit();
		installed = status == hook::Status::Installed;
		return status;
	}

	bool AdvertisedAddress(void* out)
	{
		if (!installed || !relayEnabled || !own) return false;
		const auto bytes = party_relay120::Address(own);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		memcpy(&address.sin_addr, bytes.data(), 4);
		memcpy(&address.sin_port, bytes.data() + 4, 2);
		memcpy(out, &address, sizeof(address));
		return true;
	}

	void NativePort(uint16_t port)
	{
		if (!installed) return;
		std::lock_guard guard(workerLock);
		if (worker_stopping) return;
		{
			std::lock_guard lock(queueLock);
			nativePort = port;
			authenticatedAt = 0;
			++generation;
			incoming.clear();
			outgoing.clear();
		}
		ensure_worker_locked();
		worker.wake();
	}

	void Retry()
	{
		std::lock_guard guard(workerLock);
		// Admission may have failed when the native socket was first published.
		// Existing workers keep their relay deadline; only missing tasks retry.
		ensure_worker_locked();
	}

	bool DedicatedRoute(std::string_view host, uint16_t port)
	{
		sockaddr_in endpoint{};
		endpoint.sin_family = AF_INET;
		endpoint.sin_port = htons(port);
		if (!installed || !port || host.empty() || host.size() >= INET_ADDRSTRLEN) return false;
		const std::string text(host);
		if (inet_pton(AF_INET, text.c_str(), &endpoint.sin_addr) != 1 || !endpoint.sin_addr.s_addr) return false;
		const auto ip = ntohl(endpoint.sin_addr.s_addr);
		if ((ip >> 24) == 0 || (ip >> 24) >= 224) return false;
		std::lock_guard lock(queueLock);
		dedicatedRoute = endpoint;
		dedicatedRouteUntil = GetTickCount64() + 30000;
		return true;
	}

	void ClearDedicatedRoute()
	{
		std::lock_guard lock(queueLock);
		dedicatedRouteUntil = 0;
		dedicatedRoute = {};
	}

	void Stop()
	{
		std::lock_guard shutdown_guard(shutdown_lock);
		scheduler::task_handle pending;
		{
			std::lock_guard guard(workerLock);
			worker_stopping = true;
			admission_warning = false;
			stopping = true;
			authenticatedAt = 0;
			pending = std::move(worker);
			worker = {};
			std::lock_guard lock(queueLock);
			nativePort = 0;
			++generation;
			incoming.clear();
			outgoing.clear();
			dedicatedRouteUntil = 0;
			dedicatedRoute = {};
		}
		pending.cancel(true);
		std::lock_guard guard(workerLock);
		worker_stopping = false;
		// Checked trampolines live until process exit; inactive detours call originals.
	}
}
