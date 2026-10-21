#pragma once
#include <windows.h>

#include <string_view>
namespace embedded_assets
{
	// Resource bytes remain valid for the lifetime of the loaded proxy module.
	std::string_view Find(std::string_view path);
	// The native TACT reader opens config/index files by name, before normal proxy
	// startup. Restore missing metadata at process attach; never replace user files.
	bool PrepareNativeMetadata(HMODULE module) noexcept;
}
