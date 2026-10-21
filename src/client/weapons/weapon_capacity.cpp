#include "client/weapons/weapon_capacity.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace weapon_capacity
{
	namespace
	{
#include "client/weapons/weapon_capacity_bindings.inc"
		using Capacity = int32_t (*)(const void*, bool);
		using Enumerate = unsigned (*)(const void*, bool, const void**, unsigned char*, int);
		using Predicate = bool (*)(const void*);
		std::atomic<Capacity> original{};
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

		int rank(unsigned char category, bool priority) noexcept
		{
			constexpr unsigned char normal[]{ 4, 9, 11, 12, 13, 8, 7, 6, 5, 10, 3, 2, 1, 0 };
			constexpr unsigned char special[]{ 4, 3, 9, 11, 12, 13, 8, 7, 6, 5, 10, 2, 1, 0 };
			category = std::min<unsigned char>(category, 13);
			const auto order = priority ? special : normal;
			for (int i = 0; i < 14; ++i)
				if (order[i] == category) return i;
			return 14;
		}

		bool calculate(const void* argument, bool alt, int32_t& result) noexcept
		{
			std::array<unsigned char, 60> weapon{};
			if (!safemem::ReadBytes(argument, weapon.data(), weapon.size())) return false;
			uint16_t index{};
			std::memcpy(&index, weapon.data(), 2);
			if (!index || index >= 550) return false;
			uintptr_t complete{}, definition{};
			char root[256]{};
			int32_t fallback{};
			unsigned char priority{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !name_impl(complete, root) || !weapons.contains(root) ||
				!read(image + 0xC6ED9A0 + index * 8, definition) || !read(definition + 0x3C0, fallback) ||
				!read(definition + 0x3DE, priority) || priority > 1)
				return false;
			if (reinterpret_cast<Capacity>(image + classRva)(weapon.data(), alt) == 8 &&
				reinterpret_cast<Capacity>(image + firetypeRva)(weapon.data(), alt) == 5)
			{
				result = 1;
				return true;
			}
			std::array<const void*, 64> parts{};
			std::array<unsigned char, 64> categories{};
			const auto count = reinterpret_cast<Enumerate>(image + enumerationRva)(weapon.data(), alt, parts.data(), categories.data(), 0);
			if (count > parts.size()) return false;

			struct Part
			{
				bool present{};
				int32_t capacity{};
				int32_t additive{};
			};

			std::array<Part, 64> values{};
			std::array<unsigned, 64> order{};
			uint32_t delta{};
			bool leading{};
			for (unsigned i = 0; i < count; ++i)
			{
				if (!parts[i] || categories[i] > 28) return false;
				const auto address = reinterpret_cast<uintptr_t>(parts[i]);
				uintptr_t child{};
				if (!read(address + 0x130, child)) return false;
				values[i].present = child != 0;
				if (child && !read(child + 0xC, values[i].capacity)) return false;
				char name[256]{};
				if (!name_impl(address, name)) return false;
				const auto found = modifiers.find(name);
				if (found != modifiers.end())
				{
					if (!child) return false;
					values[i].additive = found->second;
				}
				delta += std::bit_cast<uint32_t>(values[i].additive);
				order[i] = i;
				if (i == 0 && categories[i] == 10 && alt)
				{
					uint32_t type{};
					if (!read(address + 0x28, type)) return false;
					leading = type != 14 && reinterpret_cast<Predicate>(image + altRva)(weapon.data());
				}
			}
			// Stable insertion sort preserves choice-bit order within the final
			// category and the genuine leading alternate-weapon attachment.
			const unsigned first = leading ? 1 : 0;
			for (unsigned i = first + 1; i < count; ++i)
			{
				const auto value = order[i];
				auto at = i;
				while (at > first && rank(categories[order[at - 1]], priority) > rank(categories[value], priority))
				{
					order[at] = order[at - 1];
					--at;
				}
				order[at] = value;
			}
			for (unsigned i = 0; i < count; ++i)
			{
				const auto& value = values[order[i]];
				if (value.present && value.capacity >= 0)
				{
					fallback = value.capacity;
					break;
				}
			}
			result = std::max<int32_t>(1, std::bit_cast<int32_t>(std::bit_cast<uint32_t>(fallback) + delta));
			return true;
		}

		int32_t detour(const void* weapon, bool alt)
		{
			int32_t result{};
			if (calculate(weapon, alt, result)) return result;
			return original.load(std::memory_order_acquire)(weapon, alt);
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
		// Validate before the existing signed-inheritance branch hooks are installed.
		for (const auto& pair : std::array<std::pair<uintptr_t, std::span<const unsigned char>>, 5>{ { { capacityRva, capacityBytes },
				 { enumerationRva, enumerationBytes }, { classRva, classBytes }, { firetypeRva, firetypeBytes }, { altRva, altBytes } } })
			if (!matches(base + pair.first, pair.second.data(), pair.second.size())) return hook::Status::NotReady;
		try
		{
			const auto spec = nlohmann::json::parse(specification);
			std::map<std::string, int32_t, std::less<>> pending;
			std::set<std::string, std::less<>> admitted;
			for (const auto& row : spec.at("capacity_modifiers"))
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
			image = base;
			modifiers = std::move(pending);
			weapons = std::move(admitted);
			installedSpec = specification;
			const auto status = hook::Install(reinterpret_cast<void*>(base + capacityRva), &detour, capacityBytes, 16, original);
			if (status != hook::Status::Installed)
			{
				failed = true;
				return status;
			}
			installed = true;
			LOG_INFO("WeaponCapacity", "source ammo priority and additive capacity enabled for %u roots and %u parts",
				unsigned(weapons.size()), unsigned(modifiers.size()));
			return status;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("WeaponCapacity", "metadata admission failed: %s", e.what());
			return hook::Status::Failed;
		}
	}
}
