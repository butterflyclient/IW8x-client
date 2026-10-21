#include "client/weapons/weapon_inspect_playback.h"
#include "client/weapons/weapon_inspect_policy.h"
#include "engine/safemem.h"
#include "thirdparty/json.hpp"
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace weapon_inspect_playback
{
	namespace
	{
#include "client/weapons/weapon_inspect_bindings.inc"
		using Selector = int (*)(void*, void*, int, int, const void*, bool, int, void*);
		using Completion = int (*)(void*, void*, int, int, bool);
		using Interruption = bool (*)(void*, void*, int, void*);
		using Reset = void (*)(void*, void*, void*, int);
		using Active = const void* (*)(void*, void*);
		std::atomic<Selector> originalSelector{};
		std::atomic<Completion> originalCompletion{};
		std::atomic<Interruption> originalInterruption{};
		uintptr_t image{};
		bool installed{}, failed{};
		std::string installedSpec;
		std::set<std::string, std::less<>> roots;

		template <class T>
		bool read(uintptr_t address, T& value) noexcept
		{
			return safemem::ReadBytes(reinterpret_cast<const void*>(address), &value, sizeof(value));
		}

		bool admitted_impl(const void* weapon) noexcept
		{
			uint16_t index{};
			if (!read(reinterpret_cast<uintptr_t>(weapon), index) || !index || index >= 550) return false;
			uintptr_t complete{}, name{};
			if (!read(image + 0xC6EC870 + index * 8, complete) || !read(complete, name)) return false;
			char text[256]{};
			const auto length = safemem::ReadString(reinterpret_cast<const char*>(name), text, sizeof(text));
			return length && length < 255 && roots.contains(text);
		}

		int select(void* map, void* ps, int action, int argument, const void* weapon, bool alt, int hand, void* handler)
		{
			if (action == weapon_inspect::inspectAction && admitted_impl(weapon)) return weapon_inspect::inspectAnimation;
			return originalSelector.load(std::memory_order_acquire)(map, ps, action, argument, weapon, alt, hand, handler);
		}

		int complete_impl(void* pm, void* pml, int elapsed, int hand, bool force)
		{
			// Execute the native pre-dispatch event first. It can change the weapon
			// state; testing state 78 before this call would bypass that behavior.
			const auto result = originalCompletion.load(std::memory_order_acquire)(pm, pml, elapsed, hand, force);
			if (result || hand < 0 || hand > 1) return result;
			const auto address = reinterpret_cast<uintptr_t>(pm);
			uintptr_t ps{}, handler{}, map{};
			int state{}, time{};
			if (!read(address + 8, ps) || !read(ps + 0x530 + hand * 0x50, state) || state != weapon_inspect::inspectState ||
				!read(ps + 0x520 + hand * 0x50, time) || time > 0 || !read(address + 0x348, handler) || !read(address + 0x380, map))
				return result;
			const auto weapon = reinterpret_cast<Active>(image + activeRva)(reinterpret_cast<void*>(map), reinterpret_cast<void*>(ps));
			if (!admitted_impl(weapon)) return result;
			reinterpret_cast<Reset>(image + resetRva)(
				reinterpret_cast<void*>(ps), reinterpret_cast<void*>(handler), reinterpret_cast<void*>(map), hand);
			return 1;
		}

		bool interruptible(void* map, void* ps, int hand, void* handler)
		{
			const auto result = originalInterruption.load(std::memory_order_acquire)(map, ps, hand, handler);
			if (result || hand < 0 || hand > 1) return result;
			int state{};
			if (!read(reinterpret_cast<uintptr_t>(ps) + 0x530 + hand * 0x50, state) || state != weapon_inspect::inspectState) return result;
			const auto weapon = reinterpret_cast<Active>(image + activeRva)(map, ps);
			return admitted_impl(weapon) || result;
		}

		bool matches(uintptr_t address, std::span<const unsigned char> expected)
		{
			std::vector<unsigned char> actual(expected.size());
			return safemem::ReadBytes(reinterpret_cast<void*>(address), actual.data(), actual.size()) &&
				std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
		}
	}

	hook::Status Install(uintptr_t base, std::string_view specification)
	{
		if (installed) return image == base && installedSpec == specification ? hook::Status::Installed : hook::Status::Failed;
		if (failed) return hook::Status::Failed;
		for (const auto& [rva, bytes] : std::array<std::pair<uintptr_t, std::span<const unsigned char>>, 5>{
				 { { selectorRva, selectorBytes }, { completionRva, completionBytes }, { resetRva, resetBytes }, { activeRva, activeBytes },
					 { interruptionRva, interruptionBytes } } })
			if (!matches(base + rva, bytes)) return hook::Status::NotReady;
		try
		{
			const auto metadata = nlohmann::json::parse(specification);
			std::set<std::string, std::less<>> admitted;
			const auto& entries = metadata.at("inspect_weapons");
			if (!entries.is_array() || entries.empty() || entries.size() > 256) return hook::Status::Failed;
			for (const auto& row : entries)
			{
				auto root = row.get<std::string>();
				if (root.empty() || root.size() >= 255 || root.front() == ',' || !root.ends_with("_mp") || root.find('\0') != root.npos ||
					!admitted.insert(root).second)
					return hook::Status::Failed;
			}
			image = base;
			roots = std::move(admitted);
			installedSpec = specification;
			hook::Batch batch;
			batch.Add(reinterpret_cast<void*>(base + selectorRva), &select, selectorBytes, 16, originalSelector);
			batch.Add(reinterpret_cast<void*>(base + completionRva), &complete_impl, completionBytes, 16, originalCompletion);
			batch.Add(reinterpret_cast<void*>(base + interruptionRva), &interruptible, interruptionBytes, 16, originalInterruption);
			const auto result = batch.Commit();
			if (result != hook::Status::Installed)
			{
				failed = true;
				return result;
			}
			installed = true;
			return result;
		}
		catch (const std::exception&)
		{
			return hook::Status::Failed;
		}
	}
}
