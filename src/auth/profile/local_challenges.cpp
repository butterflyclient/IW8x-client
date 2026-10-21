#include "auth/profile/local_challenges.h"
#include "auth/profile/challenge_engine.h"

#include "common/embedded_assets.h"

#include "auth/offline_auth.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"

#include "common/console.h"

#include "network/commerce/commerce_account.h"
#include "network/profile/token_account.h"
#include "network/profile/after_action.h"
#include "network/commerce/unlock_policy.h"
#include "auth/dvar_patches.h"
#include <bcrypt.h>

#include <filesystem>
#include <fstream>
#include <deque>
namespace local_challenges
{
	namespace
	{
		using challenges::Json;
		challenges::Engine view;
		Json weapons = Json::object(), battle = Json::object();
		Json wallet = commerce::Empty(), packs = Json::array(), trade = Json::object();
		Json tokens = token_account::Empty();
		Json reports = after_action::Empty();
		bool tokensReady = false;
		bool journalLoaded = false, journalError = false;
		std::filesystem::path journalPath;
		std::string tradeStatus, tradeProduct;
		bool ready = false, seeded = false, pending = false, needImport = false, invalidSeed = false;
		bool unlockAll = false;
		std::string unlockStatus = "connecting";
		std::string seed, upload;
		size_t offset = 0;
		uint64_t revision = 0, epoch = 0, serial = 0, next = 0, received = 0;
		uint32_t serverUTC = 0;
		std::deque<Json> actions;
		std::set<uint64_t> acknowledgements;

		bool Read(const std::filesystem::path& path, challenges::Engine& engine, uint64_t id)
		{
			try
			{
				if (std::filesystem::file_size(path) > 4 * 1024 * 1024) return false;
				std::ifstream f(path, std::ios::binary);
				Json wrapper;
				f >> wrapper;
				const auto payload = wrapper.at("payload").get<std::string>();
				return wrapper.at("checksum") == std::to_string(challenges::Hash(payload)) && engine.Restore(Json::parse(payload), id);
			}
			catch (...)
			{
				return false;
			}
		}

		bool WriteTrade(const Json& value)
		{
			const auto bytes = value.dump();
			const auto temporary = journalPath.wstring() + L".tmp";
			HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool ok =
				WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(f);
			CloseHandle(f);
			return ok && MoveFileExW(temporary.c_str(), journalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}

		void LoadTrade()
		{
			if (journalLoaded) return;
			journalLoaded = true;
			try
			{
				wchar_t exe[MAX_PATH]{};
				if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) throw std::runtime_error("exe path");
				const auto origin = frontend_backend::Origin();
				const auto hash =
					challenges::Hash(std::string_view(reinterpret_cast<const char*>(origin.data()), origin.size() * sizeof(wchar_t)));
				journalPath = std::filesystem::path(exe).parent_path() / L"players" /
					(L"commerce_pending_" + std::to_wstring(offlineauth::GetIdentity().xuid) + L"_" + std::to_wstring(hash) + L".json");
				std::filesystem::create_directories(journalPath.parent_path());
				if (std::filesystem::exists(journalPath))
				{
					if (std::filesystem::file_size(journalPath) > 4096) throw std::runtime_error("journal size");
					std::ifstream f(journalPath);
					Json saved;
					f >> saved;
					if (!saved.is_object()) throw std::runtime_error("journal object");
					if (!saved.empty() &&
						(!commerce::Token(saved.at("transaction").get<std::string>()) ||
							!((saved.at("action") == "commerce" && (saved.at("kind") == "bundle" || saved.at("kind") == "currency")) ||
								(saved.at("action") == "token" &&
									(saved.at("kind") == "token" || saved.at("kind") == "trial" || saved.at("kind") == "supply"))) ||
							!saved.at("sequence").is_number_unsigned() || !saved.at("product").is_string() ||
							saved.at("product").get_ref<const std::string&>().size() > 63))
						throw std::runtime_error("journal fields");
					trade = std::move(saved);
				}
				if (!trade.empty())
				{
					tradeProduct = trade.at("product");
					tradeStatus = "pending";
					LOG_INFO("Commerce", "resuming saved purchase request");
				}
			}
			catch (...)
			{
				journalError = true;
				tradeStatus = "storage_error";
				LOG_ERR("Commerce", "pending purchase file unreadable; retained for recovery");
			}
		}
	}

	void Reset()
	{
		++epoch;
		ready = pending = needImport = tokensReady = false;
		revision = next = received = 0;
		actions.clear();
		acknowledgements.clear();
		offset = 0;
		unlockAll = false;
		unlockStatus = "connecting";
		reports = after_action::Empty();
		journalLoaded = journalError = false;
		trade = Json::object();
		tradeStatus.clear();
		tradeProduct.clear();
		packs = Json::array();
	}

	bool Initialize(int initialXP, const std::map<std::string, int>& legacyWeapons)
	{
		if (seeded) return true;
		const auto id = offlineauth::GetIdentity().xuid;
		if (!id) return false;
		challenges::Engine old;
		const auto catalog = embedded_assets::Find("data/challenge_catalog.json");
		if (!old.Load(catalog) || !view.Load(catalog)) return false;
		old.Initialize(id, initialXP);
		wchar_t exe[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
		const auto path = std::filesystem::path(exe).parent_path() / L"players" / (L"progression_" + std::to_wstring(id) + L".json");
		std::error_code ec;
		if ((std::filesystem::exists(path, ec) || std::filesystem::exists(path.wstring() + L".bak", ec)) && !Read(path, old, id) &&
			!Read(path.wstring() + L".bak", old, id))
		{
			// Existing server accounts can still load. Never substitute a zeroed
			// migration seed for damaged files; retain every original local file.
			invalidSeed = true;
			LOG_ERR("ServerProgression", "legacy progress unreadable; import disabled, existing server account can still load");
		}
		if (initialXP > old.XP()) old.AddXP(initialXP - old.XP());
		if (!invalidSeed)
			seed = Json{ { "xuid", std::to_string(id) }, { "weapons", legacyWeapons }, { "challenges", old.Snapshot() } }.dump();
		upload = std::to_string(id) + ":" + std::to_string(challenges::Hash(seed));
		seeded = true;
		LOG_INFO("ServerProgression", "requesting server snapshot; legacy seed preserved (%zu bytes)", seed.size());
		return true;
	}

	void Receive(const challenges::Event&)
	{ /* Client reports cannot award progression. */
	}

	void ProjectUnlock()
	{
		// Run on the native pump, including expired-lease frames. Do not leave
		// native unlock Dvars enabled after authenticated policy becomes invalid.
		static bool projectedUnlock = false;
		const bool desiredUnlock = UnlockEnabled();
		if (projectedUnlock != desiredUnlock)
		{
			dvars::SetUnlockAll(desiredUnlock);
			projectedUnlock = desiredUnlock;
		}
	}

	void Pump(const std::function<int(const std::string&)>&)
	{
		ProjectUnlock();
		if (!seeded || pending || GetTickCount64() < next || !frontend_backend::Authenticated()) return;
		Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "progression" }, { "action", "sync" } };
		bool action = false;
		std::string tradeID;
		if (needImport)
		{
			if (invalidSeed) return;
			request["action"] = "import";
			request["upload"] = upload;
			request["offset"] = offset;
			request["total"] = seed.size();
			request["chunk"] = seed.substr(offset, 2048);
		}
		else if (ready && !journalError && !trade.empty())
		{
			request.update(trade);
			tradeID = trade.at("transaction");
		}
		else if (!actions.empty())
		{
			request.update(actions.front());
			action = true;
		}
		const auto nonce = "progression:" + std::to_string(++serial);
		request["nonce"] = nonce;
		const auto generation = epoch;
		pending = true;
		next = GetTickCount64() + 2000;
		if (!social_runtime::SubmitFrontend(request.dump(),
				[generation, nonce, action, tradeID](unsigned status, std::string bytes)
				{
					if (generation != epoch) return;
					pending = false;
					bool accepted = false;
					try
					{
						const auto reply = Json::parse(bytes);
						if ((status == 200 || ((!tradeID.empty() || action) && status == 409)) && frontend_backend::Authenticated() &&
							frontend_service::Validate(reply, std::to_string(offlineauth::GetIdentity().xuid), nonce))
						{
							const auto& data = reply.at("progression");
							needImport = data.at("needs_import").get<bool>();
							if (needImport)
							{
								offset = data.at("offset").get<size_t>();
								accepted = offset < seed.size();
							}
							else
							{
								const auto& a = data.at("account");
								const auto rev = a.at("revision").get<uint64_t>();
								if (!a.value("unlock_all", Json(false)).is_boolean()) throw std::runtime_error("invalid unlock policy");
								if (a.contains("tokens") && !token_account::Valid(a.at("tokens")))
									throw std::runtime_error("invalid token snapshot");
								const auto reportState = a.value("after_action", after_action::Empty());
								if (!after_action::Valid(reportState)) throw std::runtime_error("invalid report snapshot");
								if (rev >= revision && a.at("xuid") == std::to_string(offlineauth::GetIdentity().xuid) &&
									a.at("weapons").is_object() && view.Restore(a.at("challenges"), offlineauth::GetIdentity().xuid))
								{
									const bool changed = rev != revision;
									weapons = a.at("weapons");
									battle = a.at("battlepass");
									wallet = a.value("commerce", commerce::Empty());
									tokens = a.value("tokens", token_account::Empty());
									tokensReady = a.contains("tokens");
									unlockAll = a.value("unlock_all", false);
									reports = reportState;
									packs = reply.value("currency_packs", Json::array());
									revision = rev;
									ready = true;
									accepted = true;
									LoadTrade();
									std::set<uint64_t> outstanding;
									if (a.at("challenges").contains("notifications"))
										for (const auto& n : a.at("challenges").at("notifications").at("pending"))
											outstanding.insert(n.at("sequence").get<uint64_t>());
									std::erase_if(acknowledgements,
										[&](uint64_t seq)
										{
											return !outstanding.contains(seq);
										});
									if (changed)
										LOG_INFO("ServerProgression", "committed revision=%llu XP=%d weapons=%zu BPXP=%d premium=%d",
											revision, view.XP(), weapons.size(), battle.value("xp", 0),
											int(battle.value("premium", false)));
								}
							}
							if (accepted)
							{
								serverUTC = data.at("utc").get<uint32_t>();
								received = GetTickCount64();
								if (action && !actions.empty())
								{
									if (actions.front().value("action", std::string{}) == "unlock")
										unlockStatus = status == 409 ? "conflict" : "saved";
									actions.pop_front();
								}
								if (!tradeID.empty() && reply.contains("commerce_result") &&
									reply.at("commerce_result").at("id") == tradeID)
								{
									if (WriteTrade(Json::object()))
									{
										tradeStatus = reply.at("commerce_result").at("status").get<std::string>();
										trade = Json::object();
										LOG_INFO("Commerce", "purchase result=%s product=%s balance=%d", tradeStatus.c_str(),
											tradeProduct.c_str(), BattleCP());
									}
									else
										LOG_WARN("Commerce", "purchase committed; awaiting local receipt save before next purchase");
								}
							}
						}
					}
					catch (...)
					{
						accepted = false;
					}
					if (action && status == 422 && !actions.empty()) actions.pop_front();
					if (needImport && status == 409) offset = 0; // host restarted during a chunked import
					next = GetTickCount64() + (accepted ? (needImport || !actions.empty() || !trade.empty() ? 0 : 1000) : 5000);
					if (!accepted) LOG_WARN("ServerProgression", "snapshot pending http=%u; client awards disabled", status);
				}))
			pending = false;
	}

	int XP()
	{
		return ready ? view.XP() : -1;
	}

	std::vector<std::string> AfterAction(const std::string& op, const std::string& arg)
	{
		if (!ready || !frontend_backend::Authenticated()) return {};
		if (op == "refresh")
		{
			next = 0;
			return {};
		}
		const auto report = after_action::Latest(reports);
		if (report.is_null()) return {};
		if (op == "state")
			return { report.at("id"), report.at("status"), reports.at("viewed") == report.at("id") ? "1" : "0",
				std::to_string(report.at("mode").get<int>()) };
		if (op == "ack")
		{
			if (arg != report.at("id").get<std::string>() || report.at("status") == "playing" || actions.size() >= 32) return {};
			for (const auto& action : actions)
				if (action.value("action", std::string{}) == "aar_ack" && action.value("id", std::string{}) == arg) return { "1" };
			actions.push_back({ { "action", "aar_ack" }, { "id", arg } });
			next = 0;
			return { "1" };
		}
		if (op == "value" && report.at("info").contains(arg))
		{
			int value = report.at("info").at(arg);
			// TU19's AAR sums military and season deltas. Account XP is cumulative.
			if (arg == "StartXP") value -= report.at("info").at("StartSeasonXP").get<int>();
			if (arg == "CurrentXP") value -= report.at("info").at("CurrentSeasonXP").get<int>();
			return { std::to_string(value) };
		}
		const auto index = challenges::Number(arg);
		if (index < 1 || index > 4096) return {};
		if (op == "unlock" && size_t(index) <= report.at("unlocks").size())
			return { std::to_string(report.at("unlocks").at(index - 1).get<int>()) };
		if (op == "challenge" && size_t(index) <= report.at("challenges").size())
		{
			const auto& item = report.at("challenges").at(index - 1);
			return { item.at("id"), item.at("kind"), std::to_string(item.at("count").get<int>()) };
		}
		return {};
	}

	int WeaponXP(const std::string& weapon)
	{
		return ready ? weapons.value(weapon, 0) : -1;
	}

	uint32_t UTC()
	{
		return received ? serverUTC + uint32_t((GetTickCount64() - received) / 1000) : 0;
	}

	int BattleXP()
	{
		return ready ? battle.value("xp", 0) : 0;
	}

	bool Premium()
	{
		return ready && battle.value("premium", false);
	}

	int BattleCP()
	{
		return ready ? int(battle.value("cp", int64_t{ 0 }) + wallet.value("credits", int64_t{ 0 }) - wallet.value("spent", int64_t{ 0 }))
			: 0;
	}

	CommerceView CommerceStatus()
	{
		return { ready && !packs.empty() && frontend_backend::Authenticated(), !trade.empty(), BattleCP(), tradeStatus, tradeProduct };
	}

	std::pair<std::string, int> CurrencyPack(int index)
	{
		if (!ready || index < 0 || size_t(index) >= packs.size()) return {};
		return { packs.at(index).at("id").get<std::string>(), packs.at(index).at("amount").get<int>() };
	}

	bool Purchase(const std::string& kind, const std::string& product, int quotedPrice)
	{
		if (!CommerceStatus().ready || journalError || !trade.empty() || product.empty() || product.size() > 63 ||
			(kind != "currency" && kind != "bundle" && kind != "token" && kind != "trial" && kind != "supply"))
			return false;
		if (kind == "token")
		{
			try
			{
				if (!tokensReady || !xp_tokens::Find(std::stoi(product))) return false;
			}
			catch (...)
			{
				return false;
			}
		}
		if (kind == "trial" && (!tokensReady || product != "200005")) return false;
		if (kind == "supply" && (!tokensReady || product != "trialtickets5")) return false;
		unsigned char random[16]{};
		if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
		static const char hex[] = "0123456789abcdef";
		std::string id;
		for (auto c : random)
		{
			id += hex[c >> 4];
			id += hex[c & 15];
		}
		const bool consumable = kind == "token" || kind == "trial" || kind == "supply";
		Json candidate = { { "action", consumable ? "token" : "commerce" }, { "transaction", id },
			{ "sequence", consumable ? tokens.at("next") : wallet.at("next") }, { "kind", kind }, { "product", product },
			{ "quoted_price", quotedPrice } };
		if (!WriteTrade(candidate))
		{
			tradeStatus = "storage_error";
			return false;
		}
		trade = std::move(candidate);
		tradeStatus = "pending";
		tradeProduct = product;
		next = 0;
		return true;
	}

	int TokenQuantity(int item)
	{
		return ready && tokensReady && xp_tokens::Counted(item)
			? int(std::clamp<int64_t>(int64_t(view.TokenGrants(item)) +
					  (item == xp_tokens::TrialTicket ? tokens.value("trial_grants", 0) : 0) -
					  tokens.at("used").value(std::to_string(item), 0),
				  0, INT_MAX))
			: 0;
	}

	uint32_t TokenExpiry(const char* kind)
	{
		return ready && tokensReady && kind ? token_account::Expiry(tokens, kind) : 0;
	}

	bool Owns(int item)
	{
		return unlock_policy::Grants(ready, frontend_backend::Authenticated(), unlockAll, item) || (ready && view.Owns(item));
	}

	bool UnlockEnabled()
	{
		return ready && frontend_backend::Authenticated() && unlockAll;
	}

	UnlockView UnlockStatus()
	{
		const bool connected = frontend_backend::Authenticated();
		bool saving = false;
		for (const auto& action : actions)
			if (action.value("action", std::string{}) == "unlock") saving = true;
		return { ready && connected, saving, ready && connected && unlockAll, revision,
			!connected ? "disconnected"
				: !ready ? "connecting"
				: saving ? "pending"
				: unlockStatus == "connecting" ? "current"
				: unlockStatus };
	}

	bool SetUnlock(bool enabled)
	{
		if (!ready || !frontend_backend::Authenticated() || actions.size() >= 64) return false;
		for (const auto& action : actions)
			if (action.value("action", std::string{}) == "unlock") return false;
		actions.push_back({ { "action", "unlock" }, { "enabled", enabled }, { "revision", revision } });
		unlockStatus = "pending";
		next = 0;
		return true;
	}

	int Progress(int id)
	{
		return ready && view.Knows(id) ? view.Progress(id) : -1;
	}

	std::string Assignments(const char* kind)
	{
		std::string result;
		if (ready && kind)
			for (int id : view.Assignments(kind))
				result += std::to_string(id) + ":" + std::to_string(view.Progress(id)) + ",";
		return result;
	}

	int Mission(int category)
	{
		return ready ? view.Mission(category) : 0;
	}

	bool Activate(int id)
	{
		if (!ready || actions.size() >= 64) return false;
		actions.push_back({ { "action", "activate" }, { "mission", id } });
		next = 0;
		return true;
	}

	void ObserveRank(const std::string&, const std::string&, int, const std::string&, const std::map<int, std::vector<int>>&)
	{ /* Server creates notices. */
	}

	progression::Notice NextNotice(bool challenge)
	{
		if (!ready) return {};
		auto copy = view.Snapshot();
		progression::Notices n(copy["notifications"]);
		for (auto seq : acknowledgements)
			n.Ack(seq);
		return n.Peek(challenge);
	}

	bool AckNotice(uint64_t sequence)
	{
		if (!ready || !sequence || actions.size() >= 64) return false;
		if (acknowledgements.insert(sequence).second) actions.push_back({ { "action", "ack" }, { "sequence", sequence } });
		next = 0;
		return true;
	}
}
