#include "network/backend/backend_agent.h"
#include "network/social/roster_memory.h"
#include "network/backend/native_matchmaking.h"
#include "network/social/party_roster_snapshot.h"
#include "network/social/chat_runtime.h"
#include "network/backend/tournament_backend.h"
#include "network/social/chat_backend.h"
#include "network/social/social_runtime.h"
#include "network/backend/frontend_backend.h"
#include "auth/offline_auth.h"
#include "engine/game.h"
#include "common/console.h"
#include "engine/safemem.h"
#include "network/social/chat_bindings.h"
#include "client/component/scheduler.hpp"
#include "common/utils/completion_budget.hpp"
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
namespace chat_runtime
{
	namespace
	{
		using chat::Json;
		uintptr_t base = 0;
		bool installed = false, server = false;
		std::mutex serviceLock, clientLock;
#ifndef IW8X_CLIENT_ONLY
		chat::Backend service;
#endif
		std::atomic<bool> stopping{ false };
		std::atomic<uint64_t> lastContact{ 0 };
		std::atomic<bool> voiceAuthorized{ false };
		std::mutex worker_lock, shutdown_lock;
		scheduler::task_handle worker;
		bool worker_stopping{};
		std::wstring origin;
		Json snapshot;
		uint64_t snapshotAt = 0;
		uint64_t nextRequest = 0, delivered = 0, acknowledged = 0;
		std::atomic<uint64_t> received{ 0 };

		struct Work
		{
			uint64_t id = 0, at = 0;
			Json request;
		};

		struct Result
		{
			uint64_t id = 0;
			unsigned status = 0;
			Json body;
			std::wstring origin;
			std::string principal;
		};

		std::deque<Work> outgoing;
		std::deque<Result> results;

		void wake_worker()
		{
			std::lock_guard guard(worker_lock);
			worker.wake();
		}

		struct Task
		{
			uintptr_t object;
			uint64_t at;
		};

		std::map<uint64_t, Task> tasks; // Engine thread owns every native reference.
		std::map<uint64_t, int> ownedChannels;
		std::set<uint64_t> privateChannels;
		std::string clientStream;
		std::string partySession;
		uint64_t partyHost = 0;
		Json partyMembers = Json::array();
		using SendFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, uint64_t, const void*, unsigned, unsigned);
		using GateFn = int (*)(void*);
		using FilterFn = uintptr_t* (*)(uintptr_t, uintptr_t*, const char*, uintptr_t);
		using LeaveFn = uintptr_t (*)(uintptr_t, const uint64_t*, const int*);
		std::atomic<LeaveFn> originalLeave{ nullptr };
		std::atomic<FilterFn> originalFilter{ nullptr };
		std::atomic<SendFn> originalSend{ nullptr };
		std::atomic<GateFn> originalGate{ nullptr };
		using CreatePrivateFn = uintptr_t (*)(uintptr_t, unsigned, uint64_t, uint64_t, const char*, bool);
		std::atomic<CreatePrivateFn> originalCreatePrivate{ nullptr };

		template <class F>
		F native(uintptr_t rva)
		{
			return reinterpret_cast<F>(base + rva);
		}

		template <class T>
		T read(uintptr_t at)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(at), &value, sizeof(value));
			return value;
		}

		template <class T>
		void write(uintptr_t at, T value)
		{
			memcpy(reinterpret_cast<void*>(at), &value, sizeof(value));
		}

		std::string hex(const void* data, size_t size)
		{
			const auto* p = static_cast<const uint8_t*>(data);
			std::string s;
			constexpr char digits[] = "0123456789abcdef";
			for (size_t i = 0; i < size; ++i)
			{
				s += digits[p[i] >> 4];
				s += digits[p[i] & 15];
			}
			return s;
		}

		std::string random()
		{
			std::array<uint8_t, 16> bytes{};
			if (BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
			return hex(bytes.data(), bytes.size());
		}

		bool connected()
		{
			const auto at = lastContact.load();
			return at && GetTickCount64() - at < 5000;
		}

		uintptr_t Channel(uint64_t id)
		{
			for (unsigned i = 0; i < 38; ++i)
			{
				const auto p = base + 0xB6D19E0 + 20856ull * i;
				if (read<int>(p + 4) == 1 && read<uint64_t>(p + 24) == id) return p;
			}
			return 0;
		}

		void release(uintptr_t object)
		{
			if (_InterlockedDecrement(reinterpret_cast<volatile long*>(object + 8)) == 0)
				reinterpret_cast<void (*)(uintptr_t, unsigned)>(*reinterpret_cast<uintptr_t*>(read<uintptr_t>(object)))(object, 1);
		}

		void complete(uint64_t id, bool ok)
		{
			auto it = tasks.find(id);
			if (it == tasks.end()) return;
			const auto object = it->second.object;
			if (read<int>(object + 28) == 1)
			{
				write<int>(object + 80, ok ? 0 : 1);
				write<int>(object + 28, 2);
				if (ok) ++acknowledged;
				LOG_INFO("Chat", "native send completion request=%llu accepted=%d", id, int(ok));
			}
			release(object);
			tasks.erase(it);
		}

		uintptr_t* Send(
			uintptr_t self, uintptr_t* output, const char* category, uint64_t channel, const void* bytes, unsigned length, unsigned flags)
		{
			if (!category || !ownedChannels.contains(channel) || strcmp(category, ownedChannels.at(channel) == 6 ? "clanChat" : "chat"))
				return originalSend.load()(self, output, category, channel, bytes, length, flags);
			*output = 0;
			if (!bytes || !length || length > 499 || !connected() || tasks.size() >= 32) return output;
			const std::string text(static_cast<const char*>(bytes), length);
			if (!chat::Text(text, 499)) return output;
			// Return a REAL native bdRemoteTask. Native SendMessageToChat retains it in
			// TaskManager and runs F3E0F0 after the dedicated service acknowledges it.
			const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
			if (!object) return output;
			native<uintptr_t (*)(uintptr_t)>(0x20FDEF0)(object);
			write<long>(object + 8, 2); // returned bdReference + our outstanding operation
			write<float>(object + 24, 10.0f);
			write<int>(object + 28, 1);
			const auto id = ++nextRequest;
			const auto now = GetTickCount64();
			tasks.emplace(id, Task{ object, now });
			*output = object;
			{
				std::lock_guard guard(clientLock);
				outgoing.push_back({ id, now,
					Json{ { "request", std::to_string(id) }, { "channel", std::to_string(channel) }, { "text", text }, { "flags", flags },
						{ "stream", clientStream } } });
			}
			wake_worker();
			LOG_INFO("Chat", "native channel send request=%llu type=%d bytes=%u", id, ownedChannels.at(channel), length);
			return output;
		}

		uintptr_t CreatePrivate(uintptr_t self, unsigned controller, uint64_t id, uint64_t member, const char* name, bool initiated)
		{
			const auto object = originalCreatePrivate.load()(self, controller, id, member, name, initiated);
			if (object && controller == 0 && connected())
			{
				const auto identity = offlineauth::GetIdentity();
				if (identity.xuid && chat::Text(identity.name, 35))
				{
					native<void (*)(uintptr_t, uint64_t, const char*, bool)>(0x1B31530)(object, identity.xuid, identity.name, false);
					privateChannels.insert(id);
				}
			}
			return object;
		}

		uintptr_t* Filter(uintptr_t self, uintptr_t* output, const char* text, uintptr_t response)
		{
			// Scope this adapter to the five native chat filter-response objects.
			// An absent optional moderation service must return a FAILED task, not
			// nullptr: F3/1B31C90 then releases its slot and preserves the original text.
			// Retail's task-creation failure path leaks that slot and stalls message6.
			const auto first = base + 0x11ACD610 + 8;
			if (response < first || response >= first + 5 * 1064 || (response - first) % 1064)
				return originalFilter.load()(self, output, text, response);
			const auto message = read<uintptr_t>(response - 8);
			const auto chat = message ? read<uintptr_t>(message + 560) : 0;
			if (!chat ||
				(!ownedChannels.contains(read<uint64_t>(chat + 24)) &&
					!(read<int>(chat) == 7 && privateChannels.contains(read<uint64_t>(chat + 24)))))
				return originalFilter.load()(self, output, text, response);
			const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
			*output = object;
			if (!object) return output;
			native<uintptr_t (*)(uintptr_t)>(0x20FDEF0)(object);
			write<long>(object + 8, 1);
			write<int>(object + 80, 1);
			write<int>(object + 28, 2);
			return output;
		}

		int Gate(void* state)
		{
			if (!connected()) return originalGate.load()(state);
			// This is platform CHAT availability only, backed by a live local service
			// lease. The native saved textChatEnabled option and mute checks still run.
			native<void (*)(void*, int)>(game::kLuaPushBooleanRVA)(state, 0);
			return 1;
		}

		void RemoveChannel(uint64_t id, int type)
		{
			const auto p = Channel(id);
			if (!p || read<int>(p) != type) return;
			native<void (*)(int, int, uint64_t)>(0xF08490)(0, type, id);
			native<void (*)(uintptr_t)>(0x1B320D0)(p);
			auto& count = *reinterpret_cast<int*>(base + 0xB7931B0);
			if (count > 0) --count;
		}

		uintptr_t Leave(uintptr_t self, const uint64_t* id, const int* type)
		{
			// F3CFD0 enqueues a retail DW leave before attempting its task. Missing
			// retail lobby chat metadata makes gameplay repeatedly leave our rooms;
			// failed tasks never drain that list, eventually overrunning it at F3D0AD.
			// These rooms belong to the authenticated local service. Apply/lease-loss
			// owns their native removal; unrelated retail rooms retain stock behavior.
			if (id && type)
			{
				const auto found = ownedChannels.find(*id);
				if (found != ownedChannels.end() && found->second == *type) return 0;
			}
			return originalLeave.load()(self, id, type);
		}

		void UpdateAvailability()
		{
			// TextChat caches its availability at construction. The retail profile
			// callback emits this event; a service lease must emit the same transition.
			const auto vm = read<uintptr_t>(base + 0x11868880);
			const bool active = connected() && read<uint8_t>(base + 0xEF28AE0 + 2763) != 0;
			static uintptr_t previousVm = 0;
			static int previous = -1;
			if (!vm || (previousVm == vm && previous == int(active))) return;
			if (native<bool (*)(int, const char*, uintptr_t)>(0x19BCC30)(0, "text_chat_updated", vm))
			{
				native<void (*)(const char*, bool, uintptr_t)>(0x19CE460)("active", active, vm);
				native<void (*)(const char*, int, uintptr_t)>(0x19CE4F0)("controller", 0, vm);
				native<void (*)(uintptr_t)>(0x19BDA00)(vm);
				previousVm = vm;
				previous = active;
				LOG_INFO("Chat", "native availability event active=%d", int(active));
			}
		}

		bool Apply(const Json& reply)
		{
			if (!reply.is_object() || !reply.contains("channels") || !reply["channels"].is_array() || reply["channels"].size() > 4 ||
				!reply.contains("events") || !reply["events"].is_array() || reply["events"].size() > 12)
				return false;
			std::map<uint64_t, int> desired;
			std::set<int> types;
			for (const auto& c : reply["channels"])
			{
				const auto id = chat::Id(c, "id");
				const int type = c.value("type", -1);
				if (!id || type < 3 || type > 6 || !types.insert(type).second || !desired.emplace(id, type).second ||
					!c.contains("members") || !c["members"].is_array() || c["members"].empty() || c["members"].size() > 200)
					return false;
				std::set<uint64_t> ids;
				for (const auto& m : c["members"])
					if (!chat::Id(m, "xuid") || !chat::Text(m.value("name", std::string{}), 35) || !ids.insert(chat::Id(m, "xuid")).second)
						return false;
				if (!ids.contains(offlineauth::GetIdentity().xuid)) return false;
			}
			for (auto [id, type] : ownedChannels)
				if (!desired.contains(id)) RemoveChannel(id, type);
			ownedChannels = desired;
			for (const auto& c : reply["channels"])
			{
				const auto id = chat::Id(c, "id");
				const int type = c["type"];
				auto p = Channel(id);
				const bool fresh = !p;
				if (fresh)
				{
					if (read<int>(base + 0xB7931B0) >= 37) continue;
					native<void (*)(uintptr_t, int, uint64_t, int)>(0xF3D830)(base + 0x414A740, 0, id, type);
					p = Channel(id);
					if (!p) continue;
				}
				std::set<uint64_t> members;
				for (const auto& m : c["members"])
					members.insert(chat::Id(m, "xuid"));
				// Remove in reverse order because native removal compacts its array.
				for (int i = read<int>(p + 16) - 1; i >= 0; --i)
				{
					const auto uid = read<uint64_t>(p + 48 + 56ull * i);
					if (!members.contains(uid)) native<void (*)(uintptr_t, uint64_t)>(0x1B31F80)(p, uid);
				}
				for (const auto& m : c["members"])
				{
					const auto uid = chat::Id(m, "xuid");
					bool found = false;
					for (int i = 0; i < read<int>(p + 16); ++i)
						if (read<uint64_t>(p + 48 + 56ull * i) == uid)
						{
							std::array<char, 36> current{};
							safemem::ReadBytes(reinterpret_cast<void*>(p + 48 + 56ull * i + 8), current.data(), 35);
							found = m["name"].get_ref<const std::string&>() == current.data();
							if (!found) native<void (*)(uintptr_t, uint64_t)>(0x1B31F80)(p, uid);
							break;
						}
					if (!found)
						native<void (*)(uintptr_t, uint64_t, const char*, bool)>(0x1B31530)(
							p, uid, m["name"].get_ref<const std::string&>().c_str(), false);
				}
				if (fresh)
				{
					native<void (*)(int, int, uint64_t, bool)>(0xF083F0)(0, type, id, false);
					LOG_INFO("Chat", "native channel ready type=%d members=%zu", type, members.size());
				}
			}
			for (const auto& e : reply["events"])
			{
				const auto seq = chat::Id(e, "seq"), id = chat::Id(e, "channel"), sender = chat::Id(e, "sender");
				if (!seq || seq <= received) continue;
				const auto text = e.value("text", std::string{}), name = e.value("name", std::string{});
				const unsigned flags = e.value("flags", 0u);
				if (!sender || !chat::Text(text, 499) || !chat::Text(name, 35) || (flags & ~0x7FFu)) return false;
				if (e.value("type", 0) == 7)
				{
					// TU19's original DW receiver handles private channel creation,
					// block policy, history and LUI notification. Packet prefix is 5.
					const auto packet = "CHAT:" + text;
					native<uintptr_t (*)(uintptr_t, unsigned, uint64_t, const char*, const char*)>(0xF3A9F0)(
						base + 0x414A740, 0, sender, name.c_str(), packet.c_str());
					privateChannels.insert(sender);
					++delivered;
				}
				else if (ownedChannels.contains(id) && Channel(id))
				{
					native<void (*)(uintptr_t, const char*, uint64_t, uint64_t, const char*, unsigned, const char*)>(0xF3D410)(
						base + 0x414A740, ownedChannels.at(id) == 6 ? "clanChat" : "chat", id, sender, name.c_str(), flags, text.c_str());
					++delivered;
					LOG_INFO("Chat", "native receive sequence=%llu type=%d sender=%llu bytes=%zu", seq, ownedChannels.at(id), sender,
						text.size());
				}
				received = seq;
			}
			return true;
		}

		struct worker_context
		{
			std::wstring active_origin;
			std::string lease, principal, nonce;
			uint64_t ack{}, next_sync{};
			std::map<std::wstring, unsigned> last_status;
		};

		std::optional<scheduler::milliseconds> worker_tick(worker_context& context)
		{
			if (stopping.load()) return std::nullopt;
			if (context.nonce.empty()) context.nonce = random();
			if (context.nonce.empty()) return std::nullopt;
			auto& active_origin = context.active_origin;
			auto& lease = context.lease;
			auto& principal = context.principal;
			auto& nonce = context.nonce;
			auto& ack = context.ack;
			auto& last_status = context.last_status;
			Json state;
			std::wstring target;
			Work send;
			uint64_t captured = 0;
			{
				std::lock_guard guard(clientLock);
				state = snapshot;
				target = origin;
				captured = snapshotAt;
			}
			if (state.is_null() || target.empty())
			{
				return scheduler::milliseconds{ 3600000 };
			}
			const auto credential = social_runtime::CommunicationsCredential(target);
			if (active_origin != target || principal != credential)
			{
				active_origin = target;
				principal = credential;
				lease.clear();
				ack = 0;
				context.next_sync = 0;
				lastContact = 0;
			}
			if (principal.empty())
			{
				// Keep admitted requests until authentication recovers. Their native
				// task deadline still owns failure if the provider remains absent.
				return scheduler::milliseconds{ 100 };
			}
			auto post = [&](std::wstring_view path, Json body)
			{
				body["version"] = 1;
				body["xuid"] = state["xuid"];
				body["lease"] = lease;
				body["social_lease"] = principal;
				auto reply = match_http::Post(target, path, body.dump());
				const std::wstring key(path);
				auto prior = last_status.find(key);
				if (prior == last_status.end() || prior->second != reply.status)
				{
					LOG_INFO("Chat", "service %ls status=%u", key.c_str(), reply.status);
					last_status[key] = reply.status;
				}
				return reply;
			};
			if (lease.empty())
			{
				const auto now = GetTickCount64();
				if (now < context.next_sync)
					return scheduler::milliseconds{ static_cast<int64_t>(context.next_sync - now) };
				auto opened = post(L"/v1/chat/open", Json{ { "nonce", nonce }, { "name", state["name"] } });
				context.next_sync = GetTickCount64() + 250;
				if (opened.status == 200) try
					{
						auto j = Json::parse(opened.body);
						auto value = j.value("lease", std::string{});
						if (chat::Hex(value, 32))
						{
							lease = value;
							ack = 0;
							context.next_sync = 0;
							received = 0;
							std::lock_guard guard(clientLock);
							results.push_back({ 0, 201, Json{ { "stream", lease } }, target, principal });
						}
					}
					catch (...)
					{
					}
			}
			if (!lease.empty())
			{
				if (social_runtime::CommunicationsCredential(target) != principal) return scheduler::milliseconds{ 0 };
				std::lock_guard guard(clientLock);
				// A publication can replace the provider while the open request is
				// in flight. Preserve its queued work for the next validated tick.
				if (origin != target || snapshot.is_null()) return scheduler::milliseconds{ 0 };
				if (!outgoing.empty())
				{
					send = std::move(outgoing.front());
					outgoing.pop_front();
				}
			}
			if (send.id)
			{
				match_http::Reply reply{ 0, {} };
				if (!lease.empty() && send.request.value("stream", std::string{}) == lease && GetTickCount64() - send.at < 9000)
				{
					reply = post(L"/v1/chat/send", send.request);
					// A lost response may already have routed the message. Retry
					// the same request ID so the service returns its cached ACK.
					if (!reply.status && !stopping) reply = post(L"/v1/chat/send", send.request);
				}
				Json body;
				try
				{
					body = Json::parse(reply.body);
				}
				catch (...)
				{
				}
				{
					std::lock_guard guard(clientLock);
					results.push_back({ send.id, reply.status, std::move(body), target, principal });
				}
				if (reply.status == 401)
				{
					lease.clear();
					context.next_sync = GetTickCount64() + 250;
					lastContact = 0;
				}
			}
			if (!lease.empty() && GetTickCount64() >= context.next_sync)
			{
				{
					std::lock_guard guard(clientLock);
					ack = received;
				}
				state["ack"] = std::to_string(ack);
				state["snapshot_fresh"] = captured && GetTickCount64() - captured <= 5000;
				auto reply = post(L"/v1/chat/sync", state);
				// A producer wake admits messages immediately without shortening the
				// recurring sync deadline, including wakes received during HTTP work.
				context.next_sync = GetTickCount64() + 250;
				if (reply.status == 200) try
					{
						auto j = Json::parse(reply.body);
						std::lock_guard guard(clientLock);
						// Keep one snapshot in flight. The server retains unacknowledged
						// events; replacing a snapshot cannot lose a message.
						std::erase_if(results,
							[](const Result& r)
							{
								return r.id == 0 && r.status == 200;
							});
						j["stream"] = lease;
						results.push_back({ 0, 200, std::move(j), target, principal });
					}
					catch (...)
					{
					}
				else if (reply.status == 401)
				{
					lease.clear();
					lastContact = 0;
				}
			}
			if (stopping.load()) return std::nullopt;
			{
				std::lock_guard guard(clientLock);
				if (!lease.empty() && !outgoing.empty()) return scheduler::milliseconds{ 0 };
			}
			const auto now = GetTickCount64();
			return scheduler::milliseconds{ context.next_sync > now ? static_cast<int64_t>(context.next_sync - now) : 0 };
		}

	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/chat_runtime_request.inl"
#endif
	uintptr_t* SendWhisper(uintptr_t* output, uint64_t target, const void* bytes, unsigned size)
	{
		*output = 0;
		if (!installed || server || !connected() || !target || target == offlineauth::GetIdentity().xuid || !bytes || size < 6 ||
			size > 505 || memcmp(bytes, "CHAT:", 5) || tasks.size() >= 32)
			return output;
		std::string text(static_cast<const char*>(bytes) + 5, size - 5);
		if (!text.empty() && !text.back()) text.pop_back();
		if (!chat::Text(text, 499)) return output;
		const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
		if (!object) return output;
		native<uintptr_t (*)(uintptr_t)>(0x20FDEF0)(object);
		write<long>(object + 8, 2);
		write<float>(object + 24, 10.0f);
		write<int>(object + 28, 1);
		const auto id = ++nextRequest, now = GetTickCount64();
		tasks.emplace(id, Task{ object, now });
		*output = object;
		{
			std::lock_guard guard(clientLock);
			outgoing.push_back({ id, now,
				Json{ { "request", std::to_string(id) }, { "channel", "0" }, { "target", std::to_string(target) }, { "text", text },
					{ "flags", 0 }, { "stream", clientStream } } });
		}
		wake_worker();
		return output;
	}
#ifndef IW8X_CLIENT_ONLY

	void PublishDedicated(std::string session)
	{
		if (!installed || !server) return;
		static uint64_t next = 0;
		const auto now = GetTickCount64();
		if (now < next) return;
		next = now + 250;
		party_roster::Memory memory;
		std::vector<chat::Player> players;
		const auto entities = memory.Read<uintptr_t>(base + 0xBC20F00);
		if (!session.empty())
			for (int i = 0; i < std::clamp(memory.Read<int>(base + 0xE17FCD0), 0, 200); ++i)
			{
				const auto client = memory.Read<uintptr_t>(base + 0xE17F690 + 8ull * i);
				if (!client || memory.Read<uint8_t>(client + 8) < 3 || memory.Read<int>(client + 214756) == 2) continue;
				const auto member = base + native_matchmaking::ServerPartyRVA() + 488ull * i;
				if (memory.Read<uint8_t>(member + 2594) != 4 && memory.Read<uint8_t>(member + 2594) != 5) continue;
				chat::Player p;
				p.xuid = memory.Read<uint64_t>(member + 2600);
				std::array<char, 36> name{};
				memory.Copy(member + 2244, name.data(), 35);
				p.name = name.data();
				const auto gc = entities ? memory.Read<uintptr_t>(entities + 0x5A0ull * i + 0x150) : 0;
				if (gc && memory.Read<int>(gc + 448) == i && memory.Read<int>(gc + 21368) == 2) p.team = memory.Read<int>(gc + 22016);
				if (p.xuid) players.push_back(std::move(p));
			}
		if (backend_agent::Enabled())
		{
			backend_agent::Roster(players);
			return;
		}
		std::map<std::string, int> roster;
		for (const auto& p : players)
			roster[std::to_string(p.xuid)] = p.team;
		tournament_backend::PublishRoster(roster);
		// Delivery may be consulting persistent block policy on an HTTP worker.
		// Skip a contended publication rather than stalling the simulation thread.
		std::unique_lock guard(serviceLock, std::try_to_lock);
		if (guard.owns_lock()) service.Publish(std::move(session), std::move(players), now);
	}
#endif


	bool VoicePermission()
	{
		return voiceAuthorized.load() && connected() && frontend_backend::Authenticated();
	}

	void ClientPump(const std::wstring& url, std::string gameSession)
	{
		if (!installed || server || !offlineauth::IsReady()) return;
		const auto now = GetTickCount64();
		const auto credential = social_runtime::CommunicationsCredential(url);
		for (utils::completion_budget budget; budget.next();)
		{
			Result r;
			{
				std::lock_guard guard(clientLock);
				if (results.empty()) break;
				r = std::move(results.front());
				results.pop_front();
			}
			try
			{
				if (r.origin != url || r.principal != credential || credential.empty())
				{
					if (r.id) complete(r.id, false);
					continue;
				}
				if (r.id)
					complete(r.id, r.status == 200 && r.body.value("accepted", false) && chat::Id(r.body, "request") == r.id);
				else if (r.status == 201)
				{
					received = 0;
					clientStream = r.body.value("stream", std::string{});
					for (auto [id, type] : ownedChannels)
						RemoveChannel(id, type);
					ownedChannels.clear();
					for (auto id : privateChannels)
						RemoveChannel(id, 7);
					privateChannels.clear();
				}
				else if (r.status == 200 && r.body.value("stream", std::string{}) == clientStream && Apply(r.body))
				{
					voiceAuthorized = r.body.value("communications", false);
					lastContact = now;
				}
			}
			catch (...)
			{
				LOG_WARN("Chat", "discarded malformed service response");
			}
		}
		std::vector<uint64_t> expired;
		for (auto [id, t] : tasks)
			if (now - t.at > 10000) expired.push_back(id);
		for (auto id : expired)
			complete(id, false);
		if (credential.empty()) lastContact = 0;
		if (!connected() && !ownedChannels.empty())
		{
			for (auto [id, type] : ownedChannels)
				RemoveChannel(id, type);
			ownedChannels.clear();
		}
		if (!connected() && !privateChannels.empty())
		{
			for (auto id : privateChannels)
				RemoveChannel(id, 7);
			privateChannels.clear();
		}
		UpdateAvailability();
		static uint64_t next = 0;
		if (now < next) return;
		next = now + 250;
		const auto identity = offlineauth::GetIdentity();
		if (!identity.xuid) return;
		const auto party = base + 0xC796480;
		// A dedicated handoff replaces/sleeps this PartyData before map loading.
		// Preserve the last real menu-party roster while it sleeps in a match.
		if (native<bool (*)()>(game::kIsInFrontEndRVA)() && read<int>(base + 0xC7F1870) == 0 && read<int>(party + 311120) == 1)
		{
			partySession.clear();
			partyHost = 0;
			partyMembers = Json::array();
			if (read<int>(party + 311116) == 1)
			{
				const auto session = read<uintptr_t>(party);
				const auto id = session ? read<uint64_t>(session + 88) : 0;
				const int host = read<int>(party + 311000);
				if (id) partySession = hex(&id, 8);
				if (read<int>(party + 311112) == 1)
					partyHost = identity.xuid;
				else if (host >= 0 && host < 200)
					partyHost = read<uint64_t>(party + 488ull * host + 2600);
				for (auto uid : party_roster::Members(party))
					partyMembers.push_back(std::to_string(uid));
			}
		}
		Json s = { { "xuid", std::to_string(identity.xuid) }, { "name", identity.name }, { "host", std::to_string(partyHost) },
			{ "party", partySession }, { "members", partyMembers }, { "game", gameSession } };
		{
			std::lock_guard guard(clientLock);
			snapshot = std::move(s);
			origin = url;
			snapshotAt = now;
		}
		std::lock_guard guard(worker_lock);
		if (worker_stopping) return;
		if (!worker)
		{
			stopping = false;
			const auto context = std::make_shared<worker_context>();
			worker = scheduler::schedule([context] { return worker_tick(*context); });
			if (!worker) LOG_WARN("Chat", "shared scheduler could not admit the chat worker");
		}
		else worker.wake();
	}

	void Shutdown()
	{
		std::lock_guard shutdown_guard(shutdown_lock);
		scheduler::task_handle pending;
		{
			std::lock_guard guard(worker_lock);
			worker_stopping = true;
			stopping = true;
			pending = std::move(worker);
			worker = {};
		}
		pending.cancel(true);
		lastContact = 0;
		std::lock_guard guard(worker_lock);
		worker_stopping = false;
	}

	bool HandleCommand(std::string_view command)
	{
		if (command == "chat_status")
		{
			OP_INFO("Chat | backend=%d channels=%zu pending=%zu acknowledged=%llu received=%llu", int(connected()), ownedChannels.size(),
				tasks.size(), acknowledged, delivered);
			for (auto [id, type] : ownedChannels)
				OP_INFO("  type=%d channel=%llu members=%d", type, id, read<int>(Channel(id) + 16));
			return true;
		}
		if (!command.starts_with("chat_send ")) return false;
		std::istringstream input{ std::string(command.substr(10)) };
		std::string name, text;
		input >> name;
		std::getline(input, text);
		if (!text.empty() && text.front() == ' ') text.erase(0, 1);
		const int type = name == "party" ? 3 : name == "all" ? 4 : name == "team" ? 5 : name == "global" ? 6 : -1;
		if (server || !connected() || !chat::Text(text, 499))
		{
			OP_WARN("chat_send <global|party|all|team> <text> requires a ready native channel");
			return true;
		}
		for (auto [id, t] : ownedChannels)
			if (t == type)
			{
				native<void (*)(uintptr_t, int, const uint64_t*, const int*, const char*)>(0xF3D9D0)(
					base + 0x414A740, 0, &id, &type, text.c_str());
				return true;
			}
		OP_WARN("Chat channel is not joined");
		return true;
	}

	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (installed) return hook::Status::Installed;
		base = image;
		server = dedicated;
#ifndef IW8X_CLIENT_ONLY
		if (server)
		{
			uint64_t seed = 0;
			if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
				return hook::Status::Failed;
			service = chat::Backend(seed & 0x0fffffffffffffffull);
			installed = true;
			LOG_INFO("Chat", "dedicated channel service enabled on existing HTTP listener");
			return hook::Status::Installed;
		}
#endif

		for (const auto& binding : kHelpers)
		{
			std::array<uint8_t, 16> bytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), bytes.data(), bytes.size()) ||
				memcmp(bytes.data(), binding.bytes, bytes.size()))
			{
				LOG_ERR("Chat", "native binding mismatch RVA=%llx", binding.rva);
				return hook::Status::NotReady;
			}
		}
		constexpr uint8_t send[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
		constexpr uint8_t gate[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x42, 0xeb, 0xc7, 0xff, 0x48, 0x8b };
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0xF76B80), &Send, send, sizeof(send), originalSend);
		batch.Add(reinterpret_cast<void*>(base + 0x19F1770), &Gate, gate, sizeof(gate), originalGate);
		constexpr uint8_t filter[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xec, 0x68, 0x04, 0x00, 0x00, 0x48, 0x8b, 0x05, 0x65 };
		batch.Add(reinterpret_cast<void*>(base + 0xF76880), &Filter, filter, sizeof(filter), originalFilter);
		constexpr uint8_t leave[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xfa };
		batch.Add(reinterpret_cast<void*>(base + 0xF3CFD0), &Leave, leave, sizeof(leave), originalLeave);
		constexpr uint8_t createPrivate[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48,
			0x83 };
		batch.Add(reinterpret_cast<void*>(base + 0xF3BB20), &CreatePrivate, createPrivate, sizeof(createPrivate), originalCreatePrivate);
		const auto status = batch.Commit();
		installed = status == hook::Status::Installed;
		if (installed) LOG_INFO("Chat", "native channel send/task and service-backed availability installed");
		return status;
	}
}
