#pragma once
#include "engine/safemem.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace game::imports
{
	namespace detail
	{
		struct mapped_image
		{
			uintptr_t base;
			size_t size;

			bool contains(size_t rva, size_t length) const noexcept
			{
				return rva <= size && length <= size - rva && base <= UINTPTR_MAX - rva &&
					base + rva <= UINTPTR_MAX - length;
			}

			template <class T>
			bool read(size_t rva, T& value) const noexcept
			{
				return contains(rva, sizeof(value)) && safemem::ReadBytes(reinterpret_cast<const void*>(base + rva), &value, sizeof(value));
			}

			bool matches(size_t rva, std::string_view expected, bool ignore_case) const noexcept
			{
				if (expected.empty() || expected.size() > 255 || !contains(rva, expected.size() + 1)) return false;
				const auto fold = [](unsigned char value)
				{
					return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
				};
				for (size_t index = 0; index <= expected.size(); ++index)
				{
					unsigned char actual{};
					if (!read(rva + index, actual)) return false;
					if (index == expected.size()) return actual == 0;
					const auto wanted = static_cast<unsigned char>(expected[index]);
					if ((ignore_case ? fold(actual) : actual) != (ignore_case ? fold(wanted) : wanted)) return false;
				}
				return false;
			}
		};
	}

	// Loaded PE64 only: names come from OriginalFirstThunk, never from resolved
	// function pointers. Every descriptor, name and returned IAT slot is bounded
	// by the published image size and checked before reading mapped memory.
	inline void** find(uintptr_t base, size_t image_size, std::string_view library, std::string_view function) noexcept
	{
		const detail::mapped_image image{ base, image_size };
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS64 nt{};
		if (!base || !image.read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
			!image.read(static_cast<size_t>(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
			nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) || nt.OptionalHeader.SizeOfImage != image_size ||
			nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
		{
			return nullptr;
		}
		const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
			!image.contains(directory.VirtualAddress, directory.Size)) return nullptr;
		const auto descriptor_count = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
		if (descriptor_count > 1024) return nullptr;
		for (size_t index = 0; index < descriptor_count; ++index)
		{
			IMAGE_IMPORT_DESCRIPTOR descriptor{};
			if (!image.read(directory.VirtualAddress + index * sizeof(descriptor), descriptor)) return nullptr;
			if (!descriptor.Name && !descriptor.FirstThunk && !descriptor.OriginalFirstThunk) return nullptr;
			if (!descriptor.Name || !image.matches(descriptor.Name, library, true)) continue;
			if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk) return nullptr;
			const auto remaining_names = image_size - (std::min)(image_size, size_t(descriptor.OriginalFirstThunk));
			const auto thunk_count = (std::min)(remaining_names / sizeof(IMAGE_THUNK_DATA64), size_t{ 65536 });
			for (size_t thunk_index = 0; thunk_index < thunk_count; ++thunk_index)
			{
				IMAGE_THUNK_DATA64 name{}, slot{};
				const auto name_rva = size_t(descriptor.OriginalFirstThunk) + thunk_index * sizeof(name);
				const auto slot_rva = size_t(descriptor.FirstThunk) + thunk_index * sizeof(slot);
				if (!image.read(name_rva, name) || !image.read(slot_rva, slot) || !name.u1.AddressOfData) return nullptr;
				if (IMAGE_SNAP_BY_ORDINAL64(name.u1.Ordinal)) continue;
				if (name.u1.AddressOfData >= image_size ||
					!image.matches(size_t(name.u1.AddressOfData) + offsetof(IMAGE_IMPORT_BY_NAME, Name), function, false)) continue;
				const auto address = base + slot_rva;
				if (address % alignof(void*)) return nullptr;
				return reinterpret_cast<void**>(address);
			}
			return nullptr;
		}
		return nullptr;
	}
}

namespace system_runtime
{
	// Bootstrap supplies the handler to serialize pre_destroy with its startup
	// stage lock. Normal host exit invokes it before forwarding ExitProcess.
	void set_shutdown_handler(void (*handler)()) noexcept;
}
