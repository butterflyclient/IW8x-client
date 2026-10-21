#pragma once
#include "network/social/chat_backend.h"

#include <cmath>
namespace settings120
{
	using Json = nlohmann::json;

	struct Field
	{
		const char* name;
		unsigned index, offset, type, size, bit;
		bool alias;
	};

	inline constexpr Field fields[]{
#include "network/profile/settings_fields.inc"
	};

	inline bool ValueValid(const Field& f, const Json& v)
	{
		if (f.type == 6)
		{
			if (!v.is_string()) return false;
			const auto& s = v.get_ref<const std::string&>();
			return s.size() < f.size && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ .-") == s.npos;
		}
		if (!v.is_number()) return false;
		const auto n = v.get<double>();
		if (!std::isfinite(n)) return false;
		if (f.type == 5) return n >= -1000000 && n <= 1000000;
		if (std::floor(n) != n) return false;
		switch (f.type)
		{
		case 1:
			return n >= 0 && n <= 255;
		case 2:
			return n == 0 || n == 1;
		case 3:
			return n >= INT16_MIN && n <= INT16_MAX;
		case 4:
			return n >= INT32_MIN && n <= INT32_MAX;
		case 8:
			return f.size < 32 && n >= 0 && n < double(uint64_t(1) << f.size);
		default:
			return false;
		}
	}

	inline bool Valid(const Json& v)
	{
		if (!v.is_object() || v.size() != std::size(fields)) return false;
		for (const auto& f : fields)
			if (!v.contains(f.name) || !ValueValid(f, v.at(f.name))) return false;
		return true;
	}

	inline bool Revision(const Json& v)
	{
		return v.is_number_unsigned() && v.get<uint64_t>() < INT64_MAX;
	}

	struct Reply
	{
		unsigned status;
		Json body;
		bool changed = false;
	};

	class Account
	{
		uint64_t revision = 0;
		Json values = nullptr;
		std::string receipt;

	public:
		Json Save() const
		{
			return { { "schema", 1 }, { "revision", revision }, { "values", values }, { "receipt", receipt } };
		}

		bool Restore(uint64_t, const Json& v)
		{
			try
			{
				if (v.at("schema") != 1 || !Revision(v.at("revision"))) return false;
				const auto rev = v.at("revision").get<uint64_t>();
				const auto token = v.at("receipt").get<std::string>();
				if (rev ? (!Valid(v.at("values")) || !chat::Hex(token, 32)) : (!v.at("values").is_null() || !token.empty())) return false;
				revision = rev;
				values = v.at("values");
				receipt = token;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		Reply Handle(uint64_t owner, const Json& request, uint64_t)
		{
			try
			{
				if (!owner) return { 401, {} };
				if (request.at("action") == "fetch") return { 200, { { "revision", revision }, { "values", values } } };
				if (request.at("action") != "save" || !Revision(request.at("revision")) || !Valid(request.at("values"))) return { 400, {} };
				const auto token = request.at("token").get<std::string>();
				if (!chat::Hex(token, 32)) return { 400, {} };
				const auto expected = request.at("revision").get<uint64_t>();
				if (revision && expected == revision - 1 && token == receipt && request.at("values") == values)
					return { 200, { { "revision", revision }, { "token", receipt } } };
				if (expected != revision || revision >= INT64_MAX - 1) return { 409, { { "revision", revision } } };
				values = request.at("values");
				receipt = token;
				++revision;
				return { 200, { { "revision", revision }, { "token", receipt } }, true };
			}
			catch (...)
			{
				return { 400, {} };
			}
		}
	};
}
