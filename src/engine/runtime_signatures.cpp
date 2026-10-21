#include "runtime_signatures.hpp"
#include "signature_scanner.h"
#include "image_identity.h"
#include "common/console.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

namespace runtime_signatures
{
	namespace
	{
		struct entry
		{
			const char* name;
			uint32_t rva;
			const char* pattern;
			const char* section;
			sigscan::Rule rule;
			const char* target;
		};
		constexpr auto match = sigscan::Rule::Kind::Match;
		constexpr auto rel32 = sigscan::Rule::Kind::Rel32;
		constexpr auto abs64 = sigscan::Rule::Kind::Absolute64;
		constexpr entry registry[] = {
#define IW8X_RUNTIME_SIGNATURE(name, rva, pattern, section, kind, offset, next, add, target, aliases) \
			{ name, rva, pattern, section, { kind, offset, next, add }, target },
#include "../../data/signatures/runtime_registry.inc"
#undef IW8X_RUNTIME_SIGNATURE
		};
		std::atomic<bool> published{ false };

		bool selected(const sigscan::Section& section, const char* requested)
		{
			const std::string_view name(requested);
			if (name == "executable") return section.executable();
			if (name == "readable") return section.readable();
			return section.name == name;
		}
	}

	bool scan(std::span<const uint8_t> file, report& output)
	{
		output = {};
		output.patterns = std::size(registry);
		const auto start = std::chrono::steady_clock::now();
		const char* current = "PE headers";
		try
		{
			const auto image = sigscan::Image::Parse(file);
			struct prepared
			{
				const entry* source;
				sigscan::Pattern pattern;
				size_t pivot = 0, matches = 0;
				uint64_t at = 0;
			};
			std::array<uint64_t, 256> frequency{};
			for (const auto& section : image.sections)
				if (section.readable() || section.executable())
					for (const auto byte : section.bytes) ++frequency[byte];
			std::vector<prepared> patterns;
			patterns.reserve(std::size(registry));
			std::array<std::vector<size_t>, 256> groups;
			for (const auto& item : registry)
			{
				current = item.name;
				prepared value{ &item, sigscan::Pattern::Parse(item.pattern) };
				auto rarest = UINT64_MAX;
				for (size_t byte = 0; byte < value.pattern.bytes.size(); ++byte)
					if (value.pattern.masks[byte] == 255 && frequency[value.pattern.bytes[byte]] < rarest)
					{
						rarest = frequency[value.pattern.bytes[byte]];
						value.pivot = byte;
					}
				if (rarest == UINT64_MAX) throw std::runtime_error("registry pattern needs one exact byte");
				groups[value.pattern.bytes[value.pivot]].push_back(patterns.size());
				patterns.push_back(std::move(value));
			}
			// Search each selected byte once per section, then verify the complete
			// string pattern. This shares scans without per-signature threads or a
			// large address index; every occurrence still participates in uniqueness.
			for (const auto& section : image.sections)
			{
				for (size_t byte = 0; byte < groups.size(); ++byte)
				{
					if (groups[byte].empty()) continue;
					std::vector<size_t> selected_patterns;
					for (const auto index : groups[byte])
						if (selected(section, patterns[index].source->section)) selected_patterns.push_back(index);
					if (selected_patterns.empty()) continue;
					size_t offset = 0;
					while (offset < section.bytes.size())
					{
						const auto* hit = static_cast<const uint8_t*>(std::memchr(section.bytes.data() + offset,
							int(byte), section.bytes.size() - offset));
						if (!hit) break;
						const auto position = size_t(hit - section.bytes.data());
						for (const auto index : selected_patterns)
						{
							auto& pattern = patterns[index];
							if (pattern.matches >= 2 || position < pattern.pivot) continue;
							const auto at = position - pattern.pivot;
							if (!pattern.pattern.Matches(section.bytes, at)) continue;
							++pattern.matches;
							pattern.at = section.rva + at;
						}
						offset = position + 1;
					}
				}
			}
			for (const auto& pattern : patterns)
			{
				const auto& item = *pattern.source;
				current = item.name;
				if (pattern.matches != 1) throw std::runtime_error(pattern.matches ? "ambiguous string signature" : "missing string signature");
				const auto resolved = sigscan::Resolve(image, pattern.at, item.rule);
				if (resolved != item.rva) throw std::runtime_error("resolved RVA differs from pinned binding");
				const auto* target = image.At(resolved);
				if (!target || !selected(*target, item.target)) throw std::runtime_error("target section permission differs");
				++output.resolved;
			}
		}
		catch (const std::exception& error)
		{
			output.failure = std::string(current) + ": " + error.what();
		}
		output.elapsed_us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count());
		return output.failure.empty();
	}

	bool initialize(const wchar_t* path)
	{
		if (published.load(std::memory_order_acquire)) return true;
		// Lock the executable against writes/deletion while taking the immutable
		// file snapshot. Hash and scan this same snapshot before publication.
		const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		struct file_owner
		{
			HANDLE value;
			~file_owner() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
		} owner{ file };
		std::vector<uint8_t> bytes;
		LARGE_INTEGER length{};
		bool okay = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 512LL * 1024 * 1024;
		if (okay)
		{
			bytes.resize(size_t(length.QuadPart));
			DWORD read = 0;
			okay = ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size();
		}
		CloseHandle(file);
		owner.value = INVALID_HANDLE_VALUE;
		if (!okay) return false;
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		unsigned char digest[32]{};
		okay = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
		if (okay) okay = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
		if (okay) okay = BCryptHashData(hash, bytes.data(), ULONG(bytes.size()), 0) >= 0;
		if (okay) okay = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		std::string hexadecimal;
		constexpr char alphabet[] = "0123456789abcdef";
		for (const auto value : digest)
		{
			hexadecimal += alphabet[value >> 4];
			hexadecimal += alphabet[value & 15];
		}
		if (!okay || !image_identity::Supported(hexadecimal))
		{
			LOG_ERR("Signatures", "unsupported executable SHA256=%s", hexadecimal.c_str());
			return false;
		}
		report output;
		if (!scan(bytes, output))
		{
			LOG_ERR("Signatures", "%s (%zu/%zu resolved)", output.failure.c_str(), output.resolved, output.patterns);
			return false;
		}
		published.store(true, std::memory_order_release);
		LOG_INFO("Signatures", "startup string signatures resolved=%zu/%zu in %lluus; SHA256=%s",
			output.resolved, output.patterns, output.elapsed_us, hexadecimal.c_str());
		return true;
	}

	bool ready() noexcept { return published.load(std::memory_order_acquire); }
	size_t count() noexcept { return std::size(registry); }
	bool contains(uintptr_t rva) noexcept
	{
		// Generated rows are sorted by their pinned target RVA.
		const auto* found = std::lower_bound(std::begin(registry), std::end(registry), rva,
			[](const entry& item, uintptr_t address) { return item.rva < address; });
		return found != std::end(registry) && found->rva == rva;
	}
}
