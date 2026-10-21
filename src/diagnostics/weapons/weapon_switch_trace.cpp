#include "diagnostics/weapons/weapon_switch_trace.h"
#include "diagnostics/weapons/weapon_files.h"
#include "common/console.h"
#include <array>
#include <cstring>
#include <new>
namespace weapon_switch_trace
{
	namespace
	{
		struct Site
		{
			uintptr_t rva;
			std::array<unsigned char, 5> bytes;
		};
#include "diagnostics/weapons/weapon_switch_trace.inc"
		using Fn = void (*)();
		uintptr_t image{};
		bool installed = false, failed = false;
		std::atomic<unsigned> records{};

		bool read(uintptr_t at, void* out, size_t n)
		{
			SIZE_T done{};
			return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, n, &done) && done == n;
		}

		void observe(unsigned site, uintptr_t psRegister, uintptr_t pm, uintptr_t stack) noexcept
		{
			if (site >= std::size(sites) || pm != image + 0xF430A20 || records.load() >= 128) return;
			uintptr_t ps{}, weapon{};
			uint16_t index{};
			if (!read(pm + 8, &ps, 8) || !read(stack + 0x68, &weapon, 8) || !read(weapon, &index, 2) || (index != 169 && index != 170))
				return;

			struct Previous
			{
				unsigned site{};
				uintptr_t ps{}, reg{}, stack{};
			};

			static thread_local Previous prev;
			if (site == 0 || prev.stack != stack || prev.ps != ps || prev.reg != psRegister || site == std::size(sites) - 1)
			{
				if (records.fetch_add(1) < 128)
					LOG_INFO("WeaponSwitchTrace", "before=%llX previous=%llX index=%u ps=%p reg=%p previous_ps=%p previous_reg=%p stack=%p",
						sites[site].rva, sites[prev.site].rva, unsigned(index), reinterpret_cast<void*>(ps),
						reinterpret_cast<void*>(psRegister), reinterpret_cast<void*>(prev.ps), reinterpret_cast<void*>(prev.reg),
						reinterpret_cast<void*>(stack));
			}
			prev = { site, ps, psRegister, stack };
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (!weapon_files::Enabled() || installed) return hook::Status::Installed;
		if (failed) return hook::Status::Failed;
		// No partial observation: verify every five-byte native call before preparing.
		for (const auto& site : sites)
		{
			std::array<unsigned char, 5> bytes{};
			if (!read(base + site.rva, bytes.data(), bytes.size()) || bytes != site.bytes)
			{
				failed = true;
				return hook::Status::Failed;
			}
		}
		image = base;
		auto memory =
			static_cast<unsigned char*>(VirtualAlloc(nullptr, std::size(sites) * 256, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		if (!memory)
		{
			failed = true;
			return hook::Status::Failed;
		}
		hook::Batch batch;
		for (unsigned i = 0; i < std::size(sites); ++i)
		{
			auto code = memory + i * 256;
			std::memcpy(code, observerCode, sizeof(observerCode));
			const auto observer = &observe;
			std::memcpy(code + siteFixup, &i, 4);
			std::memcpy(code + observerFixup, &observer, 8);
			auto slot = new (code + 0xF0) std::atomic<Fn>{ nullptr };
			batch.Add(reinterpret_cast<void*>(base + sites[i].rva), reinterpret_cast<Fn>(code), sites[i].bytes.data(), 5, *slot);
		}
		FlushInstructionCache(GetCurrentProcess(), memory, std::size(sites) * 256);
		if (batch.Commit() != hook::Status::Installed)
		{
			failed = true;
			return hook::Status::Failed;
		}
		DWORD old{};
		if (!VirtualProtect(memory, std::size(sites) * 256, PAGE_EXECUTE_READ, &old))
		{
			failed = true;
			return hook::Status::Failed;
		}
		installed = true;
		LOG_INFO("WeaponSwitchTrace", "bounded original switch trace installed (%u call sites); game state unchanged",
			unsigned(std::size(sites)));
		return hook::Status::Installed;
	}
}
