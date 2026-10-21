#pragma once
#include "thirdparty/json.hpp"

#include <set>
#include <string>

#include "network/backend/customization_names.h"
namespace custom_mods
{
	using Json = nlohmann::json;

	inline bool Number(const Json& value, uint64_t maximum)
	{
		return (value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0)) && value.get<uint64_t>() <= maximum;
	}

	inline bool Ref(const Json& value)
	{
		if (!value.is_string()) return false;
		const auto& s = value.get_ref<const std::string&>();
		if (s.empty() || s.size() > 63) return false;
		for (unsigned char c : s)
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
		return true;
	}

	inline bool Token(const Json& value)
	{
		if (!value.is_string() || value.get_ref<const std::string&>().size() != 32) return false;
		for (char c : value.get_ref<const std::string&>())
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
		return true;
	}

	inline std::string Key(const std::string& weapon, int slot)
	{
		return weapon + ":" + std::to_string(slot);
	}

	inline bool Mod(const Json& mod, const std::string& weapon, int slot, bool legacy = false)
	{
		try
		{
			if (!mod.is_object() || mod.size() != 9 || mod.at("weapon") != weapon || !Ref(mod.at("weapon")) || slot < 0 || slot >= 5)
				return false;
			const auto name = mod.at("blueprintName").get<std::string>();
			if (name.size() < 4 || name.substr(0, 3) != "00" + std::to_string(slot) ||
				!customization_names::Valid(std::string_view(name).substr(3), legacy ? 48 : customization_names::ModBytes, false))
				return false;
			if (!Number(mod.at("lootItemID"), 100000000) || !Number(mod.at("variantID"), 65535)) return false;
			for (const auto* key : { "camo", "cosmeticAttachment", "reticle" })
				if (!Ref(mod.at(key))) return false;
			const auto& attachments = mod.at("attachmentSetup");
			const auto& stickers = mod.at("sticker");
			if (!attachments.is_array() || attachments.size() != 5 || !stickers.is_array() || stickers.size() != 4) return false;
			for (const auto& a : attachments)
				if (!a.is_object() || a.size() != 2 || !Ref(a.at("attachment")) || !Number(a.at("variantID"), 65535)) return false;
			for (const auto& s : stickers)
				if (!Ref(s)) return false;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	inline Json Empty()
	{
		return { { "version", 1 }, { "revision", uint64_t{ 0 } }, { "mods", Json::object() }, { "last", Json::object() } };
	}

	inline bool Document(const Json& doc)
	{
		try
		{
			if (doc.at("version") != 1 || !Number(doc.at("revision"), 9007199254740990ull) || !doc.at("mods").is_object() ||
				doc.at("mods").size() > 640 || !doc.at("last").is_object())
				return false;
			for (const auto& [key, mod] : doc.at("mods").items())
			{
				const auto split = key.find(':');
				if (split == std::string::npos || split + 2 != key.size() || key.back() < '0' || key.back() > '4' ||
					!Mod(mod, key.substr(0, split), key.back() - '0', true))
					return false;
			}
			const auto& last = doc.at("last");
			return last.empty() || (last.size() == 2 && Token(last.at("transaction")) && last.at("request").is_object());
		}
		catch (...)
		{
			return false;
		}
	}

	// Compare-and-swap prevents a stale second client from silently overwriting
	// newer builds. The committed request makes a lost-response retry idempotent.
	inline unsigned Apply(Json& doc, const Json& request, const std::set<std::string>& weapons, std::string& result)
	{
		result = "invalid";
		try
		{
			const auto action = request.at("action").get<std::string>();
			const auto weapon = request.at("weapon").get<std::string>();
			if ((action != "save" && action != "delete") || !weapons.contains(weapon) || !Number(request.at("slot"), 4) ||
				!Number(request.at("revision"), 9007199254740990ull) || !Token(request.at("transaction")))
				return 422;
			const int slot = request.at("slot");
			if (action == "save" && !Mod(request.at("mod"), weapon, slot)) return 422;
			Json mutation = { { "action", action }, { "weapon", weapon }, { "slot", slot }, { "revision", request.at("revision") } };
			if (action == "save") mutation["mod"] = request.at("mod");
			const auto& last = doc.at("last");
			if (!last.empty() && last.at("transaction") == request.at("transaction"))
			{
				if (last.at("request") != mutation)
				{
					result = "conflict";
					return 409;
				}
				result = action == "save" ? "saved" : "deleted";
				return 200;
			}
			if (request.at("revision") != doc.at("revision"))
			{
				result = "conflict";
				return 409;
			}
			const auto key = Key(weapon, slot);
			if (action == "save")
				doc["mods"][key] = request.at("mod");
			else
				doc["mods"].erase(key);
			doc["revision"] = doc.at("revision").get<uint64_t>() + 1;
			doc["last"] = { { "transaction", request.at("transaction") }, { "request", std::move(mutation) } };
			result = action == "save" ? "saved" : "deleted";
			return 200;
		}
		catch (...)
		{
			return 422;
		}
	}

	inline std::string Field(const Json& snapshot, const std::string& path)
	{
		// Expose only the retail blueprint field grammar, never arbitrary JSON paths.
		if (!path.starts_with("blueprints.") || path.size() > 200) return {};
		const auto at = path.find(".weaponSetup.", 11);
		if (at == std::string::npos) return {};
		const auto slotIndex = at + std::string_view(".weaponSetup.").size();
		if (slotIndex + 1 >= path.size() || path[slotIndex] < '0' || path[slotIndex] > '4' || path[slotIndex + 1] != '.') return {};
		const auto field = path.substr(slotIndex + 2);
		const auto weapon = path.substr(11, at - 11);
		const auto key = custom_mods::Key(weapon, path[slotIndex] - '0');
		const auto& mods = snapshot.at("mods");
		if (!mods.contains(key))
		{
			if (field == "blueprintName") return {};
			if (field == "weapon") return "iw8_gunless";
			if (field == "variantID" || field == "lootItemID" || field.ends_with(".variantID")) return "0";
			return "none";
		}
		try
		{
			const auto& mod = mods.at(key);
			Json value;
			if (field.starts_with("attachmentSetup.") && field.size() > 18 && field[16] >= '0' && field[16] < '5' && field[17] == '.')
				value = mod.at("attachmentSetup").at(field[16] - '0').at(field.substr(18));
			else if (field.size() == 9 && field.starts_with("sticker.") && field[8] >= '0' && field[8] < '4')
				value = mod.at("sticker").at(field[8] - '0');
			else if (field == "blueprintName" || field == "weapon" || field == "variantID" || field == "lootItemID" || field == "camo" ||
				field == "cosmeticAttachment" || field == "reticle")
				value = mod.at(field);
			if (value.is_string()) return value.get<std::string>();
			if (value.is_number()) return value.dump();
		}
		catch (...)
		{
		}
		return {};
	}

}
