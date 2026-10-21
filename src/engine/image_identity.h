#pragma once
#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <string>
#include <string_view>
namespace image_identity
{
	inline constexpr std::string_view ReplaySHA256 = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0";

	// Worker-thread gate, never called while holding the loader lock. Keep the file
	// open without write/delete sharing until hashing completes.
	inline std::string Digest(const wchar_t* path)
	{
		HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE) return {};
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::array<unsigned char, 65536> bytes{};
		std::array<unsigned char, 32> digest{};
		bool okay = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
		if (okay) okay = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
		while (okay)
		{
			DWORD read = 0;
			if (!ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr))
			{
				okay = false;
				break;
			}
			if (!read) break;
			okay = BCryptHashData(hash, bytes.data(), read, 0) >= 0;
		}
		if (okay) okay = BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) >= 0;
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		CloseHandle(file);
		if (!okay) return {};
		constexpr char hex[] = "0123456789abcdef";
		std::string result;
		result.reserve(64);
		for (auto byte : digest)
		{
			result += hex[byte >> 4];
			result += hex[byte & 15];
		}
		return result;
	}

	inline bool Supported(std::string_view digest)
	{
		return digest == ReplaySHA256;
	}
}
