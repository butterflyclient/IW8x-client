#include "client/weapons/weapon_lowammo.h"
#include "client/weapons/weapon_lowammo_policy.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <array>
#include <map>
#include <set>
#include <cstring>
#include <bit>
#include <new>
#include <string>
namespace weapon_lowammo
{
	namespace
	{
		struct Site
		{
			uintptr_t rva, contextRva;
			const unsigned char* bytes;
			size_t size;
			const unsigned char* context;
			size_t contextSize;
			const unsigned char* code;
			size_t codeSize;
			size_t observerFixup;
		};
#include "client/weapons/weapon_lowammo_bridge.inc"
		struct Context
		{
			const void* weapon;
			uintptr_t selector;
			uint32_t value;
			int32_t clip;
			uint32_t mode, updated;
		};

		static_assert(sizeof(Context) == 32 && offsetof(Context, value) == 16 && offsetof(Context, updated) == 28);
		using Enumerate = unsigned (*)(const void*, bool, const void**, unsigned char*, int);
		using IsAlt = bool (*)(const void*);
		using Fn = void (*)();
		uintptr_t image{};
		bool installed{}, failed{};
		std::string installedSpec;
		std::map<std::string, int32_t, std::less<>> modifiers;
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
			if (!c || c->mode > 2) return;
			std::array<unsigned char, 60> weapon{};
			if (!safemem::ReadBytes(c->weapon, weapon.data(), weapon.size())) return;
			uint16_t index{};
			std::memcpy(&index, weapon.data(), 2);
			if (!index || index >= 550) return;
			uintptr_t complete{}, definition{};
			char name[256]{};
			int32_t originalDefault{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !name_impl(complete, name) || !weapons.contains(name) ||
				!read(image + 0xC6ED9A0 + index * 8, definition) || !read(definition + 0x3E4, originalDefault))
				return;
			const bool alt = c->mode == 2 ? reinterpret_cast<IsAlt>(image + altRva)(reinterpret_cast<const void*>(c->selector))
										  : (c->selector & 255) != 0;
			std::array<const void*, 64> parts{};
			std::array<unsigned char, 64> categories{};
			const auto count = reinterpret_cast<Enumerate>(image + enumerationRva)(weapon.data(), alt, parts.data(), categories.data(), 0);
			if (count > parts.size()) return;
			std::array<const int32_t*, 64> active{};
			for (unsigned i = 0; i < count; ++i)
			{
				char part[256]{};
				if (!parts[i] || !name_impl(reinterpret_cast<uintptr_t>(parts[i]), part)) continue;
				const auto found = modifiers.find(part);
				if (found != modifiers.end()) active[i] = &found->second;
			}
			auto selected = originalDefault;
			for (unsigned i = 0; i < count; ++i)
				if (active[i] && *active[i] >= 0)
				{
					selected = *active[i];
					break;
				}
			// Opting this converted package into low-ammo compatibility enables the
			// later supported-mode policy for its roots. Native stock roots and all
			// existing warning eligibility gates remain unchanged.
			if (c->mode == 0)
			{
				if (selected > 0) c->value = std::bit_cast<uint32_t>(selected);
			}
			else if (c->mode == 1)
			{
				const auto fallback = _mm_cvtt_ss2si(_mm_set_ss(std::bit_cast<float>(c->value)));
				c->value = std::bit_cast<uint32_t>(static_cast<float>(selected > 0 ? selected : fallback));
			}
			else
			{
				const auto warning = Threshold(originalDefault, std::bit_cast<float>(c->value), c->clip, std::span(active.data(), count));
				c->value = std::bit_cast<uint32_t>(HudFraction(warning, c->clip));
			}
			c->updated = 1;
		}

		bool matches(uintptr_t at, const unsigned char* bytes, size_t length)
		{
			std::vector<unsigned char> actual(length);
			return safemem::ReadBytes(reinterpret_cast<void*>(at), actual.data(), length) && !std::memcmp(actual.data(), bytes, length);
		}
	}

	hook::Status Install(uintptr_t base, std::string_view specification)
	{
		if (installed) return image == base && installedSpec == specification ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		for (const auto& site : sites)
			if (!matches(base + site.rva, site.bytes, site.size) || !matches(base + site.contextRva, site.context, site.contextSize))
				return hook::Status::NotReady;
		if (!matches(base + enumerationRva, enumerationBytes, sizeof(enumerationBytes)) ||
			!matches(base + altRva, altBytes, sizeof(altBytes)))
			return hook::Status::NotReady;
		try
		{
			const auto spec = nlohmann::json::parse(specification);
			std::map<std::string, int32_t, std::less<>> pending;
			std::set<std::string, std::less<>> admitted;
			for (const auto& row : spec.at("lowammo_modifiers"))
			{
				auto name = row.at("name").get<std::string>();
				const auto& words = row.at("words");
				if (name.empty() || name.size() >= 255 || name.front() == ',' || name.find('\0') != name.npos || !words.is_array() ||
					words.size() != 1)
					return hook::Status::Failed;
				const auto& word = words.at(0);
				if (!word.is_number_integer() || (!word.is_number_unsigned() && word.get<int64_t>() < 0) ||
					word.get<uint64_t>() > UINT32_MAX)
					return hook::Status::Failed;
				if (!pending.emplace(name, std::bit_cast<int32_t>(word.get<uint32_t>())).second) return hook::Status::Failed;
			}
			for (const auto& row : spec.at("weapons"))
			{
				auto name = row.at("asset").get<std::string>();
				if (name.empty() || name.size() >= 255 || name.front() == ',' || !name.ends_with("_mp") || name.find('\0') != name.npos ||
					!admitted.insert(name).second)
					return hook::Status::Failed;
			}
			if (pending.empty() || pending.size() > 20000 || admitted.empty() || admitted.size() > 256) return hook::Status::Failed;
			constexpr size_t stride = 8192;
			auto memory =
				static_cast<unsigned char*>(VirtualAlloc(nullptr, std::size(sites) * stride, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			if (!memory) return hook::Status::Failed;
			image = base;
			modifiers = std::move(pending);
			weapons = std::move(admitted);
			installedSpec = specification;
			hook::Batch batch;
			for (size_t i = 0; i < std::size(sites); ++i)
			{
				const auto& site = sites[i];
				auto code = memory + i * stride;
				std::memcpy(code, site.code, site.codeSize);
				auto callback = &observe;
				std::memcpy(code + site.observerFixup, &callback, 8);
				auto slot = new (code + 4096) std::atomic<Fn>{ nullptr };
				DWORD old{};
				if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old))
				{
					failed = true;
					return hook::Status::Failed;
				}
				FlushInstructionCache(GetCurrentProcess(), code, 4096);
				batch.Add(reinterpret_cast<void*>(base + site.rva), reinterpret_cast<Fn>(code), site.bytes, site.size, *slot);
			}
			if (batch.Commit() != hook::Status::Installed)
			{
				failed = true;
				return hook::Status::Failed;
			}
			installed = true;
			LOG_INFO("WeaponLowAmmo", "later warning thresholds enabled for %u admitted roots and %u parts", unsigned(weapons.size()),
				unsigned(modifiers.size()));
			return hook::Status::Installed;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("WeaponLowAmmo", "metadata admission failed: %s", e.what());
			return hook::Status::Failed;
		}
	}
}
