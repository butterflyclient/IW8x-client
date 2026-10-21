#pragma once
#include "game.h"
namespace game
{
	// Uses the image already published by Replay bootstrap; no second image owner.
	uintptr_t base() noexcept;
	uint32_t image_size() noexcept;
	bool initialized() noexcept;

	template <class T = void>
	T* at(uintptr_t rva) noexcept
	{
		return reinterpret_cast<T*>(base() + rva);
	}

	template <class T>
	uintptr_t rva(T* pointer) noexcept
	{
		const auto address = reinterpret_cast<uintptr_t>(pointer);
		return address >= base() ? address - base() : 0;
	}
}
