#include "diagnostics/night/night_script_range.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include "network/native_network.h"
#include "engine/safemem.h"
#include <windows.h>
#include <array>
#include <cstring>
namespace night_script_range
{
	namespace
	{
		// Exact original 1.28 RVA 2241CB0 chooses 0x1194D for current scripts and
		// 0xE2C0 for legacy scripts. Replay embeds the latter at these two sites.
		// The native child-variable writer stores 24-bit names, independently tested
		// across the Night fixed fields and both canonical/dynamic boundaries.
		// Evidence: tools/port120/verify_night_canonical_range45.py.
		constexpr uint32_t kSourceLimit = 72013;
		constexpr uintptr_t kCanonicalBuffer = 0xD76E608;

		struct Patch
		{
			uintptr_t rva;
			std::array<uint8_t, 6> original;
		};

		constexpr Patch kPatches[] = {
			{ 0x1316CFC, { 0x8D, 0x87, 0xC0, 0xE2, 0, 0 } }, // dynamic intern result
			{ 0x13227BE, { 0x81, 0xF9, 0xC0, 0xE2, 0, 0 } }, // fixed-field linker bound
		};
		bool g_installed = false;

		bool write(void* at, const void* data, size_t size)
		{
			DWORD old = 0;
			if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
			const bool copied = safemem::WriteBytes(at, data, size);
			const bool flushed = FlushInstructionCache(GetCurrentProcess(), at, size) != FALSE;
			DWORD ignored = 0;
			const bool restored = VirtualProtect(at, size, old, &ignored) != FALSE;
			return copied && flushed && restored;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed) return hook::Status::Installed;
		wchar_t config[MAX_PATH]{};
		if (!native_network::ConfigurationPath(config, MAX_PATH)) return hook::Status::Failed;
			if (!night_files::MapEnabled("mp_escape2") && !night_files::MapEnabled("mp_kstenod"))
			return hook::Status::Installed;

		uintptr_t canonical = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + kCanonicalBuffer), &canonical, sizeof(canonical)))
			return hook::Status::NotReady;
		if (canonical)
		{
			LOG_ERR("NightScriptRange", "refusing to change canonical IDs after script initialization");
			return hook::Status::Failed;
		}
		for (const auto& patch : kPatches)
		{
			std::array<uint8_t, 6> actual{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + patch.rva), actual.data(), actual.size())) return hook::Status::NotReady;
			if (actual != patch.original)
			{
				LOG_ERR("NightScriptRange", "native instruction mismatch at RVA %llX", static_cast<unsigned long long>(patch.rva));
				return hook::Status::Failed;
			}
		}
		size_t applied = 0;
		for (const auto& patch : kPatches)
		{
			auto replacement = patch.original;
			std::memcpy(replacement.data() + 2, &kSourceLimit, sizeof(kSourceLimit));
			if (!write(reinterpret_cast<void*>(base + patch.rva), replacement.data(), replacement.size()))
			{
				// Include the failed write: it may have copied before a flush or
				// protection restoration failed. Startup stops even if rollback works.
				for (size_t i = 0; i <= applied; ++i)
					if (!write(reinterpret_cast<void*>(base + kPatches[i].rva), kPatches[i].original.data(), kPatches[i].original.size()))
						LOG_ERR("NightScriptRange", "instruction rollback failed at RVA %llX",
							static_cast<unsigned long long>(kPatches[i].rva));
				return hook::Status::Failed;
			}
			++applied;
		}
		g_installed = true;
		LOG_INFO("NightScriptRange", "original 1.28 fixed IDs preserved through %u; dynamic strings start above this range", kSourceLimit);
		return hook::Status::Installed;
	}
}
