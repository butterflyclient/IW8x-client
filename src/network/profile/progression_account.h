#pragma once
#include "auth/profile/challenge_engine.h"
#include "auth/profile/progression_catalog.h"
#include "network/commerce/commerce_account.h"
#include "network/commerce/store_catalog.h"
#include "network/profile/token_account.h"
#include "network/profile/after_action.h"

#include <memory>
namespace server_progression
{
	using Json = nlohmann::json;

	struct Catalog
	{
		progression::Catalog items;
		std::vector<int> military, season;
		int militaryCap = 0;
		Json pass;
		Json bundles = Json::object();
		std::map<int, int> tierSkips;
		std::string challenges;
		Json reportRules;

		bool Load(std::string_view bytes, std::string_view rules)
		{
			try
			{
				auto j = Json::parse(bytes);
				if (j.at("version") != 1 || j.at("season") != 3) return false;
				auto read = [&](const std::string& n)
				{
					return j.at("tables").value(n, progression::Table{});
				};
				if (!items.Load(read)) return false;
				military.clear();
				season.clear();
				for (const auto& r : read("mp/ranktable.csv"))
					if (military.size() < 55 && progression::Number(progression::Cell(r, 0)) == int(military.size()))
					{
						military.push_back(progression::Number(progression::Cell(r, 2)));
						militaryCap = progression::Number(progression::Cell(r, 7));
					}
				for (const auto& r : read("mp/progression/elder3_progression.csv"))
					if (progression::Number(progression::Cell(r, 0)) == int(season.size()) + 1)
						season.push_back(progression::Number(progression::Cell(r, 1)));
				pass = Json::array();
				for (const auto& r : read("loot/battlepass_season3.csv"))
				{
					Json grants = Json::array();
					for (const auto& s : challenges::Split(progression::Cell(r, 2)))
						grants.push_back(progression::Number(s));
					pass.push_back({ { "tier", progression::Number(progression::Cell(r, 0)) },
						{ "xp", progression::Number(progression::Cell(r, 1)) }, { "items", grants },
						{ "free", progression::Cell(r, 3) == "1" }, { "cp", progression::Number(progression::Cell(r, 6), 0) } });
				}
				challenges = std::string(rules);
				reportRules = Json::parse(rules);
				tierSkips.clear();
				for (const auto& r : read("loot/battlepass_ids.csv"))
					if (progression::Cell(r, 5) == "skip_tier")
						tierSkips[progression::Number(progression::Cell(r, 0))] =
							std::clamp(progression::Number(progression::Cell(r, 4)), 0, 100);
				bundles = Json::object();
				for (const auto& r : read("loot/bundle_ids.csv"))
				{
					const int id = progression::Number(progression::Cell(r, 0));
					if (!store_catalog::KnownBundle(id)) continue;
					const int price = progression::Number(progression::Cell(r, 10));
					const int count = progression::Number(progression::Cell(r, 13));
					const int hidden = progression::Number(progression::Cell(r, 24), 0);
					if (progression::Cell(r, 9) != "20" || price < 0 || price > commerce::MaxBalance || count < 1 || count > 10 ||
						hidden < 0 || hidden > 10)
						return false;
					Json grants = Json::array();
					for (int n = 0; n < count + hidden; ++n)
					{
						const int item = progression::Number(progression::Cell(r, n < count ? 14 + n : 25 + n - count));
						if (item <= 0) return false;
						grants.push_back(item);
					}
					bundles[std::to_string(id)] = { { "price", price }, { "items", grants } };
				}
				// Current-season premium and repeatable tier offers use the same
				// durable wallet transaction as bundles. Retired seasons stay closed.
				for (const auto& r : read("loot/battlepass_ids.csv"))
				{
					const int id = progression::Number(progression::Cell(r, 0));
					const bool premium = progression::Cell(r, 16) == "3";
					if (!premium && !tierSkips.contains(id)) continue;
					const int count = progression::Number(progression::Cell(r, 10));
					const int price = progression::Number(progression::Cell(r, 8));
					if (progression::Cell(r, 7) != "20" || count < 1 || count > 5 || price <= 0) return false;
					Json grants = Json::array();
					for (int n = 0; n < count; ++n)
						grants.push_back(progression::Number(progression::Cell(r, 11 + n)));
					bundles[std::to_string(id)] = { { "price", price }, { "items", grants }, { "battlepass", true },
						{ "premium", premium } };
				}
				return military.size() == 55 && militaryCap > 0 && season.size() == 100 && pass.size() == 101;
			}
			catch (...)
			{
				return false;
			}
		}

		static int Rank(const std::vector<int>& thresholds, int xp)
		{
			return std::max(0, int(std::upper_bound(thresholds.begin(), thresholds.end(), xp) - thresholds.begin()) - 1);
		}
	};

	// Only trusted dedicated-server reports call Apply. HTTP exposes reads,
	// one-time migration, mission selection and notification acknowledgements.
	class Account
	{
		const Catalog& catalog;
		uint64_t xuid;
		challenges::Engine engine;
		Json weapons = Json::object(), battle = { { "xp", 0 }, { "premium", false }, { "claimed", Json::object() }, { "cp", 0 } };
		Json wallet = commerce::Empty();
		Json tokens = token_account::Empty();
		int tournamentWins = 0;
		Json reports = after_action::Empty();
		bool upgradedTokens = false;
		uint64_t revision = 0;
		bool unlockAll = false; // Access override only: never changes earned state.

		static int Award(const std::string& s)
		{
			char* end = nullptr;
			const auto n = std::strtod(s.c_str(), &end);
			return end != s.c_str() && *end == 0 && std::isfinite(n) && n > 0 && n <= INT_MAX ? int(n) : 0;
		}

		void GrantBundle(const std::string& product)
		{
			const auto& offer = catalog.bundles.at(product);
			if (offer.value("premium", false)) battle["premium"] = true;
			if (!offer.value("battlepass", false)) engine.Grant(std::stoi(product));
			for (const auto& item : catalog.bundles.at(product).at("items"))
			{
				const int id = item.get<int>();
				// Activation is committed atomically with payment. Consumables
				// must not remain in inventory and be activated again on restart.
				if (id != 432000 && !catalog.tierSkips.contains(id)) engine.Grant(id);
				if (!catalog.tierSkips.contains(id)) continue;
				const int xp = battle.at("xp");
				int tier = 0;
				while (tier < 100 && catalog.pass.at(tier + 1).at("xp").get<int>() <= xp)
					++tier;
				const int target = std::min(100, tier + catalog.tierSkips.at(id));
				// Keep earned progress within the current tier when redeeming skips.
				const int remainder = xp - catalog.pass.at(tier).at("xp").get<int>();
				battle["xp"] = std::min(catalog.pass.back().at("xp").get<int>(), catalog.pass.at(target).at("xp").get<int>() + remainder);
			}
			Rewards();
		}

		void Rewards()
		{
			const int rank = Catalog::Rank(catalog.military, engine.XP());
			std::map<int, std::vector<int>> grants;
			for (const auto& [id, level] : catalog.items.military)
				if (!catalog.items.initial.contains(id)) grants[level].push_back(id);
			engine.Notices().Observe("military", "player_ranked_up", rank, {}, grants);
			const int seasonXP = std::max(0, engine.XP() - catalog.militaryCap);
			engine.Notices().Observe("season:3", "season_ranked_up", seasonXP ? Catalog::Rank(catalog.season, seasonXP) : -1, {}, {});
			for (const auto& [ref, def] : catalog.items.weapons)
			{
				std::map<int, std::vector<int>> unlocked;
				for (const auto& [id, u] : catalog.items.weaponUnlocks)
					if (u.weapon == ref) unlocked[u.rank].push_back(id);
				engine.Notices().Observe("weapon:" + ref, "weapon_ranked_up", WeaponRank(ref), ref, unlocked);
			}
			for (const auto& tier : catalog.pass)
			{
				const auto key = std::to_string(tier.at("tier").get<int>());
				if (tier.at("xp").get<int>() > battle.at("xp").get<int>() ||
					(!tier.at("free").get<bool>() && !battle.at("premium").get<bool>()) || battle["claimed"].contains(key))
					continue;
				battle["claimed"][key] = true;
				battle["cp"] = battle.at("cp").get<int>() + tier.at("cp").get<int>();
				for (const auto& item : tier.at("items"))
					engine.Grant(item.get<int>());
			}
			engine.Mastery();
		}

	public:
		Account(const Catalog& c, uint64_t id) : catalog(c), xuid(id)
		{
			engine.Load(c.challenges);
			engine.Initialize(id, 0);
		}

		int WeaponXP(const std::string& ref) const
		{
			return weapons.value(ref, 0);
		}

		int WeaponRank(const std::string& ref) const
		{
			return catalog.items.WeaponRank(ref, WeaponXP(ref));
		}

		unsigned SetUnlock(bool enabled, uint64_t expected)
		{
			if (!revision) return 503;
			if (unlockAll == enabled) return 200; // Idempotent after a lost ACK.
			if (expected != revision || revision == UINT64_MAX) return 409;
			unlockAll = enabled;
			++revision;
			return 200;
		}

		bool UnlockEnabled() const
		{
			return unlockAll && revision > 0;
		}

		bool Owns(int item) const
		{
			return revision > 0 && item > 0 && (unlockAll || engine.Owns(item));
		}

		bool Restore(const Json& j)
		{
			try
			{
				const auto tw = j.value("tournament_wins", Json(0));
				if (!tw.is_number_integer() || tw.get<int>() < 0 || tw.get<int>() > 4) return false;
				const auto savedReports = j.value("after_action", after_action::Empty());
				if (!after_action::Valid(savedReports)) return false;
				if (!j.value("unlock_all", Json(false)).is_boolean()) return false;
				if (j.at("version") != 1 || j.at("xuid") != std::to_string(xuid) || !j.at("imported").get<bool>() ||
					!j.at("weapons").is_object() || !j.at("battlepass").at("premium").is_boolean() ||
					!j.at("battlepass").at("claimed").is_object() || j.at("battlepass").at("claimed").size() > 101)
					return false;
				if (!j.at("revision").is_number_unsigned() || j.at("revision").get<uint64_t>() == 0 ||
					!j.at("battlepass").at("xp").is_number_integer() || !j.at("battlepass").at("cp").is_number_integer())
					return false;
				for (const auto& [w, xp] : j.at("weapons").items())
					if (!catalog.items.weapons.contains(w) || !xp.is_number_integer() || xp.get<int64_t>() < 0 ||
						xp.get<int64_t>() > catalog.items.weapons.at(w).capXP)
						return false;
				const int64_t bp = j.at("battlepass").at("xp"), cp = j.at("battlepass").at("cp");
				int earnedCP = 0;
				for (const auto& [key, value] : j.at("battlepass").at("claimed").items())
				{
					const auto tier = challenges::Number(key);
					if (tier < 0 || tier > 100 || key != std::to_string(tier) || !value.is_boolean() || !value.get<bool>() ||
						catalog.pass.at(size_t(tier)).at("xp").get<int>() > bp)
						return false;
					earnedCP += catalog.pass.at(size_t(tier)).at("cp").get<int>();
				}
				if (cp != earnedCP) return false;
				if (bp < 0 || bp > catalog.pass.back().at("xp").get<int>() || cp < 0 || cp > 1000000 ||
					!engine.Restore(j.at("challenges"), xuid))
					return false;
				const auto savedWallet = j.value("commerce", commerce::Empty());
				if (!commerce::Valid(savedWallet, int(cp), catalog.bundles)) return false;
				const auto savedTokens = j.value("tokens", token_account::Empty());
				if (!token_account::Valid(savedTokens)) return false;
				for (const auto& [key, used] : savedTokens.at("used").items())
					if (used.get<int64_t>() >
						int64_t(engine.TokenGrants(std::stoi(key))) + (key == "200005" ? savedTokens.value("trial_grants", 0) : 0))
						return false;
				weapons = j.at("weapons");
				battle = j.at("battlepass");
				wallet = savedWallet;
				tokens = savedTokens;
				revision = j.at("revision").get<uint64_t>();
				unlockAll = j.value("unlock_all", false);
				// Legacy native challenge progress may predate our award receipt.
				// Preserve already-earned tiers rather than awarding their XP again.
				const auto earnedTournament = engine.Progress(1544);
				if (earnedTournament < 0 || earnedTournament > 4) return false;
				reports = savedReports;
				tournamentWins = std::max(tw.get<int>(), earnedTournament);
				if (tournamentWins > earnedTournament) engine.TournamentProgress(tournamentWins);
				upgradedTokens = !j.contains("tokens");
				if (upgradedTokens)
				{
					// Recover repeated grants provable from old durable receipts.
					// Do not add them to the legacy boolean (that would double count).
					std::map<int, int> known;
					for (const auto& tier : catalog.pass)
						if (battle["claimed"].contains(std::to_string(tier.at("tier").get<int>())))
							for (const auto& item : tier.at("items"))
								if (xp_tokens::Find(item.get<int>())) ++known[item.get<int>()];
					for (const auto& [product, receipt] : wallet.at("purchases").items())
						if (receipt.value("redeemed", false))
							for (const auto& item : catalog.bundles.at(product).at("items"))
								if (xp_tokens::Find(item.get<int>())) ++known[item.get<int>()];
					for (const auto& [item, count] : known)
						engine.RecoverTokenGrants(item, count);
				}
				return revision > 0;
			}
			catch (...)
			{
				return false;
			}
		}

		bool Import(const Json& source, uint32_t utc)
		{
			if (revision) return false;
			try
			{
				if (source.at("xuid") != std::to_string(xuid) || !source.at("weapons").is_object() ||
					!engine.Restore(source.at("challenges"), xuid))
					return false;
				for (const auto& [ref, xp] : source.at("weapons").items())
				{
					if (!catalog.items.weapons.contains(ref) || !xp.is_number_integer() || xp.get<int64_t>() < 0 ||
						xp.get<int64_t>() > INT_MAX)
						return false;
					weapons[ref] = std::min(xp.get<int>(), catalog.items.weapons.at(ref).capXP);
				}
				tournamentWins = engine.Progress(1544);
				if (tournamentWins < 0 || tournamentWins > 4) return false;
				engine.Day(utc);
				Rewards();
				revision = 1;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		bool Apply(challenges::Event e, uint32_t utc)
		{
			if (!revision || e.xuid != xuid) return false;
			// Capture time comes from our dedicated server, never from HTTP/client.
			const uint32_t earnedAt = e.utc && e.utc <= utc ? e.utc : utc;
			e.utc = earnedAt;
			engine.Day(utc);
			if (e.name == "mp_addxp")
				for (const auto& [index, kind] : { std::pair{ 0, "player" }, { 3, "weapon" }, { 4, "battle" } })
					if (e.parameters.size() > size_t(index) && token_account::Active(tokens, kind, earnedAt))
						e.parameters[index] = std::to_string(std::min<int64_t>(INT_MAX, int64_t(Award(e.parameters[index])) * 2));
			const auto before = engine.Snapshot(), weaponsBefore = weapons;
			const int battleBefore = battle.at("xp").get<int>(), balanceBefore = Balance();
			if (!engine.Apply(e,
					[&](const std::string& w)
					{
						return WeaponRank(w);
					}))
				return false;
			if (e.name == "mp_addxp" && e.parameters.size() >= 5)
			{
				const auto ref = progression::WeaponReference(e.parameters[2],
					[&](std::string_view w)
					{
						return catalog.items.weapons.contains(std::string(w));
					});
				if (catalog.items.weapons.contains(ref))
					weapons[ref] =
						int(std::min<int64_t>(catalog.items.weapons.at(ref).capXP, int64_t(WeaponXP(ref)) + Award(e.parameters[3])));
				battle["xp"] = int(std::min<int64_t>(
					catalog.pass.back().at("xp").get<int>(), int64_t(battle.at("xp").get<int>()) + Award(e.parameters[4])));
			}
			Rewards();
			after_action::Observe(reports, e, catalog.reportRules, before, engine.Snapshot(), battleBefore, battle.at("xp").get<int>(),
				weaponsBefore, weapons, e.name == "mp_addxp" && !e.parameters.empty() ? Award(e.parameters[0]) : 0);
			if (e.match)
			{
				const auto id = std::to_string(e.session) + ":" + std::to_string(e.match);
				for (auto& report : reports["matches"])
					if (report.at("id") == id)
					{
						auto& info = report["info"];
						if (!report.contains("wallet_started"))
						{
							info["StartCPpoints"] = balanceBefore;
							report["wallet_started"] = true;
						}
						info["CurrentCPpoints"] = Balance();
						for (const auto* prefix : { "Start", "Current" })
						{
							const auto key = std::string(prefix);
							const int xp = info.at(key + "XP");
							const int seasonXP = std::max(0, xp - catalog.militaryCap);
							info[key + "SeasonXP"] = seasonXP;
						}
					}
			}
			++revision;
			return true;
		}

		bool TournamentWin(int wins)
		{
			if (!revision || wins < 1 || wins > 4) return false;
			if (wins <= tournamentWins) return true;
			// TU19 tournament_challenges.csv, row1544. Award each newly reached tier once.
			constexpr int xp[] = { 0, 500, 1000, 2000, 5000 }, items[] = { 0, 190154, 180045, 190213, 109 };
			for (int tier = tournamentWins + 1; tier <= wins; ++tier)
			{
				engine.AddXP(xp[tier]);
				engine.Grant(items[tier]);
			}
			tournamentWins = wins;
			engine.TournamentProgress(wins);
			Rewards();
			++revision;
			return true;
		}

		bool AckReport(const std::string& id)
		{
			if (!after_action::Ack(reports, id)) return false;
			++revision;
			return true;
		}

		bool Day(uint32_t utc)
		{
			const auto prior = engine.Snapshot().at("day");
			engine.Day(utc);
			bool changed = upgradedTokens || prior != engine.Snapshot().at("day");
			upgradedTokens = false;
			// Upgrade receipts from the initial private commerce test which only
			// granted inventory IDs. Redeem their included tier skips once, too.
			for (auto& [id, receipt] : wallet["purchases"].items())
				if (!receipt.value("redeemed", false))
				{
					GrantBundle(id);
					receipt["redeemed"] = true;
					changed = true;
				}
			if (changed) ++revision;
			return changed;
		}

		bool Activate(int id)
		{
			if (!engine.Activate(id)) return false;
			++revision;
			return true;
		}

		bool Ack(uint64_t seq)
		{
			if (!engine.Notices().Ack(seq)) return false;
			++revision;
			return true;
		}

		void Premium(bool own)
		{
			battle["premium"] = own;
			Rewards();
			++revision;
		}

		int Balance() const
		{
			return int(battle.at("cp").get<int64_t>() + wallet.at("credits").get<int64_t>() - wallet.at("spent").get<int64_t>());
		}

		int TokenQuantity(int item) const
		{
			return int(std::clamp<int64_t>(int64_t(engine.TokenGrants(item)) +
					(item == xp_tokens::TrialTicket ? tokens.value("trial_grants", 0) : 0) -
					tokens.at("used").value(std::to_string(item), 0),
				0, INT_MAX));
		}

		unsigned ActivateToken(const Json& request, uint32_t utc, Json& result)
		{
			int available = 0;
			try
			{
				if (request.at("product") != "trialtickets5")
					available = TokenQuantity(std::stoi(request.at("product").get<std::string>()));
			}
			catch (...)
			{
				return 400;
			}
			const auto next = tokens.at("next");
			const auto status = token_account::Activate(tokens, request, available, utc, result);
			if (next != tokens.at("next")) ++revision;
			return status;
		}

		// The caller serializes and durably saves this account before replying.
		// Monotonic sequence + stable request ID prevents both lost-response retries
		// and stale requests from creating another grant after a client restart.
		unsigned Trade(const Json& request, bool published, Json& result)
		{
			try
			{
				const auto id = request.at("transaction").get<std::string>(), kind = request.at("kind").get<std::string>(),
						   product = request.at("product").get<std::string>();
				if (!commerce::Token(id) || (kind != "currency" && kind != "bundle") || product.empty() || product.size() > 63 ||
					!request.at("sequence").is_number_unsigned())
					return 400;
				const uint64_t sequence = request.at("sequence"), expected = wallet.at("next");
				if (sequence != expected)
				{
					const auto& last = wallet.at("last");
					if (sequence == expected - 1 && !last.empty() && last.at("id") == id && last.at("kind") == kind &&
						last.at("product") == product)
					{
						result = last;
						return 200;
					}
					result = { { "id", id }, { "status", "conflict" } };
					return 409;
				}
				if (expected >= 9007199254740991ull) return 503;
				std::string status = "unavailable";
				int amount = 0;
				if (kind == "currency")
				{
					for (const auto& pack : commerce::Packs())
						if (pack.at("id") == product) amount = pack.at("amount");
					if (amount)
					{
						if (Balance() > commerce::MaxBalance - amount)
							status = "wallet_full";
						else
						{
							wallet["credits"] = wallet.at("credits").get<int64_t>() + amount;
							status = "credited";
						}
					}
				}
				else if (catalog.bundles.contains(product))
				{
					const auto& offer = catalog.bundles.at(product);
					amount = offer.at("price");
					const bool bp = offer.value("battlepass", false), premium = offer.value("premium", false);
					if ((premium && battle.at("premium") == true) ||
						(!bp && (wallet["purchases"].contains(product) || engine.Owns(std::stoi(product)))))
						status = "already_owned";
					else if (bp && !premium && battle.at("xp") >= catalog.pass.back().at("xp"))
						status = "tier_cap";
					else if (!published && !bp)
						status = "unavailable";
					else if (!request.contains("quoted_price") || request.at("quoted_price") != amount)
						status = "price_changed";
					else if (Balance() < amount)
						status = "insufficient_funds";
					else
					{
						wallet["spent"] = wallet.at("spent").get<int64_t>() + amount;
						const int prior = wallet["purchases"].contains(product) ? wallet["purchases"][product].at("price").get<int>() : 0;
						wallet["purchases"][product] = { { "price", prior + amount }, { "id", id }, { "redeemed", true } };
						GrantBundle(product);
						status = "purchased";
					}
				}
				result = { { "id", id }, { "sequence", sequence }, { "kind", kind }, { "product", product }, { "status", status },
					{ "amount", amount }, { "balance", Balance() } };
				wallet["last"] = result;
				wallet["next"] = expected + 1;
				++revision;
				return 200;
			}
			catch (...)
			{
				return 400;
			}
		}

		Json Snapshot() const
		{
			return { { "version", 1 }, { "xuid", std::to_string(xuid) }, { "revision", revision }, { "imported", true },
				{ "weapons", weapons }, { "battlepass", battle }, { "commerce", wallet }, { "tokens", tokens },
				{ "challenges", engine.Snapshot() }, { "unlock_all", unlockAll }, { "after_action", reports },
				{ "tournament_wins", tournamentWins } };
		}
	};
}
