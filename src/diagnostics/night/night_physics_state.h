#pragma once
#include <bit>
#include <cstdint>
#include <span>
#include <unordered_map>
namespace night_physics
{
	enum class MapScope
	{
		Any,
		Escape130
	};

	struct SoundRule
	{
		std::int32_t interval{}, limit{};
		MapScope scope = MapScope::Any;
	};

	struct ParticleSound
	{
		std::uint32_t time{};
		std::uint8_t count{};
	};

	inline bool ApplySoundRule(SoundRule rule, ParticleSound& state, std::uint32_t now)
	{
		// Original 1.28 41E8DDE: signed byte count, wrapping signed time delta.
		if ((rule.limit && std::bit_cast<std::int8_t>(state.count) >= rule.limit) ||
			std::bit_cast<std::int32_t>(now - state.time) < rule.interval)
			return false;
		state.time = now;
		state.count = static_cast<std::uint8_t>(state.count + 1);
		return true;
	}

	class SoundState
	{
		std::unordered_map<uintptr_t, SoundRule> rules_;
		std::unordered_map<uintptr_t, std::unordered_map<int, ParticleSound>> pipelines_;

	public:
		bool Register(uintptr_t shape, SoundRule rule)
		{
			if (!shape || rules_.contains(shape)) return false;
			rules_.emplace(shape, rule);
			return true;
		}

		void Unregister(uintptr_t shape)
		{
			rules_.erase(shape);
		}

		bool Contains(uintptr_t shape) const
		{
			return rules_.contains(shape);
		}

		MapScope Scope(uintptr_t shape) const
		{
			return rules_.at(shape).scope;
		}

		bool Allow(uintptr_t pipeline, uintptr_t shape, int particle, std::uint32_t now)
		{
			const auto rule = rules_.find(shape);
			if (rule == rules_.end()) return true;
			return ApplySoundRule(rule->second, pipelines_[pipeline][particle], now);
		}

		void Created(uintptr_t pipeline, std::span<const int> particles)
		{
			const auto found = pipelines_.find(pipeline);
			if (found == pipelines_.end()) return;
			for (const auto id : particles)
				found->second.erase(id);
		}

		void Released(uintptr_t pipeline)
		{
			pipelines_.erase(pipeline);
		}
	};
}
