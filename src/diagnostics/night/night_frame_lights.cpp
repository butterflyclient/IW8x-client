#include "diagnostics/night/night_frame_lights.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include <windows.h>
#include <cstring>
namespace night_frame_lights
{
	namespace
	{
		struct FramePatch
		{
			uintptr_t rva;
			unsigned char original[16], replacement[16];
			unsigned size;
		};
#include "diagnostics/night/night_frame_lights.inc"
		uintptr_t installedImage{};

		bool write(uintptr_t address, const void* bytes, unsigned count)
		{
			DWORD old{}, unused{};
			if (!VirtualProtect(reinterpret_cast<void*>(address), count, PAGE_EXECUTE_READWRITE, &old)) return false;
			std::memcpy(reinterpret_cast<void*>(address), bytes, count);
			const bool same = !std::memcmp(reinterpret_cast<void*>(address), bytes, count);
			const bool restored = VirtualProtect(reinterpret_cast<void*>(address), count, old, &unused) != 0;
			return same && restored;
		}
	}

	hook::Status Install(uintptr_t image)
	{
		if (!night_files::Enabled()) return hook::Status::Installed;
		if (installedImage) return installedImage == image ? hook::Status::Installed : hook::Status::Failed;
		// Runs before native frame allocation. Append both arrays to each frame;
		// retain all other offsets and the original allocation/freeing ownership.
		// Preflight every instruction before changing any code.
		for (const auto& p : kFramePatches)
		{
			unsigned char actual[16]{};
			SIZE_T count{};
			if (!p.size || p.size > sizeof(actual) ||
				!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(image + p.rva), actual, p.size, &count) ||
				count != p.size || std::memcmp(actual, p.original, p.size))
				return hook::Status::Failed;
		}
		unsigned changed{};
		for (const auto& p : kFramePatches)
		{
			++changed;
			if (!write(image + p.rva, p.replacement, p.size))
			{
				for (unsigned i = 0; i < changed; ++i)
					write(image + kFramePatches[i].rva, kFramePatches[i].original, kFramePatches[i].size);
				FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
				return hook::Status::Failed;
			}
		}
		FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
		installedImage = image;
		LOG_INFO("NightFrameLights",
			"native frames expanded to 0xD33280 bytes each; source and packed light arrays relocated with 8192 primary + 32 dynamic slots; matching GPU buffer capacity");
		return hook::Status::Installed;
	}
}
