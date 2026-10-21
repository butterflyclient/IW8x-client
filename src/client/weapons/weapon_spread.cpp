#include "client/weapons/weapon_spread.h"
#include "client/weapons/weapon_spread_policy.h"
#include "engine/safemem.h"
#include "common/console.h"
#include "thirdparty/json.hpp"
#include <array>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>
namespace weapon_spread
{
	namespace
	{
#include "client/weapons/weapon_spread_bridge.inc"
		struct Context
		{
			const void* weapon;
			uint64_t alt, caller, reserved;
			std::array<float, 4> values;
			uint32_t kind;
		};

		static_assert(offsetof(Context, values) == 32 && offsetof(Context, kind) == 48);
		using Enumerate = unsigned (*)(const void*, bool, const void**, unsigned char*, int);
		using Fn = void (*)();
		uintptr_t image{};
		bool installed{}, failed{};
		std::string installedSpec;
		std::map<std::string, Modifier, std::less<>> modifiers;
		std::set<std::string, std::less<>> weapons;

		template <class T>
		bool read(uintptr_t at, T& value) noexcept
		{
			return safemem::ReadBytes(reinterpret_cast<const void*>(at), &value, sizeof(value));
		}

		bool name_impl(uintptr_t at, char (&name)[256]) noexcept
		{
			uintptr_t pointer{};
			if (!read(at, pointer)) return false;
			const auto n = safemem::ReadString(reinterpret_cast<const char*>(pointer), name, sizeof(name));
			return n > 0 && n < 255;
		}

		bool get_modifier(const void* argument, bool alt, Modifier& value) noexcept
		{
			std::array<unsigned char, 60> weapon{};
			if (!safemem::ReadBytes(argument, weapon.data(), weapon.size())) return false;
			uint16_t index{};
			std::memcpy(&index, weapon.data(), 2);
			if (!index || index >= 550) return false;
			uintptr_t complete{};
			char root[256]{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !name_impl(complete, root) || !weapons.contains(root)) return false;
			std::array<const void*, 64> parts{};
			std::array<unsigned char, 64> categories{};
			const auto count = reinterpret_cast<Enumerate>(image + 0x11706A0)(weapon.data(), alt, parts.data(), categories.data(), 0);
			if (count > parts.size()) return false;
			std::array<const Modifier*, 64> active{};
			bool found = false;
			for (unsigned i = 0; i < count; ++i)
			{
				char name[256]{};
				if (!parts[i] || !name_impl(reinterpret_cast<uintptr_t>(parts[i]), name)) return false;
				const auto at = modifiers.find(name);
				if (at != modifiers.end())
				{
					active[i] = &at->second;
					found = true;
				}
			}
			if (!found) return false;
			value = Calculate(std::span(active.data(), count));
			return std::isfinite(value.horizontal) && std::isfinite(value.vertical);
		}

		void observe(Context* context) noexcept
		{
			if (!context || context->kind >= 4) return;
			bool alt = (context->alt & 255) != 0;
			if (context->kind == 0)
			{
				// 12478A2 uses the same helper with a smaller trajectory-only frame.
				// It must not even read weapon bytes, irrespective of coincidental
				// stack contents matching an admitted index.
				if (context->caller != image + 0x11D318B) return;
				unsigned char flag{};
				if (!read(reinterpret_cast<uintptr_t>(context->weapon) + 0x3C, flag) || flag > 1) return;
				alt = flag != 0;
			}
			Modifier modifier{};
			if (!get_modifier(context->weapon, alt, modifier)) return;
			auto& values = context->values;
			if (context->kind < 3)
			{
				values[0] *= modifier.horizontal;
				values[1] *= modifier.vertical;
			}
			else
			{
				// Native textured reticle quad: retain its center while changing both
				// dimensions. The native four-arm path is scaled before its offsets.
				const auto width = values[2] * modifier.horizontal;
				const auto height = values[3] * modifier.vertical;
				values[0] += (values[2] - width) * 0.5f;
				values[1] += (values[3] - height) * 0.5f;
				values[2] = width;
				values[3] = height;
			}
		}

		bool matches(uintptr_t at, std::span<const unsigned char> bytes)
		{
			std::vector<unsigned char> actual(bytes.size());
			return safemem::ReadBytes(reinterpret_cast<const void*>(at), actual.data(), actual.size()) &&
				!std::memcmp(actual.data(), bytes.data(), bytes.size());
		}
	}

	hook::Status Install(uintptr_t base, std::string_view specification)
	{
		if (installed) return image == base && installedSpec == specification ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		if (!matches(base + 0x11706A0, enumerationBytes)) return hook::Status::NotReady;
		for (const auto& context : argumentContexts)
			if (!matches(base + context.rva, context.bytes)) return hook::Status::NotReady;
		for (const auto& site : spreadSites)
			if (!matches(base + site.rva, site.bytes) || !matches(base + site.context, site.contextBytes)) return hook::Status::NotReady;
		try
		{
			const auto spec = nlohmann::json::parse(specification);
			std::map<std::string, Modifier, std::less<>> pending;
			std::set<std::string, std::less<>> admitted;
			for (const auto& row : spec.at("spread_modifiers"))
			{
				auto name = row.at("name").get<std::string>();
				const auto& raw = row.at("words");
				if (name.empty() || name.size() >= 255 || name.front() == ',' || name.find('\0') != name.npos || !raw.is_array() ||
					raw.size() != 2)
					return hook::Status::Failed;
				for (const auto& word : raw)
					if (!word.is_number_integer() || (!word.is_number_unsigned() && word.get<int64_t>() < 0) ||
						word.get<uint64_t>() > UINT32_MAX)
						return hook::Status::Failed;
				const auto words = raw.get<std::array<uint32_t, 2>>();
				Modifier value{};
				std::memcpy(&value, words.data(), sizeof(value));
				if (!std::isfinite(value.vertical) || !std::isfinite(value.horizontal) || !pending.emplace(name, value).second)
					return hook::Status::Failed;
			}
			for (const auto& row : spec.at("weapons"))
			{
				auto name = row.at("asset").get<std::string>();
				if (name.empty() || name.size() >= 255 || name.front() == ',' || !name.ends_with("_mp") || name.find('\0') != name.npos ||
					!admitted.insert(name).second)
					return hook::Status::Failed;
			}
			if (pending.empty() || pending.size() > 20000 || admitted.empty() || admitted.size() > 256) return hook::Status::Failed;
			hook::Batch batch;
			std::array<unsigned char*, 4> memory{};
			for (size_t i = 0; i < memory.size(); ++i)
			{
				const auto& site = spreadSites[i];
				memory[i] = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
				if (!memory[i]) return hook::Status::Failed;
				std::memcpy(memory[i], site.code.data(), site.code.size());
				auto callback = &observe;
				std::memcpy(memory[i] + site.callback, &callback, sizeof(callback));
				auto slot = new (memory[i] + 0x1000) std::atomic<Fn>{ nullptr };
				DWORD previous{};
				if (!VirtualProtect(memory[i], 4096, PAGE_EXECUTE_READ, &previous)) return hook::Status::Failed;
				batch.Add(
					reinterpret_cast<void*>(base + site.rva), reinterpret_cast<Fn>(memory[i]), site.bytes.data(), site.bytes.size(), *slot);
				FlushInstructionCache(GetCurrentProcess(), memory[i], 4096);
			}
			image = base;
			modifiers = std::move(pending);
			weapons = std::move(admitted);
			installedSpec = specification;
			const auto status = batch.Commit();
			if (status != hook::Status::Installed)
			{
				failed = true;
				return status;
			}
			installed = true;
			LOG_INFO("WeaponSpread", "later two-axis spread enabled for %u roots and %u attachments", unsigned(weapons.size()),
				unsigned(modifiers.size()));
			return hook::Status::Installed;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("WeaponSpread", "metadata admission failed: %s", e.what());
			return hook::Status::Failed;
		}
	}
}
