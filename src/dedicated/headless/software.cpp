#include "software.h"
#include "console.h"

#include <dxgi1_4.h>
#include <wrl/client.h>
namespace headless::software
{
	namespace
	{
		struct WarpAdapter
		{
			// The process-lifetime adapter depends on this runtime. Do not import DXGI
			// into the proxy's attach-time dependency chain or unload it under devices.
			HMODULE runtime = nullptr;
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			HRESULT status = E_UNEXPECTED;

			WarpAdapter()
			{
				runtime = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
				if (!runtime)
				{
					status = HRESULT_FROM_WIN32(GetLastError());
					return;
				}
				const auto create = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(runtime, "CreateDXGIFactory1"));
				if (!create)
				{
					status = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
					return;
				}
				Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
				status = create(IID_PPV_ARGS(&factory));
				if (FAILED(status)) return;
				status = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
				if (FAILED(status)) return;
				DXGI_ADAPTER_DESC1 desc{};
				status = adapter->GetDesc1(&desc);
				if (FAILED(status)) return;
				if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				{
					status = E_UNEXPECTED;
					adapter.Reset();
					return;
				}
				LOG_INFO("Headless",
					"WARP adapter selected: %ls vendor=0x%X device=0x%X flags=0x%X LUID=%08X:%08X; D3D12 work stays on CPU",
					desc.Description, desc.VendorId, desc.DeviceId, desc.Flags, static_cast<unsigned>(desc.AdapterLuid.HighPart),
					desc.AdapterLuid.LowPart);
			}
		};
	}

	HRESULT CreateDevice(CreateDeviceFn native, D3D_FEATURE_LEVEL level, REFIID iid, void** output)
	{
		// C++ static initialization publishes the complete adapter once, including
		// concurrent capability probes. Native HRESULT/IID/output semantics remain
		// intact, including the supported null-output feature-level probe.
		static const WarpAdapter warp;
		if (FAILED(warp.status))
		{
			if (output) *output = nullptr;
			LOG_ERR("Headless", "WARP adapter unavailable: HRESULT=0x%08X", static_cast<unsigned>(warp.status));
			return warp.status;
		}
		if (!native) return E_POINTER;
		return native(warp.adapter.Get(), level, iid, output);
	}
}
