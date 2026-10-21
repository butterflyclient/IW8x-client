#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace runtime_signatures
{
	struct report
	{
		size_t patterns = 0, resolved = 0;
		uint64_t elapsed_us = 0;
		std::string failure;
	};
	// Tests and startup share the exact scanner. No reads from live guard pages.
	bool scan(std::span<const uint8_t> file, report& output);
	bool initialize(const wchar_t* path);
	bool ready() noexcept;
	bool contains(uintptr_t rva) noexcept;
	size_t count() noexcept;
}
