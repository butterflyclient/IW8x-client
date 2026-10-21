#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace camo120
{
	using Weapon = std::array<uint8_t, 60>;
	using Clips = std::array<std::array<uint8_t, 88>, 15>;
	constexpr size_t CamoByte = 56;

	inline bool SameWeapon(Weapon a, Weapon b)
	{
		a[CamoByte] = b[CamoByte] = 0;
		return a == b;
	}

	// TU19 reads all 60 weapon bytes plus the mode at +60 when looking up ammo.
	// Rebind only exact keys. Counts/chambers/mode and unrelated stores stay intact.
	inline bool Rebind(Clips& clips, const Weapon& before, const Weapon& after)
	{
		if (!SameWeapon(before, after)) return false;
		for (auto& clip : clips)
			if (!std::memcmp(clip.data(), before.data(), before.size())) clip[CamoByte] = after[CamoByte];
		return true;
	}

	struct CatalogRow
	{
		const char* weapon;
		const char* camo;
		int item;
	};

	inline constexpr CatalogRow catalog[] = {
#include "network/commerce/camo_catalog.inc"
	};

	inline std::string Canonical(std::string weapon)
	{
		if (weapon.ends_with("_mp")) weapon.resize(weapon.size() - 3);
		return weapon;
	}

	inline int Item(std::string_view weapon, std::string_view camo)
	{
		for (const auto& row : catalog)
			if (weapon == row.weapon && camo == row.camo) return row.item;
		return -1;
	}

	inline bool Token(std::string_view text)
	{
		return !text.empty() && text.size() <= 20 &&
			std::all_of(text.begin(), text.end(),
				[](char c)
				{
					return c >= '0' && c <= '9';
				});
	}

	struct Context
	{
		uintptr_t player = 0, connection = 0, map = 0;
		Weapon weapon{};
		bool operator==(const Context&) const = default;
	};

	struct State
	{
		Context context;
		uint8_t original = 0;
		uint64_t token = 0, touched = 0;
		std::string weapon, result = "ready";
	};

	// Caller supplies serialization; all state is bounded and expires. No engine IO.
	class States
	{
		uint64_t serial = 0;
		std::map<uint64_t, State> entries;

	public:
		State* Capture(uint64_t owner, const Context& context, std::string weapon, uint64_t now)
		{
			for (auto it = entries.begin(); it != entries.end();)
				if (now < it->second.touched || now - it->second.touched > 15000)
					it = entries.erase(it);
				else
					++it;
			if (!owner || !context.player || !context.connection || !context.map) return nullptr;
			if (!entries.contains(owner) && entries.size() >= 200) return nullptr;
			auto& state = entries[owner];
			if (!state.token || state.context != context || now < state.touched)
			{
				if (serial == UINT64_MAX)
				{
					entries.erase(owner);
					return nullptr;
				}
				state = { context, context.weapon[CamoByte], ++serial, now, std::move(weapon), "ready" };
			}
			else if (now - state.touched > 2000)
			{
				if (serial == UINT64_MAX)
				{
					entries.erase(owner);
					return nullptr;
				}
				state.token = ++serial; // Refresh authorization without losing the captured original.
			}
			state.touched = now;
			return &state;
		}

		State* Find(uint64_t owner, uint64_t now)
		{
			const auto it = entries.find(owner);
			return it != entries.end() && now >= it->second.touched && now - it->second.touched <= 2000 ? &it->second : nullptr;
		}

		State* Check(uint64_t owner, const Context& context, std::string_view token, uint64_t now)
		{
			auto* s = Find(owner, now);
			return s && s->context == context && token == std::to_string(s->token) ? s : nullptr;
		}

		void Erase(uint64_t owner)
		{
			entries.erase(owner);
		}

		void Clear()
		{
			entries.clear();
		}

		void Changed(State& state, const Weapon& weapon, uint64_t now)
		{
			state.context.weapon = weapon;
			state.touched = now;
			state.result = "applied";
			state.token = serial == UINT64_MAX ? 0 : ++serial;
		}
	};
}
