#pragma once
#include <string_view>
namespace frontend_animtree128
{
	// Both verified original 1.24 and 1.28 client_character trees exceed Replay's
	// 512-node parser bound. No other scene is permitted to extend that bound.
	inline constexpr bool SceneSelected(std::wstring_view scene) noexcept
	{
		return scene == L"1.24" || scene == L"1.28-test" || scene == L"1.28-halloween";
	}
}
