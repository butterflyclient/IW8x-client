#pragma once
#include "engine/inline_hook.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
namespace gsc
{
	class ScriptFile
	{
	public:
		const char* name_; // 0x0000
		int compressed_len_; // 0x0008
		int len_; // 0x000C
		int bytecode_len_; // 0x0010
		// Public padding keeps this an offsetof-compatible standard-layout type.
		char pad_0014[0x0004]; // 0x0014
		const char* buffer_; // 0x0018
		std::uint8_t* bytecode_; // 0x0020
	};

	static_assert(std::is_standard_layout_v<ScriptFile>);
	static_assert(sizeof(ScriptFile) == 0x28);
	static_assert(offsetof(ScriptFile, name_) == 0x00);
	static_assert(offsetof(ScriptFile, compressed_len_) == 0x08);
	static_assert(offsetof(ScriptFile, len_) == 0x0C);
	static_assert(offsetof(ScriptFile, bytecode_len_) == 0x10);
	static_assert(offsetof(ScriptFile, buffer_) == 0x18);
	static_assert(offsetof(ScriptFile, bytecode_) == 0x20);

	[[nodiscard]] hook::Status Install(std::uintptr_t module_base);
	[[nodiscard]] hook::Status InstallReleaseHook(std::uintptr_t module_base);
	void Component_Load();
	void Component_Stage_Unpack();
	// Compatibility entry points: never mutate an already-linked ScriptFile.
	void reload_script_file(ScriptFile* script_file);
	void ReapplyCustomScript(const std::string& name);
#ifdef IW8X_ISLAND_PORT_PROBE
		// Read-only attribution for an actual linked Island compact-code address.
		[[nodiscard]] bool DescribeIslandCodeSite(std::uintptr_t pc, std::string& script, std::size_t& byte_offset);
#endif
}
