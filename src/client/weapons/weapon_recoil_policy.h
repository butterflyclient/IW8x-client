#pragma once
#include <cstdint>
#include <span>
namespace weapon_recoil
{
	// Original later attachment +1D0, 40 bytes. Retain both count thresholds and
	// multiplicative channels. The scoped native bridge owns the later metadata.
	struct Modifier
	{
		float earlyHipHorizontal, earlyHipVertical;
		uint32_t earlyShots;
		float earlyAdsHorizontal, earlyAdsVertical;
		float lateHipHorizontal, lateHipVertical;
		uint32_t lateShots;
		float lateAdsHorizontal, lateAdsVertical;
	};

	static_assert(sizeof(Modifier) == 40);

	inline void Apply(
		std::span<const Modifier* const> modifiers, uint32_t shots, float adsFraction, float& horizontal, float& vertical) noexcept
	{
		const Modifier* first = nullptr;
		for (const auto* p : modifiers)
			if (p)
			{
				first = p;
				break;
			}
		if (!first) return;
		// Source RVA 3329123/33292FF reads the exact float at RVA 7076928.
		const bool ads = adsFraction >= 0.0001f;
		if (shots < first->earlyShots)
		{
			for (const auto* p : modifiers)
				if (p)
				{
					horizontal *= ads ? p->earlyAdsHorizontal : p->earlyHipHorizontal;
					vertical *= ads ? p->earlyAdsVertical : p->earlyHipVertical;
				}
		}
		if (shots >= first->lateShots)
		{
			for (const auto* p : modifiers)
				if (p)
				{
					horizontal *= ads ? p->lateAdsHorizontal : p->lateHipHorizontal;
					vertical *= ads ? p->lateAdsVertical : p->lateHipVertical;
				}
		}
	}
}
