#pragma once
#include <algorithm>
#include <cmath>
#include <span>
namespace weapon_spread
{
	// Source +280. The first channel changes the up/vertical spread component;
	// the second changes right/horizontal. The original getter accumulates in
	// enumeration order with a separate single-precision add and subtract.
	struct Modifier
	{
		float vertical, horizontal;
	};

	static_assert(sizeof(Modifier) == 8);

	inline Modifier Calculate(std::span<const Modifier* const> parts) noexcept
	{
		Modifier result{ 1, 1 };
		for (const auto* part : parts)
			if (part)
			{
				result.vertical = (result.vertical + part->vertical) - 1.0f;
				result.horizontal = (result.horizontal + part->horizontal) - 1.0f;
			}
		for (float* value : { &result.vertical, &result.horizontal })
		{
			*value = std::max(*value, 0.0f);
			if (double(std::fabs(1.0f - *value)) <= 0.000001) *value = 1.0f;
		}
		return result;
	}
}
