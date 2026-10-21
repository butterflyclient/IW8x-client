#include "client/weapons/weapon_recoil.h"
#include "client/weapons/weapon_recoil_policy.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
namespace weapon_recoil
{
	namespace
	{
#include "client/weapons/weapon_recoil_bridge.inc"
		struct Context
		{
			const void* ps;
			const void* weapon;
			uint64_t handOffset, alt;
			float ads, horizontal, vertical;
		};

		static_assert(sizeof(Context) == 48 && offsetof(Context, ads) == 32 && offsetof(Context, horizontal) == 36);
		using Enumerate = unsigned (*)(const void*, bool, const void**, unsigned char*, int);
		using Fn = void (*)();
		uintptr_t image{};
		bool installed{}, failed{};
		std::string installedSpec;
		std::map<std::string, Modifier, std::less<>> modifiers;
		std::set<std::string, std::less<>> weapons;

		template <class T>
		bool read(uintptr_t at, T& out) noexcept
		{
			return safemem::ReadBytes(reinterpret_cast<const void*>(at), &out, sizeof(out));
		}

		bool name_impl(uintptr_t at, char (&name)[256]) noexcept
		{
			uintptr_t ptr{};
			if (!read(at, ptr)) return false;
			const auto n = safemem::ReadString(reinterpret_cast<const char*>(ptr), name, sizeof(name));
			return n > 0 && n < 255;
		}

		void observe(Context* c) noexcept
		{
			if (!c || (c->handOffset != 0 && c->handOffset != 80)) return;
			// Native weapon identity/table bound: 550 entries precede WeaponDef table.
			// Scope the new semantics to admitted port roots, even when they share a
			// later attachment name with an existing stock weapon.
			std::array<unsigned char, 60> weapon{};
			if (!safemem::ReadBytes(c->weapon, weapon.data(), weapon.size())) return;
			uint16_t index{};
			std::memcpy(&index, weapon.data(), 2);
			if (index == 0 || index >= 550) return;
			uintptr_t complete{};
			char name[256]{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !name_impl(complete, name) || !weapons.contains(name)) return;
			uint32_t shots{};
			if (!read(reinterpret_cast<uintptr_t>(c->ps) + 0x540 + c->handOffset, shots)) return;
			std::array<const void*, 64> attachments{};
			std::array<unsigned char, 64> categories{};
			const auto count =
				reinterpret_cast<Enumerate>(image + enumRva)(weapon.data(), (c->alt & 255) != 0, attachments.data(), categories.data(), 0);
			if (count > attachments.size()) return;
			std::array<const Modifier*, 64> active{};
			for (unsigned i = 0; i < count; ++i)
			{
				char part[256]{};
				if (!attachments[i] || !name_impl(reinterpret_cast<uintptr_t>(attachments[i]), part)) continue;
				const auto at = modifiers.find(part);
				if (at != modifiers.end()) active[i] = &at->second;
			}
			Apply(std::span(active.data(), count), shots, c->ads, c->horizontal, c->vertical);
		}
	}

	hook::Status Install(uintptr_t base, std::string_view specification)
	{
		if (installed) return image == base && installedSpec == specification ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		std::array<unsigned char, sizeof(siteBytes)> site{};
		std::array<unsigned char, sizeof(enumBytes)> enumeration{};
		std::array<unsigned char, sizeof(contextBytes)> context{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + siteRva), site.data(), site.size()) ||
			!safemem::ReadBytes(reinterpret_cast<void*>(base + enumRva), enumeration.data(), enumeration.size()) ||
			!safemem::ReadBytes(reinterpret_cast<void*>(base + contextRva), context.data(), context.size()) ||
			std::memcmp(site.data(), siteBytes, site.size()) || std::memcmp(enumeration.data(), enumBytes, enumeration.size()) ||
			std::memcmp(context.data(), contextBytes, context.size()))
			return hook::Status::NotReady;
		try
		{
			const auto spec = nlohmann::json::parse(specification);
			std::map<std::string, Modifier, std::less<>> pending;
			std::set<std::string, std::less<>> admitted;
			for (const auto& row : spec.at("recoil_modifiers"))
			{
				auto name = row.at("name").get<std::string>();
				if (name.empty() || name.size() >= 255 || name.front() == ',' || name.find('\0') != name.npos) return hook::Status::Failed;
				const auto& encoded = row.at("words");
				if (!encoded.is_array() || encoded.size() != 10) return hook::Status::Failed;
				for (const auto& word : encoded)
				{
					if (!word.is_number_integer() || (!word.is_number_unsigned() && word.get<int64_t>() < 0) ||
						word.get<uint64_t>() > UINT32_MAX)
						return hook::Status::Failed;
				}
				const auto words = encoded.get<std::array<uint32_t, 10>>();
				Modifier m{};
				std::memcpy(&m, words.data(), sizeof(m));
				for (auto i : { 0, 1, 3, 4, 5, 6, 8, 9 })
				{
					float v;
					std::memcpy(&v, &words[i], 4);
					if (!std::isfinite(v)) return hook::Status::Failed;
				}
				if (!pending.emplace(name, m).second) return hook::Status::Failed;
			}
			for (const auto& row : spec.at("weapons"))
			{
				auto name = row.at("asset").get<std::string>();
				if (name.empty() || name.size() >= 255 || !name.ends_with("_mp") || name.find('\0') != name.npos ||
					!admitted.insert(name).second)
					return hook::Status::Failed;
			}
			if (pending.empty() || pending.size() > 20000 || admitted.empty() || admitted.size() > 256) return hook::Status::Failed;
			auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			if (!memory) return hook::Status::Failed;
			std::memcpy(memory, bridgeCode, sizeof(bridgeCode));
			auto callback = &observe;
			std::memcpy(memory + observerFixup, &callback, 8);
			// Keep the atomic original slot on a separate writable page until
			// MinHook publishes it. The executable page becomes RX before enable.
			auto slot = new (memory + trampolineOffset) std::atomic<Fn>{ nullptr };
			DWORD old{};
			if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old)) return hook::Status::Failed;
			image = base;
			modifiers = std::move(pending);
			weapons = std::move(admitted);
			installedSpec = specification;
			hook::Batch batch;
			batch.Add(reinterpret_cast<void*>(base + siteRva), reinterpret_cast<Fn>(memory), siteBytes, sizeof(siteBytes), *slot);
			FlushInstructionCache(GetCurrentProcess(), memory, 4096);
			if (batch.Commit() != hook::Status::Installed)
			{
				failed = true;
				return hook::Status::Failed;
			}
			installed = true;
			LOG_INFO("WeaponRecoil", "original later shot-threshold modifiers enabled for %u admitted weapon roots and %u parts",
				unsigned(weapons.size()), unsigned(modifiers.size()));
			return hook::Status::Installed;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("WeaponRecoil", "metadata admission failed: %s", e.what());
			return hook::Status::Failed;
		}
	}
}
