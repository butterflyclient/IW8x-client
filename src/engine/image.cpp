#include "image.hpp"
#include "../common/state.h"
namespace game
{
	uintptr_t base() noexcept
	{
		return state::moduleBase.load(std::memory_order_acquire);
	}

	uint32_t image_size() noexcept
	{
		return state::imageSize.load(std::memory_order_acquire);
	}

	bool initialized() noexcept
	{
		return base() != 0 && image_size() != 0;
	}
}
