#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>
namespace stats_file
{
	struct Group
	{
		uint32_t size = 0;
		uint32_t version = 0;
		uint64_t schema = 0;
		bool operator==(const Group&) const = default;
	};

	struct Header
	{
		char magic[8] = { 'I', 'W', '8', 'S', 'T', 'A', 'T', 'S' };
		uint32_t version = 1;
		uint32_t source = 0;
		uint64_t xuid = 0;
		std::array<Group, 6> groups{};
		uint32_t checksum = 0;
		uint32_t reserved = 0;
	};

	static_assert(sizeof(Header) == 128);
	constexpr size_t kMaxSize = 128 + 128 * 1024;

	inline uint32_t Checksum(std::span<const uint8_t> bytes)
	{
		uint32_t crc = ~0u;
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			// The checksum covers identity/schema metadata too, excluding itself.
			const uint8_t value = i >= offsetof(Header, checksum) && i < offsetof(Header, checksum) + 4 ? 0 : bytes[i];
			crc ^= value;
			for (int bit = 0; bit < 8; ++bit)
				crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
		}
		return ~crc;
	}

	inline bool Validate(std::span<const uint8_t> bytes, const Header& expected)
	{
		if (bytes.size() < sizeof(Header) || bytes.size() > kMaxSize) return false;
		Header actual;
		memcpy(&actual, bytes.data(), sizeof(actual));
		if (memcmp(actual.magic, expected.magic, 8) || actual.version != 1 || actual.reserved || actual.xuid != expected.xuid ||
			actual.source != expected.source || actual.groups != expected.groups)
			return false;
		size_t size = sizeof(Header);
		for (const auto& group : actual.groups)
		{
			if (!group.size || group.size > kMaxSize - size) return false;
			size += group.size;
		}
		return size == bytes.size() && Checksum(bytes) == actual.checksum;
	}

	// Replay DDL_LocallyUnObfuscate (2054010) keys bytes by their ORIGINAL address.
	// Decode a snapshot, never the live bank, so active engine contexts stay valid.
	inline void Decode(std::span<uint8_t> copy, uintptr_t original, size_t headerBytes, uint32_t seed)
	{
		for (size_t i = headerBytes; i < copy.size(); ++i)
		{
			const uint32_t key = static_cast<uint16_t>((original + i) ^ seed);
			const uint32_t product = key * (key + 6u);
			copy[i] ^= static_cast<uint8_t>(product ^ (product >> 8));
		}
	}
}
