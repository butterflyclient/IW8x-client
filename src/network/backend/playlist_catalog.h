#pragma once
#include "network/backend/playlist_tu27.h"
#include "network/match_http.h"

#include <windows.h>

#include <bcrypt.h>
namespace playlist_catalog
{
	// Separate title-content endpoint, not a publisher-variable value or DW task.
	inline constexpr size_t kReplyLimit = match_http::kPlaylistReplyLimit;
	inline constexpr const char* kBuild = "iw8-1.20.4";

	struct Snapshot
	{
		std::string text, sha256;
		int version = 0;
	};

	inline std::string Digest(std::string_view text)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			throw std::runtime_error("playlist SHA256 provider unavailable");
		unsigned char digest[32]{};
		const auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
			static_cast<ULONG>(text.size()), digest, sizeof(digest));
		BCryptCloseAlgorithmProvider(algorithm, 0);
		if (status < 0) throw std::runtime_error("playlist SHA256 failed");
		constexpr char hex[] = "0123456789abcdef";
		std::string result;
		result.reserve(64);
		for (auto c : digest)
		{
			result += hex[c >> 4];
			result += hex[c & 15];
		}
		return result;
	}

	inline Snapshot Validate(std::string text)
	{
		const auto info = playlists::ValidateTransferDocument(text);
		auto hash = Digest(text);
		return { std::move(text), std::move(hash), info.version };
	}

	inline std::string Encode(const Snapshot& catalog)
	{
		auto wire = nlohmann::json{
			{ "protocol", 1 }, { "build", kBuild }, { "catalog_version", catalog.version }, { "bytes", catalog.text.size() },
			{ "sha256", catalog.sha256 }, { "document", catalog.text }
		}.dump();
		if (wire.size() > kReplyLimit) throw std::runtime_error("playlist response exceeds transfer limit");
		return wire;
	}

	inline Snapshot Decode(std::string_view wire)
	{
		if (wire.empty() || wire.size() > kReplyLimit) throw std::runtime_error("invalid playlist response size");
		auto doc = nlohmann::json::parse(wire);
		if (doc.at("protocol") != 1 || doc.at("build") != kBuild || !doc.at("document").is_string())
			throw std::runtime_error("incompatible playlist response");
		auto result = Validate(doc.at("document").get<std::string>());
		if (doc.at("bytes") != result.text.size() || doc.at("sha256") != result.sha256 || doc.at("catalog_version") != result.version)
			throw std::runtime_error("playlist response content/version/hash mismatch");
		return result;
	}

	inline bool ValidRequest(std::string_view wire)
	{
		if (wire.size() > 256) return false;
		try
		{
			const auto doc = nlohmann::json::parse(wire);
			return doc.is_object() && doc.value("protocol", 0) == 1 && doc.value("build", "") == kBuild;
		}
		catch (...)
		{
			return false;
		}
	}
}
