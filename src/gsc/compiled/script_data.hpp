#pragma once
#include "gsc.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>
namespace gsc::detail
{
	inline constexpr std::size_t kFileLimit = 64 * 1024 * 1024;
	inline constexpr std::size_t kNameLimit = 1024;

	struct ScriptData
	{
		std::int32_t compressed_len{};
		std::int32_t len{};
		std::int32_t bytecode_len{};
		std::span<const std::uint8_t> compressed;
		std::span<const std::uint8_t> bytecode;
	};

	// Serialized format: NUL-terminated name, three little-endian signed lengths,
	// compressed payload, bytecode. No native pointers or alignment padding.
	inline bool Parse(std::span<const std::uint8_t> bytes, ScriptData& result)
	{
		result = {};
		if (bytes.empty() || bytes.size() > kFileLimit) return false;
		const auto name_end = std::find(bytes.begin(), bytes.begin() + std::min(bytes.size(), kNameLimit), 0);
		const auto name_size = static_cast<std::size_t>(name_end - bytes.begin());
		if (!name_size || name_size == std::min(bytes.size(), kNameLimit)) return false;
		const auto header = name_size + 1;
		if (bytes.size() - header < 12) return false;
		ScriptData parsed;
		std::memcpy(&parsed.compressed_len, bytes.data() + header, 4);
		std::memcpy(&parsed.len, bytes.data() + header + 4, 4);
		std::memcpy(&parsed.bytecode_len, bytes.data() + header + 8, 4);
		if (parsed.compressed_len <= 0 || parsed.len <= 0 || parsed.bytecode_len <= 0 || static_cast<std::size_t>(parsed.len) > kFileLimit)
			return false;
		const auto payload = header + 12;
		const auto compressed = static_cast<std::size_t>(parsed.compressed_len);
		const auto bytecode = static_cast<std::size_t>(parsed.bytecode_len);
		if (compressed > bytes.size() - payload || bytecode != bytes.size() - payload - compressed) return false;
		parsed.compressed = bytes.subspan(payload, compressed);
		parsed.bytecode = bytes.subspan(payload + compressed, bytecode);
		result = parsed;
		return true;
	}

	inline bool NormalizeName(std::string_view input, std::string& name)
	{
		name.clear();
		if (input.empty() || input.size() >= kNameLimit) return false;
		for (const unsigned char c : input)
		{
			if (c < 32 || c == ':' || c == 127) return false;
			name.push_back(c == '\\' ? '/' : static_cast<char>(c));
		}
		std::size_t start = 0;
		while (start < name.size())
		{
			const auto end = name.find('/', start);
			const auto part = std::string_view(name).substr(start, end - start);
			if (part.empty() || part == "." || part == "..") return false;
			if (end == std::string::npos) return true;
			start = end + 1;
		}
		return false;
	}

	// Fits only the original just-streamed payload slices. Oversized replacements
	// are handled separately by gsc.cpp using verified unused native block-6 tail
	// storage without changing native XFile stream consumption.
	inline bool Fits(const ScriptFile& native, const ScriptData& custom)
	{
		return native.buffer_ && native.bytecode_ && custom.compressed_len > 0 && custom.bytecode_len > 0 &&
			native.compressed_len_ >= custom.compressed_len && native.bytecode_len_ >= custom.bytecode_len;
	}

	inline void Apply(ScriptFile& native, const ScriptData& custom)
	{
		std::memcpy(const_cast<char*>(native.buffer_), custom.compressed.data(), custom.compressed.size());
		std::memcpy(native.bytecode_, custom.bytecode.data(), custom.bytecode.size());
		native.compressed_len_ = custom.compressed_len;
		native.len_ = custom.len;
		native.bytecode_len_ = custom.bytecode_len;
	}
} // namespace gsc::detail
