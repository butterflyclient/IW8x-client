#include "../software.h"
#include "../d3d.h"
#include "bot_nav_compute.h"

#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <thread>
#include <vector>
namespace console
{
	void Print(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		std::vprintf(format, args);
		va_end(args);
		std::puts("");
	}
}

namespace
{
	using Microsoft::WRL::ComPtr;

	void Check(bool value, const char* message)
	{
		if (!value) throw std::runtime_error(message);
	}

	headless::software::CreateDeviceFn realCreate = nullptr;

	HRESULT WINAPI Reject(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** output)
	{
		ComPtr<IDXGIAdapter1> selected;
		Check(SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&selected))), "adapter interface");
		DXGI_ADAPTER_DESC1 desc{};
		Check(SUCCEEDED(selected->GetDesc1(&desc)), "adapter description");
		Check((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0, "hardware adapter selected");
		Check(level == D3D_FEATURE_LEVEL_12_1 && iid == __uuidof(ID3D12Device), "device parameters changed");
		Check(output == nullptr, "native null-output probe changed");
		return DXGI_ERROR_UNSUPPORTED;
	}
}

int main()
{
	try
	{
		const auto runtime = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		Check(runtime != nullptr, "load native D3D12");
		realCreate = reinterpret_cast<headless::software::CreateDeviceFn>(GetProcAddress(runtime, "D3D12CreateDevice"));
		Check(realCreate != nullptr, "native device factory");
		std::vector<std::thread> probes;
		for (unsigned i = 0; i < 8; ++i)
			probes.emplace_back(
				[]
				{
					Check(headless::software::CreateDevice(&Reject, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr) ==
							DXGI_ERROR_UNSUPPORTED,
						"native failure changed or hardware fallback attempted");
				});
		for (auto& thread : probes)
			thread.join();

		ComPtr<ID3D12Device> device;
		Check(SUCCEEDED(headless::software::CreateDevice(realCreate, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device))), "create WARP device");
		const auto luid = device->GetAdapterLuid();
		Check(headless::d3d::AttachDevice(device.Get()), "attach production command filtering");
		D3D12_HEAP_PROPERTIES uploadHeap{};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_HEAP_PROPERTIES readbackHeap{};
		readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC buffer{};
		buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer.Width = 64;
		buffer.Height = 1;
		buffer.DepthOrArraySize = 1;
		buffer.MipLevels = 1;
		buffer.SampleDesc.Count = 1;
		buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> upload, readback;
		Check(SUCCEEDED(device->CreateCommittedResource(
				  &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))),
			"upload allocation");
		Check(SUCCEEDED(device->CreateCommittedResource(
				  &readbackHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))),
			"readback allocation");
		const uint64_t expected = 0x123456789ABCDEF0;
		void* mapped = nullptr;
		D3D12_RANGE empty{ 0, 0 };
		Check(SUCCEEDED(upload->Map(0, &empty, &mapped)), "map upload");
		std::memcpy(mapped, &expected, sizeof(expected));
		upload->Unmap(0, nullptr);

		for (const auto type : { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY })
		{
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = type;
			ComPtr<ID3D12CommandQueue> queue;
			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList> list;
			ComPtr<ID3D12Fence> fence;
			Check(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))), "queue creation");
			Check(SUCCEEDED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator))), "allocator creation");
			Check(SUCCEEDED(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list))), "list creation");
			if (type == D3D12_COMMAND_LIST_TYPE_DIRECT) list->DrawInstanced(3, 1, 0, 0);
			if (type != D3D12_COMMAND_LIST_TYPE_COPY) list->Dispatch(1, 1, 1);
			list->CopyBufferRegion(readback.Get(), 0, upload.Get(), 0, sizeof(expected));
			Check(SUCCEEDED(list->Close()), "close native list");
			Check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))), "fence creation");
			ID3D12CommandList* work[]{ list.Get() };
			queue->ExecuteCommandLists(1, work);
			Check(SUCCEEDED(queue->Signal(fence.Get(), 1)), "native signal");
			HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			Check(event && SUCCEEDED(fence->SetEventOnCompletion(1, event)), "native fence event");
			const auto wait = WaitForSingleObject(event, 10000);
			CloseHandle(event);
			Check(wait == WAIT_OBJECT_0 && fence->GetCompletedValue() == 1, "native fence stalled");
			Check(SUCCEEDED(queue->Wait(fence.Get(), 1)), "native wait");
			Check(SUCCEEDED(readback->Map(0, nullptr, &mapped)), "map native readback");
			uint64_t actual{};
			std::memcpy(&actual, mapped, sizeof(actual));
			readback->Unmap(0, &empty);
			Check(actual == expected, "copy was dropped or corrupted");
			Check(SUCCEEDED(device->GetDeviceRemovedReason()), "device removed");
		}
		const auto counters = headless::d3d::ReadCounters();
		Check(counters.queueSubmissions == 3 && counters.submittedLists == 3, "queue calls duplicated/dropped");
		Check(counters.draws == 1 && counters.dispatches == 2, "draw/dispatch filtering inactive");

		auto graph = std::make_shared<botnav::Graph>();
		graph->nodes = { { 0, 1, 0, 0 }, { 1, 1, 0, 0 }, { 2, 0, 0, 0 } };
		graph->edges = { { 1, 1, 0, 0 }, { 2, 1, 0, 0 } };
		botnav::Request request{};
		request.source = 0;
		botnav::Compute compute(&headless::d3d::DispatchCompute, false, true);
		const auto result = compute.Run(graph, { request });
		Check(result.visited.size() == 1 && result.visited[0] == botnav::CpuReachable(*graph, 0, request.filter),
			"software bot compute does not match CPU BFS");
		std::printf(
			"PASS: WARP LUID=%08X:%08X, concurrent probes, native failures, DIRECT/COMPUTE/COPY data, fences, filtering, software bot BFS\n",
			static_cast<unsigned>(luid.HighPart), luid.LowPart);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
