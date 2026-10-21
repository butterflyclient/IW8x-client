#include "dedicated/assets/infil_transients.h"
#include "common/console.h"
#include "engine/safemem.h"
#include <array>
#include <atomic>
#include <cstring>
namespace dedicated::infil_transients
{
	namespace
	{
		// Exact Replay 1.20.4 bindings. The stock processor reads CG omnvars for each
		// local player. A headless host has no CG player, but still executes the GSC
		// camera animations and must stream the XAnimParts requested by its SV omnvar.
		// Evidence: br_victory_transients_native.json and br_victory_animation_assets.jsonl.
		struct Binding
		{
			uintptr_t rva;
			std::array<unsigned char, 16> bytes;
		};

		constexpr Binding bindings[] = {
			{ 0xD6DC20, { 0x48, 0x83, 0xEC, 0x38, 0xB9, 0x91, 0, 0, 0, 0xE8, 0x52, 0xAD, 0x35, 0, 0x84, 0xC0 } },
			{ 0xD6D780, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B } },
			{ 0xD38DF0, { 0x8B, 0x05, 0x76, 0xB8, 0xF6, 0x04, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC } },
			{ 0xD38E00, { 0x8B, 0xC1, 0x48, 0x8D, 0x0D, 0x5B, 0xB5, 0xF6, 0x04, 0x48, 0xC1, 0xE0, 0x06, 0x48, 0x03, 0xC1 } },
			{ 0xD38E20, { 0x8B, 0xC1, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0xAB, 0xB4, 0xF6, 0x04, 0x8B, 0x04, 0x88 } },
			{ 0xD38EE0, { 0x8B, 0xC1, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0xEF, 0xB3, 0xF6, 0x04, 0x0F, 0xB6, 0x04 } },
			{ 0xFC5110, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49 } },
			{ 0x15F6E50, { 0x0F, 0xB7, 0xC1, 0x48, 0x8D, 0x0D, 0x16, 0x66, 0x9C, 0x0D, 0x48, 0xC1, 0xE0, 0x05, 0xF6, 0x04 } },
			{ 0x15F94D0, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB7, 0xD9, 0xE8, 0x42, 0xDE, 0xFF, 0xFF, 0x48, 0x8D } },
			{ 0x12B0250, { 0x48, 0x8B, 0x05, 0x59, 0x5D, 0x0D, 0x0C, 0x48, 0x85, 0xC0, 0x74, 0x09, 0x80, 0x78, 0x28, 0x00 } },
			{ 0xCD5830, { 0x8B, 0xC1, 0x48, 0x8D, 0x0C, 0x80, 0x48, 0x8D, 0x05, 0x43, 0x2D, 0xF7, 0x04, 0x48, 0x8D, 0x04 } },
		};

		struct FileState
		{
			uint16_t index;
			bool requested;
			bool loaded;
		};

		struct InfilState
		{
			unsigned counts[2];
			char map[64];
			FileState files[12];
		};

		static_assert(sizeof(InfilState) == 0x78 && offsetof(InfilState, files) == 0x48);
		uintptr_t image = 0;
		using ProcessFn = void (*)();
		std::atomic<ProcessFn> original{ nullptr };

		template <class T>
		T fn(uintptr_t rva)
		{
			return reinterpret_cast<T>(image + rva);
		}

		void process()
		{
			auto& state = *reinterpret_cast<InfilState*>(image + 0x5CE9A20);
			const auto count = fn<unsigned (*)()>(0xD38DF0)();
			// On map teardown retain stock ownership/unload. Never carry a request from
			// the previous BG table into another map or consult unallocated SV omnvars.
			uintptr_t svOmnvars = 0;
			if (!fn<bool (*)()>(0x12B0250)() || !count || count > 12 ||
				strncmp(state.map, reinterpret_cast<const char*>(image + 0x5CA4291), 64) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(image + 0xBC216C8), &svOmnvars, sizeof(svOmnvars)) || !svOmnvars)
			{
				original.load(std::memory_order_acquire)();
				return;
			}
			for (unsigned i = 0; i < count; ++i)
			{
				const auto kind = fn<unsigned char (*)(unsigned)>(0xD38EE0)(i);
				const auto index = fn<unsigned (*)(unsigned)>(0xD38E20)(i);
				// BG can expose its file names before map omnvar registration finishes.
				if (kind > 1 || index == 0xFFFFFFFFu) continue;
				const auto* def = fn<const unsigned char* (*)(unsigned)>(0xCD5830)(index);
				// loadinfiltransient owns a global BOOL, never a per-client/archive value.
				if (def[38] != 0 || (def[37] & 6)) continue;
				const auto* data = fn<const unsigned char* (*)(unsigned, int, void*)>(0xFC5110)(index, -1, nullptr);
				if (!data) continue;
				auto& file = state.files[i];
				if (data[4] && !file.requested)
				{
					fn<void (*)(unsigned)>(0xD6D780)(i); // native name lookup, refcount and priority
					LOG_INFO("BRAssets", "server requested %s (file=%u)", fn<const char* (*)(unsigned)>(0xD38E00)(i), file.index);
				}
				else if (!data[4] && file.requested)
				{
					fn<void (*)(uint16_t)>(0x15F94D0)(file.index);
					if (state.counts[kind]) --state.counts[kind];
					file = {};
				}
				if (file.requested && !file.loaded && fn<bool (*)(uint16_t)>(0x15F6E50)(file.index))
				{
					file.loaded = true;
					LOG_INFO("BRAssets", "server loaded %s", fn<const char* (*)(unsigned)>(0xD38E00)(i));
				}
			}
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (original.load(std::memory_order_acquire)) return hook::Status::Installed;
		for (const auto& binding : bindings)
		{
			unsigned char actual[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), actual, sizeof(actual)) ||
				memcmp(actual, binding.bytes.data(), sizeof(actual)))
			{
				LOG_ERR("BRAssets", "binding mismatch at Replay RVA 0x%llX", binding.rva);
				return hook::Status::NotReady;
			}
		}
		image = base;
		const auto status = hook::Install(
			reinterpret_cast<void*>(base + bindings[0].rva), &process, bindings[0].bytes.data(), bindings[0].bytes.size(), original);
		if (status == hook::Status::Installed) LOG_INFO("BRAssets", "dedicated cinematic streaming reads authoritative server requests");
		return status;
	}
}
