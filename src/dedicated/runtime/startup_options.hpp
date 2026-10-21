#pragma once
#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	bool HasFlag(const wchar_t* flag);

	bool StartupTokenChar(wchar_t c);

	ArgumentStatus ReadStartupToken(const wchar_t* flag, char* out, size_t outSize);

	void LoadStartupOptions();
}
