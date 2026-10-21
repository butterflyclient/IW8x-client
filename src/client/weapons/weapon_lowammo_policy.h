#pragma once
#include <cstdint>
#include <span>
#include <algorithm>

#include <immintrin.h>
namespace weapon_lowammo
{
	inline int32_t Threshold(
		int32_t originalDefault, float fraction, int32_t clip, std::span<const int32_t* const> overrides, bool supported = true)
	{
		if (supported)
		{
			auto value = originalDefault;
			for (const auto* candidate : overrides)
				if (candidate && *candidate >= 0)
				{
					value = *candidate;
					break;
				}
			if (value > 0) return value;
		}
		// Original cvtdq2ps, mulss, cvttss2si, including indefinite INT_MIN on
		// overflow/NaN. A C++ float-to-int cast has undefined overflow semantics.
		return _mm_cvtt_ss2si(_mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(), clip), _mm_set_ss(fraction)));
	}

	inline float HudFraction(int32_t warning, int32_t clip)
	{
		clip = std::min(clip, 999);
		warning = std::min(warning, 999);
		return clip > 0 ? static_cast<float>(warning) / static_cast<float>(clip) : 0.0f;
	}
}
