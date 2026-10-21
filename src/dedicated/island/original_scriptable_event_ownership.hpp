#pragma once

// Typed, source-backed data ownership for IW8 1.64.5 ScriptableDef event
// kinds 44, 47, 49. Keeps the original source-packed event bytes, decoded
// XStrings, native pointer target identities and owner ScriptableDef names.
// It deliberately does NOT mislabel these as Replay 1.20 event types, nor
// claim an event run-state, part transition, spawnpickup or inventory hook.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace iw8x::island::original_scriptable_event_ownership
{
	struct packed_alias
	{
		std::uint16_t event_offset{};
		std::uint64_t target_source_physical{};
		std::uint8_t block{};
		bool indirect{};
	};
	struct native_xstring
	{
		std::uint16_t union_offset{};
		std::uint64_t source_physical{};
		std::string value{};
	};
	struct event
	{
		std::uint16_t type{};
		std::uint16_t native_union_bytes{};
		std::uint64_t source_physical{};
		std::string owner_scriptabledef{};
		std::array<std::uint8_t, 176> source_event_record{};
		std::vector<packed_alias> aliases{};
		std::vector<native_xstring> xstrings{};
	};

	class arena final
	{
	public:
		bool import(std::span<const std::uint8_t> bytes)
		{
			if (bytes.size() > 1024 * 1024 || bytes.size() < 24 ||
				std::memcmp(bytes.data(), "ISLEVT01", 8) != 0) return false;
			struct reader
			{
				std::span<const std::uint8_t> src;
				std::size_t at{};
				bool take(void* output, std::size_t n) noexcept
				{
					if (at > src.size() || n > src.size() - at) return false;
					if (n) std::memcpy(output, src.data() + at, n);
					at += n;
					return true;
				}
				bool number16(std::uint16_t& v) noexcept { return take(&v, 2); }
				bool number32(std::uint32_t& v) noexcept { return take(&v, 4); }
				bool number64(std::uint64_t& v) noexcept { return take(&v, 8); }
			};
			reader read{bytes, 8};
			std::array<std::uint32_t, 4> claimed_counts{};
			for (auto& count : claimed_counts)
				if (!read.number32(count)) return false;
			if (claimed_counts != std::array<std::uint32_t, 4>{12, 1, 9, 2}) return false;
			std::vector<event> checked;
			checked.reserve(12);
			std::size_t counts44{}, counts47{}, counts49{}, aliases{}, strings{};
			for (std::size_t index = 0; index < 12; ++index)
			{
				event item;
				std::uint16_t name_length{}, alias_count{}, xstring_count{};
				if (!read.number16(item.type) || !read.number16(item.native_union_bytes) ||
					!read.number16(name_length) || !read.number64(item.source_physical) ||
					!read.number16(alias_count) || !read.number16(xstring_count)) return false;
				std::uint16_t required{};
				if (item.type == 44) { ++counts44; required = 24; }
				else if (item.type == 47) { ++counts47; required = 64; }
				else if (item.type == 49) { ++counts49; required = 48; }
				else return false;
				if (item.native_union_bytes != required || !name_length || name_length > 255 ||
					alias_count > 16 || xstring_count > 2 || !item.source_physical) return false;
				item.owner_scriptabledef.resize(name_length);
				if (!read.take(item.owner_scriptabledef.data(), name_length) ||
					!read.take(item.source_event_record.data(), item.source_event_record.size())) return false;
				std::uint32_t embedded_kind{};
				std::memcpy(&embedded_kind, item.source_event_record.data() + 16, sizeof(embedded_kind));
				if (embedded_kind != item.type) return false;
				for (std::size_t i = 0; i < alias_count; ++i)
				{
					packed_alias entry;
					std::uint8_t indirect{};
					if (!read.number16(entry.event_offset) || !read.number64(entry.target_source_physical) ||
						!read.take(&entry.block, 1) || !read.take(&indirect, 1)) return false;
					if (entry.event_offset > 168 || !entry.target_source_physical || indirect > 1) return false;
					entry.indirect = indirect != 0;
					item.aliases.push_back(entry);
					++aliases;
				}
				for (std::size_t i = 0; i < xstring_count; ++i)
				{
					native_xstring string;
					std::uint16_t length{};
					if (!read.number16(string.union_offset) || !read.number16(length) ||
						!read.number64(string.source_physical)) return false;
					if (!length || length > 512 || !string.source_physical ||
						(string.union_offset != 8 && string.union_offset != 16)) return false;
					string.value.resize(length);
					if (!read.take(string.value.data(), length) ||
						string.value.find('\0') != std::string::npos) return false;
					item.xstrings.push_back(std::move(string));
					++strings;
				}
				if (item.xstrings.size() != (item.type == 44 ? 1u : item.type == 47 ? 2u : 0u))
					return false;
				checked.push_back(std::move(item));
			}
			if (read.at != bytes.size() || counts44 != 1 || counts47 != 9 || counts49 != 2 ||
				aliases != 40 || strings != 19) return false;
			events_ = std::move(checked);
			return true;
		}
		const std::vector<event>& events() const noexcept { return events_; }
		static constexpr bool native_replay_runtime_dispatch_available = false;
	private:
		std::vector<event> events_{};
	};
}
