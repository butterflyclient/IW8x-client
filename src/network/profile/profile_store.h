#pragma once
#include "auth/profile/stats_file.h"
#include "network/social/chat_backend.h"

#include <limits>
// Opaque TU19 profile storage. Its six native groups never award server XP or
// currency. The client additionally validates each DDL group with native code.

namespace profile_store
{
	using Json = nlohmann::json;
	inline constexpr size_t Chunk = 12000, Maximum = stats_file::kMaxSize * 2;

	inline std::string Encode(std::span<const uint8_t> bytes)
	{
		constexpr char hex[] = "0123456789abcdef";
		std::string result(bytes.size() * 2, '0');
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			result[2 * i] = hex[bytes[i] >> 4];
			result[2 * i + 1] = hex[bytes[i] & 15];
		}
		return result;
	}

	inline std::vector<uint8_t> Decode(std::string_view hex)
	{
		if (hex.size() % 2 || hex.size() > Maximum || !chat::Hex(hex, hex.size())) return {};
		auto nib = [](char c)
		{
			return c <= '9' ? c - '0' : c - 'a' + 10;
		};
		std::vector<uint8_t> bytes(hex.size() / 2);
		for (size_t i = 0; i < bytes.size(); ++i)
			bytes[i] = uint8_t(nib(hex[2 * i]) * 16 + nib(hex[2 * i + 1]));
		return bytes;
	}

	inline bool Valid(uint64_t owner, std::string_view hex, const stats_file::Header* schema = nullptr)
	{
		const auto bytes = Decode(hex);
		if (bytes.size() < sizeof(stats_file::Header)) return false;
		stats_file::Header actual;
		memcpy(&actual, bytes.data(), sizeof(actual));
		stats_file::Header expected;
		if (schema)
			expected = *schema;
		else
			expected.groups = actual.groups;
		expected.xuid = owner;
		expected.source = 0;
		for (const auto& group : actual.groups)
			if (!group.size || group.size > 65536 || !group.version || !group.schema) return false;
		return owner && stats_file::Validate(bytes, expected);
	}

	struct Reply
	{
		unsigned status;
		Json body;
		bool changed = false;
	};

	inline bool RestartUpload(const Json& profile, uint64_t revision, size_t offset)
	{
		try
		{
			return offset > 0 && profile.at("revision").is_number_unsigned() && profile.at("revision").get<uint64_t>() == revision &&
				profile.at("error") == "upload changed";
		}
		catch (...)
		{
			return false;
		}
	}

	class Account
	{
		uint64_t revision = 0, at = 0, base = 0;
		size_t total = 0;
		std::string saved, upload, token, lastToken;
		uint64_t lastBase = 0;

	public:
		Json Save() const
		{
			return { { "schema", 1 }, { "revision", revision }, { "data", saved }, { "receipt", lastToken }, { "base", lastBase } };
		}

		bool Restore(uint64_t owner, const Json& value)
		{
			try
			{
				if (value.at("schema") != 1 || !value.at("revision").is_number_unsigned()) return false;
				auto rev = value.at("revision").get<uint64_t>();
				auto data = value.at("data").get<std::string>();
				if (rev > INT64_MAX || (!rev && !data.empty()) || (rev && !Valid(owner, data))) return false;
				const auto receipt = value.value("receipt", std::string{});
				const auto receiptBase = value.value("base", uint64_t{});
				if ((!receipt.empty() && !chat::Hex(receipt, 32)) || (!receipt.empty() && (!rev || receiptBase != rev - 1))) return false;
				revision = rev;
				saved = std::move(data);
				lastToken = receipt;
				lastBase = receiptBase;
				upload.clear();
				token.clear();
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		Reply Handle(uint64_t owner, const Json& request, uint64_t now)
		{
			auto error = [&](unsigned status, const char* message)
			{
				return Reply{ status, { { "revision", revision }, { "error", message } } };
			};
			try
			{
				if (!owner || !request.is_object()) return error(400, "identity");
				const auto action = request.at("action").get<std::string>();
				if (!request.at("offset").is_number_unsigned() || !request.at("revision").is_number_unsigned())
					return error(400, "position");
				const auto offset = request.at("offset").get<size_t>(), expected = request.at("revision").get<uint64_t>();
				if (action == "fetch")
				{
					if ((offset && expected != revision) || offset > saved.size()) return error(409, "profile changed");
					return { 200,
						{ { "revision", revision }, { "total", saved.size() }, { "offset", offset },
							{ "chunk", saved.substr(offset, Chunk) } } };
				}
				if (action != "save") return error(400, "action");
				if (!request.at("total").is_number_unsigned()) return error(400, "size");
				const auto count = request.at("total").get<size_t>();
				const auto key = request.at("upload").get<std::string>();
				const auto part = request.at("chunk").get<std::string>();
				if (count < sizeof(stats_file::Header) * 2 || count > Maximum || count % 2 || part.empty() || part.size() > Chunk ||
					offset > count || part.size() > count - offset || !chat::Hex(key, 32) || !chat::Hex(part, part.size()))
					return error(400, "chunk");
				// Lost final ACK: repeating any committed fragment with the same
				// upload token returns the committed revision without another write.
				if (lastToken == key && lastBase == expected && saved.size() == count && saved.compare(offset, part.size(), part) == 0)
					return { 200, { { "revision", revision }, { "offset", count }, { "complete", true } } };
				if (expected != revision) return error(409, "revision conflict");
				if (now >= at && now - at > 30000)
				{
					upload.clear();
					token.clear();
				}
				if (offset == 0 && token != key)
				{
					if (!upload.empty()) return error(409, "another upload active");
					token = key;
					base = expected;
					total = count;
				}
				if (token != key || base != expected || total != count) return error(409, "upload changed");
				if (offset < upload.size())
				{
					if (part.size() > upload.size() - offset || upload.compare(offset, part.size(), part))
						return error(409, "chunk changed");
				}
				else if (offset == upload.size())
					upload += part;
				else
					return error(409, "chunk gap");
				at = now;
				if (upload.size() < total)
					return { 200, { { "revision", revision }, { "offset", offset + part.size() }, { "complete", false } } };
				stats_file::Header schema;
				const auto prior = Decode(saved);
				if (revision) memcpy(&schema, prior.data(), sizeof(schema));
				if (!Valid(owner, upload, revision ? &schema : nullptr))
				{
					upload.clear();
					token.clear();
					return error(400, "profile schema");
				}
				if (revision == INT64_MAX) return error(409, "revision exhausted");
				saved = std::move(upload);
				upload.clear();
				lastToken = token;
				lastBase = base;
				token.clear();
				++revision;
				return { 200, { { "revision", revision }, { "offset", total }, { "complete", true } }, true };
			}
			catch (...)
			{
				return error(400, "malformed profile request");
			}
		}
	};
}
