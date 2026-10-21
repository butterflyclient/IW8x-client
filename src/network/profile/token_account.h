#pragma once
#include "network/commerce/commerce_account.h"
#include "auth/profile/xp_tokens.h"

#include <algorithm>
#include <climits>
namespace token_account
{
	using commerce::Json;

	inline Json Empty()
	{
		return { { "next", uint64_t{ 1 } }, { "last", Json::object() }, { "used", Json::object() }, { "periods", Json::object() } };
	}

	inline bool Valid(const Json& j)
	{
		try
		{
			if (!j.is_object() || !j.at("next").is_number_unsigned() || j.at("next").get<uint64_t>() < 1 ||
				j.at("next").get<uint64_t>() > 9007199254740991ull || !j.at("last").is_object() || !j.at("used").is_object() ||
				j.at("used").size() > xp_tokens::Definitions.size() + 1 || !j.at("periods").is_object() || j.at("periods").size() > 3)
				return false;
			for (const auto& [key, n] : j.at("used").items())
			{
				const auto id = std::stoi(key);
				if (key != std::to_string(id) || !xp_tokens::Counted(id) || !n.is_number_integer() || n.get<int64_t>() < 0 ||
					n.get<int64_t>() > INT_MAX)
					return false;
			}
			const auto credits = j.value("trial_grants", Json(0));
			if (!credits.is_number_integer() || credits.get<int64_t>() < 0 || credits.get<int64_t>() > 1000000) return false;
			for (const auto& [key, periods] : j.at("periods").items())
			{
				if ((key != "player" && key != "weapon" && key != "battle") || !periods.is_array() || periods.size() > 64) return false;
				uint64_t previous = 0;
				for (const auto& p : periods)
				{
					if (!p.is_array() || p.size() != 2 || !p[0].is_number_unsigned() || !p[1].is_number_unsigned()) return false;
					const uint64_t start = p[0], end = p[1];
					if (start < previous || start >= end || end > UINT32_MAX) return false;
					previous = end;
				}
			}
			if (j.at("next") == 1) return j.at("last").empty();
			const auto& last = j.at("last");
			return commerce::Token(last.at("id").get<std::string>()) &&
				last.at("sequence").get<uint64_t>() == j.at("next").get<uint64_t>() - 1 && last.at("status").is_string() &&
				((last.at("kind") == "token" && xp_tokens::Find(std::stoi(last.at("product").get<std::string>()))) ||
					(last.at("kind") == "trial" && last.at("product") == "200005") ||
					(last.at("kind") == "supply" && last.at("product") == "trialtickets5"));
		}
		catch (...)
		{
			return false;
		}
	}

	inline uint32_t Expiry(const Json& j, std::string_view kind)
	{
		const auto key = std::string(kind);
		return j.at("periods").contains(key) && !j.at("periods").at(key).empty() ? j.at("periods").at(key).back().at(1).get<uint32_t>() : 0;
	}

	inline bool Active(const Json& j, std::string_view kind, uint32_t when)
	{
		const auto key = std::string(kind);
		if (j.at("periods").contains(key))
			for (const auto& p : j.at("periods").at(key))
				if (when >= p[0].get<uint32_t>() && when < p[1].get<uint32_t>()) return true;
		return false;
	}

	// Caller saves inventory consumption, timer and receipt in one transaction.
	inline unsigned Activate(Json& j, const Json& request, int available, uint32_t utc, Json& result)
	{
		try
		{
			const auto id = request.at("transaction").get<std::string>(), product = request.at("product").get<std::string>();
			const auto kind = request.at("kind").get<std::string>();
			const bool supply = kind == "supply" && product == "trialtickets5", trial = kind == "trial" && product == "200005";
			const int item = supply ? 0 : std::stoi(product);
			const auto* d = xp_tokens::Find(item);
			if ((!supply && !trial && (!d || kind != "token" || product != std::to_string(item))) || !commerce::Token(id) ||
				!request.at("sequence").is_number_unsigned())
				return 400;
			const uint64_t seq = request.at("sequence"), next = j.at("next");
			if (seq != next)
			{
				const auto& last = j.at("last");
				if (seq == next - 1 && !last.empty() && last.at("id") == id && last.at("product") == product && last.at("kind") == kind)
				{
					result = last;
					return 200;
				}
				result = { { "id", id }, { "status", "conflict" } };
				return 409;
			}
			if (next >= 9007199254740991ull) return 503;
			std::string status = "not_owned";
			if (supply)
			{
				const int grants = j.value("trial_grants", 0);
				if (grants <= 999995)
				{
					j["trial_grants"] = grants + 5;
					status = "credited";
				}
				else
					status = "inventory_limit";
			}
			else if (trial)
			{
				if (available > 0)
				{
					j["used"][product] = j["used"].value(product, 0) + 1;
					status = "activated";
				}
			}
			else if (available > 0)
			{
				const std::string kind(d->kind);
				const uint64_t end = uint64_t(std::max(utc, Expiry(j, kind))) + d->seconds;
				if (end > UINT32_MAX)
					status = "timer_limit";
				else
				{
					if (!j["periods"].contains(kind)) j["periods"][kind] = Json::array();
					auto& periods = j["periods"][kind];
					// Retain past intervals for delayed awards; never boost an event
					// that occurred before activation or after expiration.
					while (!periods.empty() && uint64_t(periods.front()[1].get<uint32_t>()) + 604800 < utc)
						periods.erase(periods.begin());
					if (!periods.empty() && periods.back()[1].get<uint32_t>() >= utc)
						periods.back()[1] = uint32_t(end);
					else if (periods.size() < 64)
						periods.push_back(Json::array({ utc, uint32_t(end) }));
					else
					{
						status = "timer_limit";
					}
					if (status != "timer_limit")
					{
						j["used"][product] = j["used"].value(product, 0) + 1;
						status = "activated";
					}
				}
			}
			result = { { "id", id }, { "sequence", seq }, { "kind", kind }, { "product", product }, { "status", status },
				{ "expires", d ? Expiry(j, d->kind) : 0 } };
			j["last"] = result;
			j["next"] = next + 1;
			return 200;
		}
		catch (...)
		{
			return 400;
		}
	}
}
