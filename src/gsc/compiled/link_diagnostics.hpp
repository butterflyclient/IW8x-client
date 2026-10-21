#pragma once
#include "gsc.hpp"

#include "engine/addresses.hpp"

#include <array>
#include <cstring>
namespace gsc::link_diagnostics
{
	using FindVariable = std::uint32_t (*)(void*, std::uint32_t, std::uint32_t);
	enum class ExportState
	{
		pending,
		present,
		missing
	};

	// The Replay linker (1316800) reads these same objects. Inspect child values
	// directly: EvaluateVariable/GetObject would change reference counts or create
	// objects, which diagnostics must never do.
	inline ExportState Inspect(void* context, std::uint32_t module, std::uint32_t function, FindVariable find)
	{
		using namespace game::addresses::gsc;
		const auto* bytes = static_cast<const std::uint8_t*>(context);
		const auto object = [&](std::size_t offset)
		{
			std::uint32_t value;
			std::memcpy(&value, bytes + offset, sizeof(value));
			return value;
		};
		const std::uint8_t* children;
		std::memcpy(&children, bytes + kChildrenOffset, sizeof(children));
		const auto loaded = find(context, object(kLoadedScriptsOffset), module);
		if (!(children[kChildStride * loaded + kChildTypeOffset] & kChildTypeMask)) return ExportState::pending;
		const auto file = find(context, object(kExportsOffset), module);
		if (!file || !(children[kChildStride * file + kChildTypeOffset] & kChildTypeMask)) return ExportState::missing;
		std::uint32_t exports;
		std::memcpy(&exports, children + kChildStride * file + kChildObjectOffset, sizeof(exports));
		return find(context, exports, function) ? ExportState::present : ExportState::missing;
	}

	struct Source
	{
		const ScriptFile* asset{};
		std::uintptr_t bytecode{};
		int bytecode_size{};
		std::array<char, 512> name{};
	};

	inline thread_local Source current_source;

	inline Source Begin(const ScriptFile* script)
	{
		const auto previous = current_source;
		current_source = {};
		current_source.asset = script;
		current_source.bytecode = reinterpret_cast<std::uintptr_t>(script->bytecode_);
		current_source.bytecode_size = script->bytecode_len_;
		if (script->name_) std::strncpy(current_source.name.data(), script->name_, current_source.name.size() - 1);
		return previous;
	}

	inline void End(const Source& previous)
	{
		current_source = previous;
	}

	void Install(std::uintptr_t base);
} // namespace gsc::link_diagnostics
