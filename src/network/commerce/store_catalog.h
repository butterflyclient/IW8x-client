#pragma once
#include "network/backend/motd_catalog.h"

#include <set>
#include <algorithm>
namespace store_catalog
{
	using Json = nlohmann::json;
	// This service selects shipped preview bundles. Wallets/grants are separate.
	inline bool KnownBundle(uint32_t id);

	inline bool Validate(const Json& j)
	{
		try
		{
			if (!j.is_object() || j.size() != 4 || j.at("version") != 1 || !motd::Unsigned(j.at("revision"), 0xFFFFFFFFull) ||
				j.at("revision") == 0 || j.at("layout") != "billboard_grid" || !j.at("categories").is_array() ||
				j.at("categories").size() > 10)
				return false;
			std::set<std::string> categories;
			for (const auto& c : j.at("categories"))
			{
				if (!c.is_object() || c.size() != 3 || !c.at("id").is_string() || !c.at("name").is_string() || !c.at("offers").is_array() ||
					c.at("offers").size() > 8)
					return false;
				const auto id = c.at("id").get<std::string>(), name = c.at("name").get<std::string>();
				const std::set<std::string> ids = { "featured", "operator", "weapon", "temp_category_1", "temp_category_2",
					"temp_category_3", "temp_category_4", "temp_category_5" };
				if (!ids.contains(id) || !categories.insert(id).second || !motd::Text(c.at("name"), 96, true) ||
					std::any_of(name.begin(), name.end(),
						[](unsigned char ch)
						{
							return ch < 32;
						}))
					return false;
				std::set<uint32_t> bundles;
				for (const auto& o : c.at("offers"))
				{
					if (!o.is_object() || o.size() != 3 || !motd::Unsigned(o.at("bundle_id"), 100000000) ||
						!motd::Unsigned(o.at("starts"), 4102444800ull) || !motd::Unsigned(o.at("ends"), 4102444800ull))
						return false;
					const auto bundle = o.at("bundle_id").get<uint32_t>();
					if (!KnownBundle(bundle) || !bundles.insert(bundle).second || (o.at("ends") != 0 && o.at("ends") <= o.at("starts")))
						return false;
				}
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	inline Json Select(Json j, uint64_t utc)
	{
		for (auto& c : j["categories"])
		{
			auto& offers = c["offers"];
			offers.erase(std::remove_if(offers.begin(), offers.end(),
							 [utc](const Json& o)
							 {
								 return o.at("starts").get<uint64_t>() > utc || (o.at("ends") != 0 && o.at("ends").get<uint64_t>() <= utc);
							 }),
				offers.end());
		}
		return j;
	}
}
#include "network/commerce/store_bundle_ids.h"
