#include "diagnostics/night/night_entry_pool.h"
#include "common/console.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>
namespace night_entry_pool
{
	namespace
	{
		constexpr uintptr_t kOldBase = 0xC815C60;
		enum class Kind
		{
			Rip,
			Literal,
			NativeGlobal,
			ImageOffset
		};

		struct Patch
		{
			uintptr_t rva;
			std::uint8_t original[16];
			unsigned size, field;
			Kind kind;
			std::int64_t value;
		};
#include "diagnostics/night/night_entry_pool.inc"
		void* g_storage = nullptr;
		uintptr_t g_image = 0;

		bool encode(const Patch& p, uintptr_t image, uintptr_t table, std::int32_t& result)
		{
			std::int64_t value = p.value;
			switch (p.kind)
			{
			case Kind::Rip:
				value += static_cast<std::int64_t>(table) - static_cast<std::int64_t>(image + p.rva + p.size);
				break;
			case Kind::Literal:
				break;
			case Kind::NativeGlobal:
				value += static_cast<std::int64_t>(image) - static_cast<std::int64_t>(table);
				break;
			case Kind::ImageOffset:
				value += static_cast<std::int64_t>(table) - static_cast<std::int64_t>(image);
				break;
			}
			if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) return false;
			result = static_cast<std::int32_t>(value);
			return true;
		}

		void* reserve_near(uintptr_t image)
		{
			// Above the pinned 0x1324B000-byte image; all RIP and module-relative
			// encodings remain signed 32-bit. VirtualAlloc never replaces a mapping.
			for (uintptr_t at = (image + 0x18000000 + 0xFFFF) & ~uintptr_t(0xFFFF); at < image + 0x60000000;)
			{
				MEMORY_BASIC_INFORMATION info{};
				if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) return nullptr;
				const auto end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
				if (info.State == MEM_FREE && end - at >= kTableBytes)
					if (void* memory = VirtualAlloc(reinterpret_cast<void*>(at), kTableBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
						return memory;
				if (end <= at) return nullptr;
				at = (end + 0xFFFF) & ~uintptr_t(0xFFFF);
			}
			return nullptr;
		}

		bool write(const Patch& p, uintptr_t image, const void* bytes, std::size_t size, unsigned field)
		{
			auto* at = reinterpret_cast<void*>(image + p.rva + field);
			DWORD old = 0, unused = 0;
			if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
			std::memcpy(at, bytes, size);
			const bool same = std::memcmp(at, bytes, size) == 0;
			const bool restored = VirtualProtect(at, size, old, &unused) != 0;
			return same && restored;
		}
	}

	bool Prepare(uintptr_t image)
	{
		if (g_storage) return g_image == image;
		// After the constructor this is a nonzero intrusive sentinel/free-head.
		// Reject a late attempt; no migration of live entries is performed.
		if (*reinterpret_cast<const uintptr_t*>(image + kOldBase + 0x6D6000)) return false;
		for (const auto& p : kPatches)
			if (p.size > 16 || p.field + 4 > p.size || std::memcmp(reinterpret_cast<const void*>(image + p.rva), p.original, p.size))
				return false;
		void* memory = reserve_near(image);
		if (!memory) return false;
		const auto table = reinterpret_cast<uintptr_t>(memory);
		for (const auto& p : kPatches)
		{
			std::int32_t encoded{};
			if (!encode(p, image, table, encoded))
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return false;
			}
		}
		std::size_t changed = 0;
		bool okay = true;
		for (const auto& p : kPatches)
		{
			std::int32_t encoded{};
			encode(p, image, table, encoded);
			++changed; // Include a partially written/protection-failed operand.
			if (!write(p, image, &encoded, sizeof(encoded), p.field))
			{
				okay = false;
				break;
			}
		}
		if (!okay)
		{
			bool restored = true;
			for (std::size_t i = 0; i < changed; ++i)
				restored = write(kPatches[i], image, kPatches[i].original, kPatches[i].size, 0) && restored;
			FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
			if (restored) VirtualFree(memory, 0, MEM_RELEASE);
			return false;
		}
		FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
		g_storage = memory;
		g_image = image;
		return true;
	}

	bool Report()
	{
		if (!g_storage) return false;
		const auto table = reinterpret_cast<uintptr_t>(g_storage);
		const auto sentinel = table + 20ull * kEntryCount;
		const auto free = *reinterpret_cast<const uintptr_t*>(sentinel);
		if (free < table || free > sentinel || (free != sentinel && (free - table) % 20))
		{
			LOG_ERR("NightAssetPool", "native free-list construction failed for expanded entry table");
			return false;
		}
		for (const auto& p : kPatches)
		{
			std::int32_t expected{}, actual{};
			encode(p, g_image, table, expected);
			std::memcpy(&actual, reinterpret_cast<const void*>(g_image + p.rva + p.field), 4);
			if (expected != actual) return false;
		}
		LOG_INFO("NightAssetPool",
			"native XAsset entry table active: entries=%u buckets=358400 planes=13 bytes=%zu table=%p; native lookup and unload preserved",
			kEntryCount, kTableBytes, g_storage);
		return true;
	}
}
