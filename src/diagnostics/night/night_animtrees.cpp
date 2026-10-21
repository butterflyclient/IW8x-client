#include "diagnostics/night/night_animtrees.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include "engine/addresses.hpp"
#include "engine/safemem.h"
#include <array>
#include <atomic>
#include <cstring>
#ifdef IW8X_ISLAND_PORT_PROBE
#include <cstdio>
#include <filesystem>
#include <vector>
#endif
namespace night_animtrees
{
	namespace
	{
		// Exact Replay 68fb1cbc: 1312138 calls this reader with context/name.
		// It finds RawFile type 51, rejects defaults, allocates in the native script
		// arena and expands/copies the file. Preserve that entire ownership path.
		constexpr uintptr_t ReaderRva = 0x13181D0;
		// Native collision registration owns this pointer, not the UI map dvar.
		// Same verified field used by the numbered-collision adapter (state+0x48).
		constexpr uintptr_t CollisionMapRva = 0x5CC7538;
		// Com_FrontEndScene_IsActive reads this field (state >= 5).
		constexpr uintptr_t FrontendStateRva = 0xC4EBEB0;
		constexpr std::array<std::uint8_t, 16> ReaderBytes{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
			0xEC, 0x20, 0x48 };
		using Reader = const char* (*)(void*, const char*);
		std::atomic<Reader> original{};
		uintptr_t imageBase{};

		bool map_impl(std::array<char, 128>& name)
		{
			if (!night_files::Enabled()) return false;
			// Preloading a selected map changes the collision name while the stock
			// frontend server still precaches its own animation trees.
			int frontend_state{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(imageBase + FrontendStateRva), &frontend_state, sizeof(frontend_state)) ||
				frontend_state >= 5) return false;
			const char* map{};
			return safemem::ReadBytes(reinterpret_cast<void*>(imageBase + CollisionMapRva), &map, sizeof(map)) && map &&
				safemem::ReadString(map, name.data(), name.size()) && night_files::MapEnabled(name.data());
		}

		const char* read(void* context, const char* name)
		{
			const char* selected = name;
			std::array<char, 128> map{};
			if (name && map_impl(map))
			{
				if (const auto* source = night_files::animtree_source(map.data(), name))
				{
					selected = source;
					LOG_INFO("NightAnimTrees", "reading original tree '%s' for %s through private RawFile", name, map.data());
				}
			}
			// Missing private source still fails normally. Do not silently fall back to
			// a stock tree, manufacture animation indices, or suppress parser failures.
			const auto* result = original.load(std::memory_order_acquire)(context, selected);
#ifdef IW8X_ISLAND_PORT_PROBE
			char capture[2]{};
			if (result && name && map[0] && GetEnvironmentVariableA("IW8X_ISLAND_CAPTURE_TREES", capture, sizeof(capture)) == 1 && capture[0] == '1')
			{
				std::vector<char> text(4 * 1024 * 1024);
				if (safemem::ReadString(result, text.data(), text.size()))
				{
					std::error_code error;
					const auto directory = std::filesystem::path("magma/island-probe-trees") / std::to_string(GetCurrentProcessId());
					std::filesystem::create_directories(directory, error);
					if (!error)
					{
						const auto path = directory / std::filesystem::path(name).filename();
						std::FILE* file{};
						if (fopen_s(&file, path.string().c_str(), "wb") == 0 && file)
						{
							std::fwrite(text.data(), 1, std::strlen(text.data()), file);
							std::fclose(file);
						}
					}
				}
			}
#endif
			return result;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (!night_files::Enabled()) return hook::Status::Installed;
		if (original.load(std::memory_order_acquire)) return imageBase == base ? hook::Status::Installed : hook::Status::Failed;
		std::array<std::uint8_t, sizeof(game::kIsFrontEndPrologue)> frontend_bytes{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kIsFrontEndRVA), frontend_bytes.data(), frontend_bytes.size()) ||
			std::memcmp(frontend_bytes.data(), game::kIsFrontEndPrologue, frontend_bytes.size())) return hook::Status::Failed;

		// Pin both the native registration write and unload clear of the scope slot.
		struct Guard
		{
			uintptr_t rva;
			std::array<std::uint8_t, 7> bytes;
		};

		constexpr Guard guards[]{
			{ 0xD6B430, { 0x48, 0x89, 0x0D, 0x01, 0xC1, 0xF5, 0x04 } },
			{ 0xD6B9DE, { 0x48, 0x89, 0x1D, 0x53, 0xBB, 0xF5, 0x04 } },
		};
		for (const auto& guard : guards)
		{
			std::array<std::uint8_t, 7> bytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + guard.rva), bytes.data(), bytes.size()) || bytes != guard.bytes)
				return hook::Status::Failed;
		}
		imageBase = base;
		const auto status =
			hook::Install(reinterpret_cast<void*>(base + ReaderRva), &read, ReaderBytes.data(), ReaderBytes.size(), original);
		if (status == hook::Status::Installed)
			LOG_INFO("NightAnimTrees", "private original trees selected for registered, admitted gameplay maps; stock frontend trees retained");
		return status;
	}
}
