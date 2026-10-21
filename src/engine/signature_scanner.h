#pragma once
// Read-only pattern matching and PE address resolution. No hooks or writes.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace sigscan
{
	using Bytes = std::span<const uint8_t>;

	struct Pattern
	{
		std::vector<uint8_t> bytes, masks;
		size_t anchor = 0, anchorLength = 0;

		static Pattern Parse(std::string_view text)
		{
			Pattern p;
			auto nibble = [](char c) -> int
			{
				if (c == '?') return -1;
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				throw std::invalid_argument("invalid signature hex digit");
			};
			while (!text.empty())
			{
				const auto start = text.find_first_not_of(" \t\r\n");
				if (start == text.npos) break;
				text.remove_prefix(start);
				auto end = text.find_first_of(" \t\r\n");
				auto token = text.substr(0, end);
				if (token == "?") token = "??";
				if (token.size() != 2) throw std::invalid_argument("signature tokens must be two hex/wildcard digits");
				int hi = nibble(token[0]), lo = nibble(token[1]);
				p.bytes.push_back(uint8_t((hi < 0 ? 0 : hi * 16) | (lo < 0 ? 0 : lo)));
				p.masks.push_back(uint8_t((hi < 0 ? 0 : 240) | (lo < 0 ? 0 : 15)));
				if (end == text.npos) break;
				text.remove_prefix(end);
			}
			if (p.bytes.empty() || p.bytes.size() > 4096) throw std::invalid_argument("signature length must be 1..4096 bytes");
			bool constrained = false;
			size_t run = 0;
			for (size_t i = 0; i < p.masks.size(); ++i)
			{
				constrained |= p.masks[i] != 0;
				run = p.masks[i] == 255 ? run + 1 : 0;
				if (run > p.anchorLength)
				{
					p.anchor = i + 1 - run;
					p.anchorLength = run;
				}
			}
			if (!constrained) throw std::invalid_argument("all-wildcard signatures are not allowed");
			return p;
		}

		bool Matches(Bytes data, size_t at) const
		{
			if (at > data.size() || bytes.size() > data.size() - at) return false;
			for (size_t i = 0; i < bytes.size(); ++i)
				if ((data[at + i] & masks[i]) != bytes[i]) return false;
			return true;
		}
	};

	// Includes overlapping matches and the final possible position; never reads
	// outside the caller's span. Call separately for discontiguous readable regions.
	inline std::vector<size_t> Find(Bytes data, const Pattern& p, size_t limit = 2)
	{
		std::vector<size_t> found;
		if (!limit || data.size() < p.bytes.size()) return found;
		const auto last = data.size() - p.bytes.size();
		size_t at = 0;
		while (at <= last)
		{
			if (p.anchorLength)
			{
				const auto* hit = static_cast<const uint8_t*>(std::memchr(data.data() + at + p.anchor, p.bytes[p.anchor], last - at + 1));
				if (!hit) break;
				at = size_t(hit - data.data()) - p.anchor;
				if (p.anchorLength > 1 && std::memcmp(hit, p.bytes.data() + p.anchor, p.anchorLength) != 0)
				{
					++at;
					continue;
				}
			}
			if (p.Matches(data, at))
			{
				found.push_back(at);
				if (found.size() == limit) break;
			}
			++at;
		}
		return found;
	}

	template <class T>
	T Read(Bytes data, size_t offset)
	{
		if (offset > data.size() || sizeof(T) > data.size() - offset) throw std::runtime_error("truncated PE/operand");
		T value{};
		std::memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}

	struct Section
	{
		std::string name;
		uint32_t rva = 0, virtualSize = 0, flags = 0;
		Bytes bytes;

		bool executable() const
		{
			return (flags & 0x20000000) != 0;
		}

		bool readable() const
		{
			return (flags & 0x40000000) != 0;
		}
	};

	class Image
	{
	public:
		enum class Layout
		{
			File,
			Mapped
		};
		uint64_t preferredBase = 0, actualBase = 0;
		uint32_t size = 0, timestamp = 0;
		std::vector<Section> sections;

		// Mapped requires a fully readable, stable snapshot of SizeOfImage, not a
		// pointer across guard pages. actualBase matters only for relocated abs64.
		static Image Parse(Bytes data, Layout layout = Layout::File, uint64_t actualBase = 0)
		{
			if (Read<uint16_t>(data, 0) != 0x5a4d) throw std::runtime_error("not an MZ image");
			const auto pe = Read<uint32_t>(data, 0x3c);
			if (Read<uint32_t>(data, pe) != 0x4550) throw std::runtime_error("not a PE image");
			if (Read<uint16_t>(data, size_t(pe) + 4) != 0x8664) throw std::runtime_error("only AMD64 images are supported");
			const auto count = Read<uint16_t>(data, size_t(pe) + 6), optSize = Read<uint16_t>(data, size_t(pe) + 20);
			const size_t opt = size_t(pe) + 24;
			if (!count || count > 96 || optSize < 112 || opt > data.size() || optSize > data.size() - opt ||
				Read<uint16_t>(data, opt) != 0x20b)
				throw std::runtime_error("invalid PE32+ headers");
			Image image;
			image.timestamp = Read<uint32_t>(data, size_t(pe) + 8);
			image.preferredBase = Read<uint64_t>(data, opt + 24);
			image.actualBase = actualBase ? actualBase : image.preferredBase;
			image.size = Read<uint32_t>(data, opt + 56);
			if (!image.size || (layout == Layout::Mapped && data.size() < image.size)) throw std::runtime_error("invalid image size");
			const size_t table = opt + optSize;
			if (table > data.size() || count > (data.size() - table) / 40) throw std::runtime_error("truncated section table");
			for (unsigned i = 0; i < count; ++i)
			{
				const size_t p = table + 40 * i;
				Section s;
				for (size_t n = 0; n < 8 && data[p + n]; ++n)
					s.name.push_back(char(data[p + n]));
				const auto vs = Read<uint32_t>(data, p + 8), raw = Read<uint32_t>(data, p + 16);
				s.rva = Read<uint32_t>(data, p + 12);
				s.virtualSize = vs ? vs : raw;
				s.flags = Read<uint32_t>(data, p + 36);
				if (s.rva > image.size || s.virtualSize > image.size - s.rva) throw std::runtime_error("section outside image");
				for (const auto& old : image.sections)
					if (s.virtualSize && old.virtualSize && s.rva < uint64_t(old.rva) + old.virtualSize &&
						old.rva < uint64_t(s.rva) + s.virtualSize)
						throw std::runtime_error("overlapping virtual sections");
				size_t offset = layout == Layout::File ? Read<uint32_t>(data, p + 20) : s.rva;
				size_t length = layout == Layout::File ? std::min(raw, s.virtualSize) : s.virtualSize;
				if (layout == Layout::File && raw && (offset > data.size() || raw > data.size() - offset))
					throw std::runtime_error("truncated section data");
				if (length)
				{
					if (offset > data.size() || length > data.size() - offset) throw std::runtime_error("truncated section");
					s.bytes = data.subspan(offset, length);
				}
				image.sections.push_back(s);
			}
			return image;
		}

		const Section* At(uint64_t rva, size_t length = 1, bool backed = false) const
		{
			for (const auto& s : sections)
			{
				const auto n = backed ? s.bytes.size() : s.virtualSize;
				if (rva >= s.rva && rva - s.rva <= n && length <= n - (rva - s.rva)) return &s;
			}
			return nullptr;
		}

		Bytes Slice(uint64_t rva, size_t length) const
		{
			const auto* s = At(rva, length, true);
			if (!s) throw std::runtime_error("operand outside section bytes");
			return s->bytes.subspan(size_t(rva - s->rva), length);
		}
	};

	struct Rule
	{
		enum class Kind
		{
			Match,
			Rel32,
			Absolute64
		} kind = Kind::Match;
		int64_t offset = 0, next = 0, add = 0;
	};

	inline uint64_t Add(uint64_t value, int64_t delta)
	{
		if (delta >= 0)
		{
			if (uint64_t(delta) > UINT64_MAX - value) throw std::runtime_error("address overflow");
			return value + uint64_t(delta);
		}
		const uint64_t n = uint64_t(-(delta + 1)) + 1;
		if (n > value) throw std::runtime_error("address underflow");
		return value - n;
	}

	inline uint64_t Resolve(const Image& image, uint64_t match, const Rule& rule)
	{
		uint64_t result = Add(match, rule.offset);
		if (rule.kind == Rule::Kind::Rel32)
		{
			const int32_t displacement = Read<int32_t>(image.Slice(result, 4), 0);
			result = Add(Add(match, rule.next), displacement);
		}
		else if (rule.kind == Rule::Kind::Absolute64)
		{
			const auto address = Read<uint64_t>(image.Slice(result, 8), 0);
			if (address < image.actualBase) throw std::runtime_error("absolute pointer outside image");
			result = address - image.actualBase;
		}
		result = Add(result, rule.add);
		if (!image.At(result)) throw std::runtime_error("resolved target outside image sections");
		return result;
	}
}
