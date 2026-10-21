#pragma once
#include <d3d12.h>
namespace headless::software
{
	using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

	// Called after Replay's attach-time compatibility repair. Never substitutes a
	// physical adapter if software initialization fails.
	HRESULT CreateDevice(CreateDeviceFn native, D3D_FEATURE_LEVEL level, REFIID iid, void** output);
}
