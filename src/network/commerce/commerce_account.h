#pragma once
#include "thirdparty/json.hpp"

#include <cstdint>
#include <string>
namespace commerce
{
	using Json = nlohmann::json;
	inline constexpr int MaxBalance = 1000000;

	inline Json Packs()
	{
		Json result = Json::array();
		for (int amount : { 200, 500, 1100, 2400, 5000, 9500, 13000 })
			result.push_back({ { "id", "codpoints" + std::to_string(amount) }, { "amount", amount } });
		return result;
	}

	inline Json Empty()
	{
		return { { "credits", 0 }, { "spent", 0 }, { "next", uint64_t{ 1 } }, { "last", Json::object() }, { "purchases", Json::object() } };
	}

	inline bool Token(const std::string& s)
	{
		return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == s.npos;
	}

	inline bool Valid(const Json& j, int earnedCP, const Json& bundles)
	{
		try
		{
			if (!j.is_object() || !j.at("credits").is_number_integer() || !j.at("spent").is_number_integer() ||
				!j.at("next").is_number_unsigned() || j.at("next").get<uint64_t>() < 1 ||
				j.at("next").get<uint64_t>() > 9007199254740991ull || !j.at("purchases").is_object() ||
				j.at("purchases").size() > bundles.size() || !j.at("last").is_object())
				return false;
			const int64_t credited = j.at("credits"), spent = j.at("spent"), balance = earnedCP + credited - spent;
			if (credited < 0 || credited > 1000000000000ll || spent < 0 || spent > 1000000000000ll || balance < 0 ||
				balance > MaxBalance + earnedCP)
				return false;
			int64_t total = 0;
			for (const auto& [id, purchase] : j.at("purchases").items())
			{
				if (!bundles.contains(id) || !purchase.at("price").is_number_integer() || purchase.at("price").get<int>() < 0 ||
					purchase.at("price").get<int>() > MaxBalance || !Token(purchase.at("id").get<std::string>()) ||
					(purchase.contains("redeemed") && !purchase.at("redeemed").is_boolean()))
					return false;
				total += purchase.at("price").get<int>();
			}
			if (total != spent) return false;
			if (j.at("next") == 1) return j.at("last").empty();
			const auto& last = j.at("last");
			return Token(last.at("id").get<std::string>()) && last.at("sequence").get<uint64_t>() == j.at("next").get<uint64_t>() - 1 &&
				(last.at("kind") == "currency" || last.at("kind") == "bundle") && last.at("product").is_string() &&
				last.at("product").get_ref<const std::string&>().size() < 64 && last.at("status").is_string();
		}
		catch (...)
		{
			return false;
		}
	}
}
