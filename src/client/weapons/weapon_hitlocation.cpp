#include "client/weapons/weapon_hitlocation.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <array>
#include <map>
#include <set>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <string>
namespace weapon_hitlocation
{
	namespace
	{
#include "client/weapons/weapon_hitlocation_native.inc"
		using Modifier = std::array<float, 22>;
		using Enumerate = unsigned (*)(const void*, bool, const void**, unsigned char*, int);
		using Classification = int (*)(const void*, bool);
		using Common = float (*)(int, const void*, bool, int);
		using Getter = const float* (*)(const void*, bool);
		std::atomic<Common> originalCommon{};
		std::atomic<Getter> originalGetter{};
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

		bool products_impl(const void* source, bool alt, Modifier& products) noexcept
		{
			std::array<unsigned char, 60> weapon{};
			if (!safemem::ReadBytes(source, weapon.data(), weapon.size())) return false;
			uint16_t index{};
			std::memcpy(&index, weapon.data(), 2);
			if (index == 0 || index >= 550) return false;
			uintptr_t complete{};
			char name[256]{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !name_impl(complete, name) || !weapons.contains(name)) return false;
			std::array<const void*, 64> attachments{};
			std::array<unsigned char, 64> categories{};
			const auto count = reinterpret_cast<Enumerate>(image + enumRva)(weapon.data(), alt, attachments.data(), categories.data(), 0);
			if (count > attachments.size()) return false;
			products.fill(1.0f);
			bool found = false;
			// Preserve the original enumeration and single-precision multiplication
			// order. No sorting, folded product, or replacement hit-location table.
			for (unsigned i = 0; i < count; ++i)
			{
				char part[256]{};
				if (!attachments[i] || !name_impl(reinterpret_cast<uintptr_t>(attachments[i]), part)) continue;
				const auto at = modifiers.find(part);
				if (at == modifiers.end()) continue;
				found = true;
				for (size_t location = 0; location < products.size(); ++location)
					products[location] *= at->second[location];
			}
			return found;
		}

		float common_hook(int location, const void* weapon, bool alt, int context)
		{
			const auto value = originalCommon.load(std::memory_order_acquire)(location, weapon, alt, context);
			// The original common path only uses weapon hit-location modifiers for
			// type 2, excluding class 8. Retain those gates for the additional product.
			uint16_t index{};
			if (location < 0 || location >= 22 || !read(reinterpret_cast<uintptr_t>(weapon), index) || !index ||
				reinterpret_cast<Classification>(image + typeRva)(weapon, alt) != 2 ||
				reinterpret_cast<Classification>(image + classRva)(weapon, alt) == 8)
				return value;
			Modifier products{};
			return products_impl(weapon, alt, products) ? value * products[location] : value;
		}

		const float* getter_hook(const void* weapon, bool alt)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
			const auto table = originalGetter.load(std::memory_order_acquire)(weapon, alt);
			// The server damage caller evaluates scale * (product * base); the common
			// caller evaluates (scale * base) * product. Combining them would change
			// floating-point rounding. Only this verified immediate consumer gets a
			// thread-local copy; every other native table reader retains its pointer.
			if (caller != image + serverReturnRva) return table;
			Modifier products{}, base{};
			if (!products_impl(weapon, alt, products) || !safemem::ReadBytes(table, base.data(), sizeof(base))) return table;
			thread_local Modifier adjusted;
			for (size_t i = 0; i < adjusted.size(); ++i)
				adjusted[i] = products[i] * base[i];
			return adjusted.data();
		}

		template <size_t N>
		bool matches(uintptr_t base, uintptr_t rva, const unsigned char (&bytes)[N])
		{
			std::array<unsigned char, N> actual{};
			return safemem::ReadBytes(reinterpret_cast<void*>(base + rva), actual.data(), N) && !std::memcmp(actual.data(), bytes, N);
		}
	}

	hook::Status Install(uintptr_t base, std::string_view specification)
	{
		if (installed) return image == base && installedSpec == specification ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		if (!matches(base, commonRva, commonBytes) || !matches(base, getterRva, getterBytes) || !matches(base, serverRva, serverBytes) ||
			!matches(base, enumRva, enumBytes) || !matches(base, typeRva, typeBytes) || !matches(base, classRva, classBytes))
			return hook::Status::NotReady;
		try
		{
			const auto spec = nlohmann::json::parse(specification);
			std::map<std::string, Modifier, std::less<>> pending;
			std::set<std::string, std::less<>> admitted;
			for (const auto& row : spec.at("hitlocation_modifiers"))
			{
				auto name = row.at("name").get<std::string>();
				if (name.empty() || name.size() >= 255 || name.front() == ',' || name.find('\0') != name.npos) return hook::Status::Failed;
				const auto& encoded = row.at("words");
				if (!encoded.is_array() || encoded.size() != 22) return hook::Status::Failed;
				for (const auto& word : encoded)
					if (!word.is_number_integer() || (!word.is_number_unsigned() && word.get<int64_t>() < 0) ||
						word.get<uint64_t>() > UINT32_MAX)
						return hook::Status::Failed;
				const auto words = encoded.get<std::array<uint32_t, 22>>();
				Modifier m{};
				std::memcpy(m.data(), words.data(), sizeof(m));
				for (float value : m)
					if (!std::isfinite(value)) return hook::Status::Failed;
				if (!pending.emplace(name, m).second) return hook::Status::Failed;
			}
			for (const auto& row : spec.at("weapons"))
			{
				auto name = row.at("asset").get<std::string>();
				if (name.empty() || name.size() >= 255 || !name.ends_with("_mp") || name.front() == ',' || name.find('\0') != name.npos ||
					!admitted.insert(name).second)
					return hook::Status::Failed;
			}
			if (pending.empty() || pending.size() > 20000 || admitted.empty() || admitted.size() > 256) return hook::Status::Failed;
			image = base;
			modifiers = std::move(pending);
			weapons = std::move(admitted);
			installedSpec = specification;
			hook::Batch batch;
			// Full function/context pins were checked above; Batch verifies the
			// entry span again while preparing its bounded instruction relocator.
			batch.Add(reinterpret_cast<void*>(base + commonRva), &common_hook, commonBytes, 16, originalCommon);
			batch.Add(reinterpret_cast<void*>(base + getterRva), &getter_hook, getterBytes, 16, originalGetter);
			if (batch.Commit() != hook::Status::Installed)
			{
				failed = true;
				return hook::Status::Failed;
			}
			installed = true;
			LOG_INFO("WeaponHitLocation", "original later hit-location multipliers enabled for %u admitted roots and %u parts",
				unsigned(weapons.size()), unsigned(modifiers.size()));
			return hook::Status::Installed;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("WeaponHitLocation", "metadata admission failed: %s", e.what());
			return hook::Status::Failed;
		}
	}
}
