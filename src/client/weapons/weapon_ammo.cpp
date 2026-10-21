#include "client/weapons/weapon_ammo.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <array>
#include <cstring>
#include <vector>
namespace weapon_ammo
{
	namespace
	{
		struct Site
		{
			uintptr_t rva, taken, next;
			unsigned char field;
			bool usesR8;
			std::array<unsigned char, 12> bytes;
		};
#include "client/weapons/weapon_ammo_sites.inc"
		using Fn = void (*)();
		std::array<std::atomic<Fn>, std::size(sites)> originals{};
		uintptr_t installedImage{};
		bool failed{};

		void jump(std::vector<unsigned char>& code, uintptr_t target)
		{
			code.insert(code.end(), { 0xFF, 0x25, 0, 0, 0, 0 });
			const auto at = code.size();
			code.resize(at + 8);
			std::memcpy(code.data() + at, &target, 8);
		}

		std::vector<unsigned char> bridge(const Site& site, uintptr_t image)
		{
			// Replay's pointer load and test are unchanged. Neither branch calls C++
			// or changes the stack, nonvolatile registers, SIMD registers or ammo.
			std::vector<unsigned char> code(site.bytes.begin(), site.bytes.begin() + 10);
			code.insert(code.end(), { 0x74, 0 }); // null -> original next-attachment path
			const auto nullDisplacement = code.size() - 1;
			if (site.usesR8)
				code.insert(code.end(), { 0x41, 0x83, 0x78, site.field, 0 });
			else
				code.insert(code.end(), { 0x83, 0x7A, site.field, 0 });
			code.insert(code.end(), { 0x78, 0 }); // negative -> same next-attachment path
			const auto negativeDisplacement = code.size() - 1;
			jump(code, image + site.taken);
			const auto fall = code.size();
			code[nullDisplacement] = static_cast<unsigned char>(fall - nullDisplacement - 1);
			code[negativeDisplacement] = static_cast<unsigned char>(fall - negativeDisplacement - 1);
			jump(code, image + site.next);
			return code;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (installedImage) return installedImage == base ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		for (const auto& site : sites)
		{
			std::array<unsigned char, 12> bytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + site.rva), bytes.data(), bytes.size()) || bytes != site.bytes)
				return hook::Status::NotReady;
		}
		constexpr size_t stride = 64;
		auto memory =
			static_cast<unsigned char*>(VirtualAlloc(nullptr, std::size(sites) * stride, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!memory) return hook::Status::Failed;
		for (size_t i = 0; i < std::size(sites); ++i)
		{
			const auto code = bridge(sites[i], base);
			if (code.size() > stride)
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return hook::Status::Failed;
			}
			std::memcpy(memory + i * stride, code.data(), code.size());
		}
		DWORD old{};
		if (!VirtualProtect(memory, std::size(sites) * stride, PAGE_EXECUTE_READ, &old) ||
			!FlushInstructionCache(GetCurrentProcess(), memory, std::size(sites) * stride))
		{
			VirtualFree(memory, 0, MEM_RELEASE);
			return hook::Status::Failed;
		}
		hook::Batch batch;
		for (size_t i = 0; i < std::size(sites); ++i)
			batch.Add(reinterpret_cast<void*>(base + sites[i].rva), reinterpret_cast<Fn>(memory + i * stride), sites[i].bytes.data(),
				sites[i].bytes.size(), originals[i]);
		const auto status = batch.Commit();
		// Retain executable backing even on failure: batch rollback may have had
		// an already executing detour. Never retry a partially attempted batch.
		if (status != hook::Status::Installed)
		{
			failed = true;
			return status;
		}
		installedImage = base;
		LOG_INFO("WeaponAmmo", "six native ammo getter loops accept negative inheritance; stock positive values preserved");
		return status;
	}
}
