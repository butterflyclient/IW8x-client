#include "network/backend/backend_agent.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/tournament_backend.h"
#include "network/commerce/store_backend.h"
#include "network/profile/progression_backend.h"
#include "network/commerce/custom_mod_backend.h"
#include "network/profile/profile_backend.h"
#include "network/profile/settings_backend.h"
#include "engine/camo_changer.h"
#include "auth/profile/local_settings.h"
#include "auth/profile/local_stats.h"
#include "network/backend/name_validation.h"
#include "network/backend/frontend_service.h"
#include "network/backend/frontend_bindings.h"
#include "network/social/social_runtime.h"
#include "network/backend/playlist_backend.h"
#include "network/playlists.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_playercard.h"
#include "auth/profile/local_progression.h"
#include "auth/profile/local_challenges.h"
#include "common/console.h"
#include "engine/safemem.h"
#include "engine/game.h"
#include "engine/commands.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ctime>
namespace frontend_backend
{
	namespace
	{
		using frontend_service::Json;
		uintptr_t base = 0;
		bool installed = false, server = false, pending = false;
		std::atomic<bool> required{ false }, premium{ false };
		std::atomic<uint64_t> until{ 0 };
		std::mutex configLock, serviceLock;
		std::wstring configuredOrigin, activeOrigin;
#ifndef IW8X_CLIENT_ONLY
		Json policy = frontend_service::Defaults();
		bool serviceReady = false;
		Json linkedAccounts = Json::object();
		bool accountStoreReady = false;
		Json motdCatalog = motd::Defaults(), motdReads = Json::object();
		bool motdCatalogReady = false, motdReadsReady = false;
#endif
		bool accountLinked = false;
		uint32_t motdRevision = 0;
		bool loginPending = false, loginResultReady = false, loginSucceeded = false;
		uint64_t loginSerial = 0, loginDeadline = 0, loginVm = 0;
		uint64_t generation = 0, sequence = 0, nextFetch = 0, nextLog = 0;
		std::string playlistHash;
		unsigned lastStatus = 0;
		bool patchReady = false;
		using FenceFn = bool (*)(int, uint64_t);
		using PlatformFn = int (*)(uintptr_t, int);
		using PrivilegesFn = bool (*)(int, unsigned*);
		using PatchFn = unsigned char (*)(uintptr_t, int);
		using patch_version_fn = uint16_t (*)(uintptr_t, int);
		using PredicateFn = bool (*)(uintptr_t, int);
		using QosFn = bool (*)();
		std::atomic<FenceFn> originalComplete{ nullptr }, originalFailed{ nullptr };
		std::atomic<PlatformFn> originalPlatform{ nullptr };
		std::atomic<PrivilegesFn> originalPrivileges{ nullptr };
		std::atomic<PatchFn> originalPatch{ nullptr };
		std::atomic<patch_version_fn> original_patch_version{ nullptr };
		std::atomic<PredicateFn> originalPatchData{ nullptr }, originalCommerce{ nullptr };
		std::atomic<QosFn> originalQos{ nullptr };
		std::atomic<uintptr_t (*)()> originalOnlineTransition{ nullptr };
		using LuaFn = int (*)(uintptr_t);
		std::atomic<LuaFn> originalUpgrade{ nullptr }, originalLogin{ nullptr }, originalCancel{ nullptr }, originalHasAccount{ nullptr };
		using ContentFn = char (*)(uintptr_t, unsigned, const char*);
		using ContentFrameFn = void (*)(uintptr_t, unsigned);
		using MotdFn = char (*)(uintptr_t, unsigned);
		using MotdReadFn = char (*)(uintptr_t, unsigned, unsigned);
		std::atomic<MotdReadFn> originalMotdRead{ nullptr };
		std::atomic<ContentFn> originalAchievements{ nullptr };
		std::atomic<ContentFrameFn> originalContentFrame{ nullptr };
		std::atomic<MotdFn> originalMotd{ nullptr };
		bool achievementPending = false, motdPending = false;
		uint64_t nextAchievement = 0, nextMotd = 0;
		unsigned observedMotdMode = 0;

		template <class T>
		T native(uintptr_t rva)
		{
			return reinterpret_cast<T>(base + rva);
		}

		template <class T>
		T read(uintptr_t rva)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(base + rva), &value, sizeof(value));
			return value;
		}

		template <class T>
		void write(uintptr_t rva, T value)
		{
			safemem::WriteBytes(reinterpret_cast<void*>(base + rva), &value, sizeof(value));
		}

		unsigned current_motd_mode()
		{
			// Same feature checks and BR dvar as SetupBatchRequest (F6A6F0).
			if (native<bool (*)(unsigned)>(game::kComGameModeSupportsFeatureRVA)(213))
			{
				const auto br = read<uintptr_t>(0xD386778);
				return br && read<uint8_t>(br - base + 40) ? 3 : 1;
			}
			return native<bool (*)(unsigned)>(game::kComGameModeSupportsFeatureRVA)(214) ? 2 : 0;
		}

		bool apply_motd(const Json& data)
		{
			constexpr uintptr_t manager = 0x41CD2B0;
			// The stock frontend allocator owns the request/response and 15 parsed slots.
			// SetupBatchRequest selects slot pointers for the current MP/BR/CP mode.
			if (!motd::Response(data) || !read<uintptr_t>(manager + 72) || !read<uintptr_t>(manager + 216) ||
				!native<bool (*)(uintptr_t)>(0xF6A6F0)(base + manager))
				return false;
			const auto& records = data.at("records");
			const auto count = uint32_t(records.size());
			const auto allocation = count ? native<uintptr_t (*)(size_t)>(0x20EBA30)(7328ull * count) : 0;
			if (count && !allocation) return false;
			for (uint32_t i = 0; i < count; ++i)
			{
				const auto at = allocation + 7328ull * i;
				const auto& record = records[i];
				native<void (*)(uintptr_t, int)>(0x2114BE0)(at, 1);
				auto message = record.at("message");
				message["message_id"] = record.at("id");
				const auto content = message.dump();
				// bdMarketingCommsMessage layout is verified by2114CF0 deserialization.
				const auto offset = at - base;
				write<uint64_t>(offset + 16, std::stoull(record.at("id").get<std::string>()));
				const auto locale = record.at("locale").get<std::string>();
				memcpy(reinterpret_cast<void*>(at + 24), locale.c_str(), locale.size() + 1);
				memcpy(reinterpret_cast<void*>(at + 44), content.c_str(), content.size() + 1);
				write<uint32_t>(offset + 4140, uint32_t(content.size()));
				memcpy(reinterpret_cast<void*>(at + 4144), "{}", 3);
				write<uint32_t>(offset + 6192, 2);
				write<uint64_t>(offset + 7296, data.at("revision").get<uint32_t>());
				write<uint32_t>(offset + 7304, record.at("location").get<uint32_t>());
			}
			const auto response = read<uintptr_t>(manager + 216) - base;
			const auto previous = read<uintptr_t>(response + 16);
			// Same array ownership as native2114EC0: inline records, one bd allocation.
			write<uintptr_t>(response + 16, allocation);
			write<uint32_t>(response + 24, count);
			write<uint32_t>(response + 28, count);
			if (previous) native<void (*)(uintptr_t)>(0x20EBA60)(previous);
			write<uint32_t>(manager + 296, 0);
			motdRevision = data.at("revision").get<uint32_t>();
			LOG_INFO(
				"MOTD", "received host catalog revision=%u messages=%u nativeMode=%u", motdRevision, count, read<uint32_t>(manager + 228));
			return true;
		}

		void initialize_provider_content()
		{
			// OnlineManager::TryInitSystems (1051940) runs on retail sign-in events.
			// A delayed local-provider login can miss that event: these systems stay
			// uninitialized, so the native manager never calls either frame adapter.
			// Repeat its Init -> initialized sequence for ONLY the two adapted systems.
			// +20 is module initialization, not the downloaded-data success flag.
			if (!read<uintptr_t>(0xB4AB990 + 3 * 8) || !read<uintptr_t>(0xB4AB990 + 9 * 8)) return;
			for (const auto [manager, init] :
				{ std::pair<uintptr_t, uintptr_t>{ 0x4596420, 0x107B7D0 }, std::pair<uintptr_t, uintptr_t>{ 0x411F850, 0xF32FB0 } })
			{
				if (read<uint8_t>(manager + 20) || !read<uintptr_t>(manager + 8)) continue;
				if (native<bool (*)(uintptr_t)>(init)(base + manager))
				{
					write<uint8_t>(manager + 20, 1);
					LOG_INFO("FrontendBackend", "initialized provider module=%s after authenticated profile restoration",
						manager == 0x4596420 ? "publisher variables" : "achievement state");
				}
			}
		}

		uintptr_t online_transition()
		{
			// Services entry calls this directly (10394F0), without the invitation HSM.
			// Keep the configured provider's direct transport even while an auth lease
			// refresh is pending after a long load. No access is granted here: both
			// stock service/data fences still require a current authenticated response.
			if (Required() && read<int>(0xE6BDA40) == 2) return 0;
			return originalOnlineTransition.load()();
		}

		bool content_request(bool achievements)
		{
			auto& busy = achievements ? achievementPending : motdPending;
			auto& retry = achievements ? nextAchievement : nextMotd;
			const auto now = GetTickCount64();
			if (busy) return true;
			if (!Authenticated() || now < retry || !read<uintptr_t>(0x11868880)) return false;
			const auto epoch = generation, vm = read<uintptr_t>(0x11868880);
			const std::string kind = achievements ? "achievements" : "motd";
			const auto nonce = kind + ":" + std::to_string(++sequence) + ":" + std::to_string(now);
			Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "online_data" }, { "kind", kind },
				{ "nonce", nonce } };
			if (!achievements)
			{
				request["motd_catalog"] = 1;
				request["locale"] = "en";
				char locale[20]{};
				const auto nativeRequest = read<uintptr_t>(0x41CD2B0 + 208);
				if (nativeRequest && safemem::ReadBytes(reinterpret_cast<void*>(nativeRequest + 32), locale, sizeof(locale)) && locale[0] &&
					memchr(locale, 0, sizeof(locale)))
					request["locale"] = locale;
			}
			busy = social_runtime::SubmitFrontend(request.dump(),
				[achievements, epoch, vm, nonce, now, kind](unsigned status, std::string body)
				{
					(achievements ? achievementPending : motdPending) = false;
					if (epoch != generation || vm != read<uintptr_t>(0x11868880)) return;
					bool accepted = false;
					if (status == 200 && Authenticated() && GetTickCount64() - now < 10000) try
						{
							const auto response = Json::parse(body);
							accepted = frontend_service::OnlineData(response, std::to_string(offlineauth::GetIdentity().xuid), nonce, kind);
							if (accepted && !achievements) accepted = apply_motd(response.at("online_data"));
						}
						catch (...)
						{
						}
					// Both native completion functions consume controller at +48; CRM also
					// consumes game mode at +52. They do not access a BD task in these paths.
					alignas(16) unsigned char task[64]{};
					const int mode =
						achievements ? native<uint8_t (*)()>(game::kComGameModeGetActiveGameModeRVA)() : read<int>(0x41CD2B0 + 228);
					memcpy(task + 52, &mode, sizeof(mode));
					(achievements ? nextAchievement : nextMotd) = GetTickCount64() + (accepted ? (achievements ? 0 : 60000) : 5000);
					if (achievements)
					{
						if (accepted) native<void (*)(uintptr_t)>(0x2104940)(base + 0x411F850 + 72);
						// The first completion requests the second native active/completed
						// page. Only the last native callback marks the manager ready and
						// emits challenges_updated. Never set its success bytes ourselves.
						native<void (*)(void*, int)>(0xF350D0)(task, accepted ? 0 : 1);
					}
					else if (accepted)
					{
						const auto manager = base + 0x41CD2B0;
						native<void (*)(void*, int)>(0xF69AB0)(task, 0);
						// Stock completion clears parsed slots, parses the selected MOTD,
						// emits update_crm_data, and owns native readiness/checksums.
						if (!read<uint8_t>(0xD2B0A00)) native<void (*)(uintptr_t, unsigned, void*, int)>(0xF69BB0)(manager, 0, task, 0);
					}
					LOG_INFO("FrontendBackend", "online data kind=%s http=%u completed=%d", kind.c_str(), status, int(accepted));
				});
			retry = now + 1000;
			if (busy && achievements) write<uint8_t>(0x411F850 + 64, 2); // native request-pending state
			return busy;
		}

		char fetch_achievements(uintptr_t self, unsigned controller, const char* page)
		{
			if (!Required() || self != base + 0x411F850 || controller != 0) return originalAchievements.load()(self, controller, page);
			return content_request(true);
		}

		void content_frame(uintptr_t self, unsigned controller)
		{
			if (!Required() || self != base + 0x411F850 || controller != 0)
			{
				originalContentFrame.load()(self, controller);
				return;
			}
			if (!Authenticated() || !offlineauth::InitializeProfile()) return;
			local_progression::Pump();
			if (local_challenges::XP() < 0) return;
			const auto state = read<uint8_t>(0x411F850 + 64);
			// A VM rebuild discards its old completion; queue pressure can also defer
			// the second page. Retry a pending native request with no outstanding work.
			if (state == 1 || state == 4 || (state == 2 && !achievementPending)) content_request(true);
		}

		char fetch_motd(uintptr_t self, unsigned controller)
		{
			if (!Required() || self != base + 0x41CD2B0 || controller != 0) return originalMotd.load()(self, controller);
			return content_request(false);
		}

		char read_motd(uintptr_t self, unsigned controller, unsigned index)
		{
			if (!Required() || self != base + 0x41CD2B0 || controller != 0) return originalMotdRead.load()(self, controller, index);
			if (!Authenticated()) return 0;
			const auto response = read<uintptr_t>(0x41CD2B0 + 216);
			if (!response || index >= read<uint32_t>(response - base + 28)) return 0;
			const auto at = read<uintptr_t>(response - base + 16) - base + 7328ull * index;
			const auto id = std::to_string(read<uint64_t>(at + 16));
			const auto revision = read<uint64_t>(at + 7296);
			const auto nonce = "crm-read:" + std::to_string(++sequence);
			return social_runtime::SubmitFrontend(Json{ { "version", 1 }, { "build", frontend_service::Build }, { "op", "crm_read" },
													  { "nonce", nonce }, { "message_id", id }, { "revision", revision } }
													  .dump(),
				[id, nonce](unsigned status, std::string body)
				{
					bool accepted = false;
					try
					{
						const auto j = Json::parse(body);
						accepted = status == 200 && frontend_service::Validate(j, std::to_string(offlineauth::GetIdentity().xuid), nonce) &&
							j.at("crm_read").at("message_id") == id && j.at("crm_read").at("recorded") == true;
					}
					catch (...)
					{
					}
					LOG_INFO("MOTD", "read receipt message=%s http=%u recorded=%d", id.c_str(), status, int(accepted));
				});
		}

		bool services_caller(uintptr_t caller)
		{
			return caller >= 0x1039550 && caller < 0x1039940;
		}

		bool complete(int controller, uint64_t mask)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			// Adapt the online-services provider dependency only. Do not announce a
			// retail DW login globally: unrelated modules require their own adapters.
			if (Required() && controller == 0 && mask == 128 && services_caller(caller))
				return Authenticated() && read<int>(0xE6BDA40) == 2;
			return originalComplete.load()(controller, mask);
		}

		bool failed(int controller, uint64_t mask)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
			if (Required() && controller == 0 && mask == 128 && services_caller(caller)) return lastStatus == 403;
			return originalFailed.load()(controller, mask);
		}

		int platform(uintptr_t self, int group)
		{
			// Groups1/2 are account and notification prerequisites. The local provider
			// has an authenticated account + a live social-sync subscription, not IPC.
			if (Required() && self == base + 0x4628AA0 && (group == 1 || group == 2))
				return Authenticated() ? 1 : lastStatus == 403 ? 2 : 0;
			return originalPlatform.load()(self, group);
		}

		bool privileges(int controller, unsigned* error)
		{
			if (Required() && controller == 0)
			{
				if (error) *error = Authenticated() ? 0 : lastStatus == 403 ? 2 : 4;
				return Authenticated();
			}
			return originalPrivileges.load()(controller, error);
		}

		unsigned char patch(uintptr_t self, int category)
		{
			if (Required() && self == base + 0x41E3A70 && category >= 0 && category < 2) return Authenticated() && patchReady ? 1 : 0;
			return originalPatch.load()(self, category);
		}

		uint16_t patch_manifest_version(uintptr_t self, int category)
		{
			// GetProtocolVersion (13E35D0) combines this value with native revision 978.
			// The frozen title provider validates an empty patch manifest. Use Replay
			// initialization/reset sentinel 7FFF, as the dedicated server does. Retail
			// cache verification failures set 7FFD/7FFE even though no patch is applied.
			// Keep the validated manifest stable across authentication lease refreshes.
			if (Required() && patchReady && self == base + 0x41E3A70 && category >= 0 && category < 2) return 0x7FFF;
			return original_patch_version.load()(self, category);
		}

		bool patch_data(uintptr_t self, int category)
		{
			if (Required() && self == base + 0x41E3A70 && category >= 0 && category < 2) return Authenticated() && patchReady;
			return originalPatchData.load()(self, category);
		}

		bool commerce(uintptr_t self, int controller)
		{
			if (Required() && self == base + 0x59E05C0 && controller == 0)
				return Authenticated() && local_challenges::CommerceStatus().ready;
			return originalCommerce.load()(self, controller);
		}

		bool qos()
		{
			// The local provider selects its direct endpoint. No retail datacenter or
			// relay allocation is advertised. A request/response must have completed.
			if (Required()) return !Authenticated();
			return originalQos.load()();
		}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_save_accounts.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_load_motd_catalog.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_load_motd_reads.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_save_motd_read.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_load_accounts.inl"
#endif
		void finish(unsigned, std::string, uint64_t, std::string, uint64_t);

		bool start_login(uintptr_t vm, int fields)
		{
			// The stock UpgradeFromExisting copies an absent DW login result (null +
			// 0x1B90) before sending anything. Intercept at the request boundary, before
			// it copies/logs credentials. Text is deliberately not read or transmitted:
			// the backend authenticates the saved device key and its current lease.
			if (native<int (*)(uintptr_t)>(0x20838B0)(vm) != fields || native<int (*)(uintptr_t, int)>(0x2084AC0)(vm, 1) != 3 ||
				native<int (*)(uintptr_t, int)>(0x19CE9B0)(vm, 1) != 0)
				return false;
			for (int i = 2; i <= fields; ++i)
				if (native<int (*)(uintptr_t, int)>(0x2084AC0)(vm, i) != 4) return false;
			if (loginPending) return true;
			const auto now = GetTickCount64(), epoch = generation, serial = ++loginSerial;
			const auto nonce = "login:" + std::to_string(serial) + ":" + std::to_string(now);
			Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "local_login" }, { "nonce", nonce } };
			loginVm = vm;
			loginPending = true;
			loginDeadline = now + 10000;
			loginResultReady = false;
			if (!social_runtime::SubmitFrontend(request.dump(),
					[epoch, serial, nonce, now](unsigned status, std::string body)
					{
						if (epoch != generation || serial != loginSerial || !loginPending) return;
						finish(status, body, epoch, nonce, now);
						loginSucceeded = Authenticated() && accountLinked;
						loginResultReady = true;
					}))
			{
				loginSucceeded = false;
				loginResultReady = true;
			}
			LOG_INFO("FrontendBackend", "local account login queued; text fields omitted, saved identity retained");
			return true;
		}

		int upgrade_login(uintptr_t vm)
		{
			if (!Required()) return originalUpgrade.load()(vm);
			native<void (*)(uintptr_t, int)>(0x2083E80)(vm, start_login(vm, 3));
			return 1;
		}

		int existing_login(uintptr_t vm)
		{
			if (!Required()) return originalLogin.load()(vm);
			start_login(vm, 4);
			return 0;
		}

		int cancel_login(uintptr_t vm)
		{
			if (!Required()) return originalCancel.load()(vm);
			if (loginPending)
			{
				++loginSerial;
				loginPending = false;
				loginResultReady = false;
			}
			return 0;
		}

		int has_account(uintptr_t vm)
		{
			if (!Required()) return originalHasAccount.load()(vm);
			native<void (*)(uintptr_t, int)>(0x2083E80)(vm, Authenticated() && accountLinked);
			return 1;
		}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_load_policy.inl"
#endif
		void finish(unsigned status, std::string body, uint64_t epoch, std::string nonce, uint64_t began)
		{
			if (epoch != generation) return;
			if (!nonce.starts_with("login:")) pending = false;
			lastStatus = status;
			bool accepted = false;
			if (status == 200) try
				{
					const auto response = Json::parse(body);
					const auto identity = offlineauth::GetIdentity();
					if (frontend_service::Validate(response, std::to_string(identity.xuid), nonce) && GetTickCount64() - began < 10000)
					{
						const auto utc = response.at("server_time").get<uint32_t>();
						// Same clock-cache fields written by the native server-time task
						// completion12A0950. Value comes from the dedicated system clock.
						write<uint32_t>(0xD3278E0, utc);
						write<uint32_t>(0xD385948, utc - uint32_t(std::time(nullptr)));
						write<uint32_t>(0xD38594C, native<unsigned (*)()>(0x148FCE0)());
						write<uint8_t>(0xD327D3B, 1);
						// Validation above includes the supported build and patch manifest.
						// Wallet/pending-transaction recovery gates native exchange below.
						if (!patchReady)
							LOG_INFO("FrontendBackend", "validated empty patch manifest; native party net code=2147419090 (manifest=32767 revision=978)");
						patchReady = true;
						premium = response.at("account").at("multiplayer").get<bool>();
						accountLinked = response.at("account").at("linked").get<bool>();
						playlistHash = response.at("playlists_sha256").get<std::string>();
						until = began + 15000;
						accepted = true;
					}
				}
				catch (...)
				{
				}
			if (!accepted)
			{
				until = 0;
				premium = false;
				accountLinked = false;
				write<int>(0x59D5F88, 0);
			}
			const auto now = GetTickCount64();
			nextFetch = now + (accepted ? 5000 : 2000);
			static bool wasAccepted = false;
			static unsigned logged = ~0u;
			if (accepted != wasAccepted || status != logged || (!accepted && now >= nextLog))
			{
				LOG_INFO("FrontendBackend",
					"authenticated title response http=%u accepted=%d rtt=%llums; native boot remains gated by service data", status,
					int(accepted), now - began);
				nextLog = now + 30000;
				wasAccepted = accepted;
				logged = status;
			}
		}
	}

	bool Required()
	{
		return required.load(std::memory_order_acquire);
	}

	bool Authenticated()
	{
		const auto expiry = until.load(std::memory_order_acquire);
		return Required() && expiry && GetTickCount64() < expiry;
	}

	bool Premium()
	{
		return Authenticated() && premium.load();
	}

	unsigned DataRequirements(int controller, unsigned flags, uintptr_t caller)
	{
		if (!Required() || controller != 0) return flags;
		// The time/patch/commerce predicates consume provider caches above. Native
		// stats, profile, inventory and publisher failures are not masked away.
		if (!Authenticated()) return flags | 0x10u;
		if (caller >= 0x1039C50 && caller < 0x1039E40)
		{
			static unsigned lastFlags = ~0u;
			if (flags != lastFlags)
			{
				LOG_INFO("FrontendBackend", "native online-data pending mask=%X", flags);
				lastFlags = flags;
			}
		}
		if (caller >= 0x1039C50 && caller < 0x1039E40 && native<uint8_t (*)()>(game::kComGameModeGetActiveGameModeRVA)() == 2 &&
			(!playlists::Ready() || !playlist_backend::Current(playlistHash)))
			flags |= 0x20u;
		return flags;
	}

	void Configure(bool dedicated, const std::wstring& origin)
	{
		std::lock_guard lock(configLock);
		configuredOrigin = origin;
		required = !dedicated && !origin.empty();
	}

	std::wstring Origin()
	{
		std::lock_guard lock(configLock);
		return configuredOrigin;
	}

	void Pump()
	{
		if (!installed || server || !Required()) return;
		// Profile restoration and native completion events can reenter BeginAuth.
		static bool pumping = false;
		if (pumping) return;
		pumping = true;

		struct Guard
		{
			bool& flag;

			~Guard()
			{
				flag = false;
			}
		} guard{ pumping };

		std::wstring origin;
		{
			std::lock_guard lock(configLock);
			origin = configuredOrigin;
		}
		if (origin != activeOrigin)
		{
			activeOrigin = origin;
			++generation;
			until = 0;
			pending = false;
			nextFetch = 0;
			playlistHash.clear();
			store_backend::Reset();
			local_challenges::Reset();
			custom_mod_backend::Reset();
			name_validation::Reset();
			patchReady = false;
		}
		social_runtime::Bootstrap(origin);
		social_runtime::PumpFrontend();
		const auto now = GetTickCount64();
		if (loginPending && (loginResultReady || now >= loginDeadline))
		{
			const bool success = loginResultReady && loginSucceeded && Authenticated() && read<uintptr_t>(0x11868880) == loginVm;
			loginPending = false;
			loginResultReady = false;
			++loginSerial;
			if (read<uintptr_t>(0x11868880) == loginVm)
			{
				if (success)
				{
					commands::Queue(base, "profile_SetHasEverSeen_CODAccount 0 1\nupdategamerprofile 0\n");
					native<char (*)(uintptr_t, int, const char*)>(0xF2EE80)(base + 0x411F2C0, 0, "AccountRegistrationSuccess");
				}
				else
					native<char (*)(uintptr_t, int, const char*, int, unsigned)>(0xF2ED80)(
						base + 0x411F2C0, 0, "AccountRegistrationFailure", 3, 0);
			}
			LOG_INFO("FrontendBackend", "local account login completed success=%d; native account event dispatched", int(success));
		}
		const auto wallet = local_challenges::CommerceStatus();
		write<int>(0x59D5F88, Authenticated() && wallet.ready && !wallet.pending && wallet.status != "storage_error" ? 2 : 0);
		if (!pending && now >= nextFetch)
		{
			const auto epoch = generation;
			const auto nonce = std::to_string(epoch) + ":" + std::to_string(++sequence) + ":" + std::to_string(now);
			Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "bootstrap" }, { "nonce", nonce } };
			pending = social_runtime::SubmitFrontend(request.dump(),
				[epoch, nonce, now](unsigned status, std::string body)
				{
					finish(status, std::move(body), epoch, nonce, now);
				});
			nextFetch = now + 2000;
		}
		if (Authenticated())
		{
			tournament_backend::Pump();
			store_backend::Pump();
			custom_mod_backend::Pump();
			if (offlineauth::InitializeProfile())
			{
				initialize_provider_content();
				// Keep server profile and pending purchase recovery moving even
				// before a game mode is selected or an achievement page is active.
				// Profile replication must progress before consumers that wait on
				// RemoteReady. Boot fences do not draw the client console overlay.
				local_stats::Pump();
				local_progression::Pump();
				local_playercard::Pump();
				local_settings::Pump();
			}
			playlists::Pump();
			const auto mode = current_motd_mode();
			if (mode != observedMotdMode)
			{
				observedMotdMode = mode;
				nextMotd = 0;
			}
			if (native<bool (*)()>(0x10C67A0)() && read<uintptr_t>(0x41CD2B0 + 72) && mode && now >= nextMotd) content_request(false);
		}
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_request.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/frontend_backend_server_playlist_allowed.inl"
#endif
	void Shutdown()
	{
		required = false;
		until = 0;
		pending = false;
		++generation;
#ifndef IW8X_CLIENT_ONLY
		if (server)
		{
			progression_backend::Stop();
			tournament_backend::Stop();
		}
#endif

	}

	bool HandleCommand(std::string_view command)
	{
#ifndef IW8X_CLIENT_ONLY
		if (server && backend_agent::Enabled() && backend_agent::Command(command)) return true;
#endif
#ifndef IW8X_CLIENT_ONLY
		if (progression_backend::Command(command, server)) return true;
#endif
		if (custom_mod_backend::Command(command, server)) return true;
#ifndef IW8X_CLIENT_ONLY
		if (name_validation::Command(command, server)) return true;
#endif
		if (store_backend::Command(command, server)) return true;
#ifndef IW8X_CLIENT_ONLY
		if (command == "motd_reload")
		{
			if (!server)
				OP_WARN("motd_reload is a dedicated-server command");
			else
				OP_INFO("MOTD catalog reload: %s",
					LoadMotdCatalog() ? "accepted" : "invalid/non-increasing revision; previous catalog retained");
			return true;
		}
#endif

		if (command == "motd_refresh")
		{
			if (!server) nextMotd = 0;
			return true;
		}
		if (command == "motd_status")
		{
#ifndef IW8X_CLIENT_ONLY
			if (server)
			{
				std::lock_guard lock(serviceLock);
				OP_INFO("MOTD catalog ready=%d revision=%u messages=%zu readStorage=%d", int(motdCatalogReady),
					motdCatalog.at("revision").get<uint32_t>(), motdCatalog.at("messages").size(), int(motdReadsReady));
			}
			else
#endif

				OP_INFO("MOTD revision=%u pending=%d nativeReady=%u mode=%u", motdRevision, int(motdPending), read<uint8_t>(0xD2B0A00),
					read<uint32_t>(0x41CD2B0 + 228));
			return true;
		}
#ifndef IW8X_CLIENT_ONLY
		if (command == "frontend_backend_reload")
		{
			if (!server)
				OP_WARN("frontend_backend_reload is a dedicated-server command");
			else
			{
				const auto ok = LoadPolicy();
				OP_INFO("Frontend backend policy reload: %s", ok ? "accepted" : "invalid; previous policy retained");
			}
			return true;
		}
#endif

		if (command == "frontend_backend_status")
		{
#ifndef IW8X_CLIENT_ONLY
			if (server)
			{
				std::lock_guard lock(serviceLock);
				OP_INFO("Frontend backend ready=%d maintenance=%d", int(serviceReady), int(policy.value("maintenance", false)));
			}
			else
#endif

				OP_INFO("Frontend backend required=%d authenticated=%d http=%u pending=%d services=%u dataMP=%u patch=%u exchange=%d",
					int(Required()), int(Authenticated()), lastStatus, int(pending), read<uint8_t>(0xC26A6F0), read<uint8_t>(0xC26A718),
					read<uint8_t>(0xC26A730), read<int>(0x59D5F88));
			if (!server)
				OP_INFO("Frontend content achievements state=%u page=%u ready=%u pending=%d localXP=%d motdReady=%u motdPending=%d VM=%d",
					read<uint8_t>(0x411F850 + 64), read<uint8_t>(0x411F850 + 65), read<uint8_t>(0x411F850 + 66), int(achievementPending),
					local_challenges::XP(), read<uint8_t>(0xD2B0A00), int(motdPending), int(read<uintptr_t>(0x11868880) != 0));
			if (!server)
				OP_INFO("Frontend modules publisherInitialized=%u achievementsInitialized=%u", read<uint8_t>(0x4596420 + 20),
					read<uint8_t>(0x411F850 + 20));
			return true;
		}
		return false;
	}

	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (installed) return hook::Status::Installed;
		base = image;
		server = dedicated;
#ifndef IW8X_CLIENT_ONLY
		if (server && backend_agent::Enabled())
		{
			installed = true;
			return hook::Status::Installed;
		}
#endif

#ifndef IW8X_CLIENT_ONLY
		if (server)
		{
			serviceReady = LoadPolicy();
			accountStoreReady = LoadAccounts();
			motdCatalogReady = LoadMotdCatalog();
			motdReadsReady = LoadMotdReads();
			store_backend::Load();
			name_validation::Load();
			if (!progression_backend::Start() || !custom_mod_backend::Start() || !tournament_backend::Start()) return hook::Status::Failed;
			installed = true;
			return hook::Status::Installed;
		}
#endif

		for (const auto& s : kFrontendBindings)
		{
			unsigned char bytes[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + s.rva), bytes, 16) || memcmp(bytes, s.bytes, 16))
			{
				LOG_ERR("FrontendBackend", "native binding mismatch RVA=%llx", s.rva);
				return hook::Status::NotReady;
			}
		}
		auto signature = [](uintptr_t rva)
		{
			for (const auto& s : kFrontendBindings)
				if (s.rva == rva) return s.bytes;
			return static_cast<const unsigned char*>(nullptr);
		};
		hook::Batch batch;
#define FRONTEND_HOOK(rva,fn,original) batch.Add(reinterpret_cast<void*>(base+rva),&fn,signature(rva),16,original)
		FRONTEND_HOOK(0xF19830, complete, originalComplete);
		FRONTEND_HOOK(0x12BD8F0, online_transition, originalOnlineTransition);
		FRONTEND_HOOK(0xF19890, failed, originalFailed);
		FRONTEND_HOOK(0x1670370, platform, originalPlatform);
		FRONTEND_HOOK(0x13FCE00, privileges, originalPrivileges);
		FRONTEND_HOOK(0x1053520, patch, originalPatch);
		FRONTEND_HOOK(0x1053480, patch_manifest_version, original_patch_version);
		FRONTEND_HOOK(0x10527B0, patch_data, originalPatchData);
		FRONTEND_HOOK(0x1B3C350, commerce, originalCommerce);
		FRONTEND_HOOK(0x15048E0, qos, originalQos);
		FRONTEND_HOOK(0xF2A1C0, upgrade_login, originalUpgrade);
		FRONTEND_HOOK(0xF29B80, existing_login, originalLogin);
		FRONTEND_HOOK(0xF29D70, cancel_login, originalCancel);
		FRONTEND_HOOK(0xF2A3C0, has_account, originalHasAccount);
		FRONTEND_HOOK(0xF33150, content_frame, originalContentFrame);
		FRONTEND_HOOK(0xF33B70, fetch_achievements, originalAchievements);
		FRONTEND_HOOK(0xF68B70, fetch_motd, originalMotd);
		FRONTEND_HOOK(0xF69740, read_motd, originalMotdRead);
#undef FRONTEND_HOOK
		const auto status = batch.Commit();
		if (status != hook::Status::Installed) return status;
		installed = true;
		LOG_INFO("FrontendBackend", "local provider adapters installed; stock Lua/native frontend fence state machines retained");
		return hook::Status::Installed;
	}
}
