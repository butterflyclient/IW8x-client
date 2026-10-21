#include "diagnostics/night/night_light_bits.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include <windows.h>
#include <cstring>
#include <limits>
namespace night_light_bits
{
	namespace
	{
		constexpr size_t Capacity = 8192, Bytes = Capacity / 8;

		struct Patch
		{
			uintptr_t rva;
			unsigned char original[16];
			unsigned size, field, width;
			bool rip;
			std::int64_t value;
		};
#include "diagnostics/night/night_light_bits.inc"
		void* storage{};
		uintptr_t installedImage{};

		bool encode(const Patch& p, uintptr_t image, uintptr_t target, std::int64_t& value)
		{
			value = p.rip ? static_cast<std::int64_t>(target) - static_cast<std::int64_t>(image + p.rva + p.size) : p.value;
			return p.rip ? (value >= INT32_MIN && value <= INT32_MAX)
						 : (value >= 0 && static_cast<std::uint64_t>(value) < (1ull << (p.width * 8)));
		}

		void* reserve_near(uintptr_t image)
		{
			for (uintptr_t at = (image + 0x18000000 + 0xFFFF) & ~uintptr_t(0xFFFF); at < image + 0x60000000;)
			{
				MEMORY_BASIC_INFORMATION info{};
				if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) return nullptr;
				const auto end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
				if (info.State == MEM_FREE && end - at >= Bytes)
					if (void* memory = VirtualAlloc(reinterpret_cast<void*>(at), Bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
						return memory;
				if (end <= at) return nullptr;
				at = (end + 0xFFFF) & ~uintptr_t(0xFFFF);
			}
			return nullptr;
		}

		bool write(uintptr_t at, const void* bytes, size_t count)
		{
			DWORD old{}, unused{};
			if (!VirtualProtect(reinterpret_cast<void*>(at), count, PAGE_EXECUTE_READWRITE, &old)) return false;
			std::memcpy(reinterpret_cast<void*>(at), bytes, count);
			const bool same = !std::memcmp(reinterpret_cast<void*>(at), bytes, count);
			const bool restored = VirtualProtect(reinterpret_cast<void*>(at), count, old, &unused) != 0;
			return same && restored;
		}
	}

	hook::Status Install(uintptr_t image)
	{
		if (!night_files::Enabled()) return hook::Status::Installed;
		if (storage) return installedImage == image ? hook::Status::Installed : hook::Status::Failed;
		// Called during startup after bundle admission, before any frontend/world
		// rendering. Native filtering, light selection and other globals stay intact.
		// The worker's 0x300-byte stack array and three entity-linker 0x2F0-byte
		// arrays also need expansion to 0x400; matching
		// SAVE_NONVOL/ALLOC_LARGE/GS metadata is changed with its frame operands.
		for (const auto& p : kPatches)
		{
			unsigned char actual[16]{};
			SIZE_T read{};
			if (p.size > 16 || p.field + p.width > p.size ||
				!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(image + p.rva), actual, p.size, &read) || read != p.size ||
				std::memcmp(actual, p.original, p.size))
				return hook::Status::Failed;
		}
		void* memory = reserve_near(image);
		if (!memory) return hook::Status::Failed;
		for (const auto& p : kPatches)
		{
			std::int64_t value{};
			if (!encode(p, image, reinterpret_cast<uintptr_t>(memory), value))
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return hook::Status::Failed;
			}
		}
		size_t changed = 0;
		bool okay = true;
		for (const auto& p : kPatches)
		{
			std::int64_t value{};
			encode(p, image, reinterpret_cast<uintptr_t>(memory), value);
			++changed;
			if (!write(image + p.rva + p.field, &value, p.width))
			{
				okay = false;
				break;
			}
		}
		if (!okay)
		{
			bool restored = true;
			for (size_t i = 0; i < changed; ++i)
				restored = write(image + kPatches[i].rva, kPatches[i].original, kPatches[i].size) && restored;
			FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
			if (restored) VirtualFree(memory, 0, MEM_RELEASE);
			return hook::Status::Failed;
		}
		FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
		storage = memory;
		installedImage = image;
		LOG_INFO("NightLights",
			"expanded native visibility bitsets to %zu lights; global=%p bytes=%zu; worker and three entity-linker stacks/unwind metadata updated",
			Capacity, storage, Bytes);
		return hook::Status::Installed;
	}
}
