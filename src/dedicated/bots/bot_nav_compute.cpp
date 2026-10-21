#include "dedicated/bots/bot_nav_compute.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
namespace botnav
{
	using Microsoft::WRL::ComPtr;

	static void Hr(HRESULT h)
	{
		if (FAILED(h)) throw std::runtime_error("Bot GPU HRESULT " + std::to_string(uint32_t(h)));
	}

	using Clock = std::chrono::steady_clock;

	static double Ms(Clock::time_point a, Clock::time_point b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	}

	// This proxy loads before Replay's CRT/bootstrap repairs. Optional graphics
	// dependencies must not change that load order. Resolve them only when the
	// background compute worker constructs its device, never at DLL attach.
	struct SystemModule
	{
		HMODULE handle;

		explicit SystemModule(const wchar_t* name) : handle(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
		{
			if (!handle) throw std::runtime_error("Bot GPU library load failed: " + std::to_string(GetLastError()));
		}

		~SystemModule()
		{
			FreeLibrary(handle);
		}

		SystemModule(const SystemModule&) = delete;
		SystemModule& operator=(const SystemModule&) = delete;

		template <class T>
		T Resolve(const char* name)
		{
			auto function = GetProcAddress(handle, name);
			if (!function) throw std::runtime_error(std::string("Bot GPU export missing: ") + name);
			return reinterpret_cast<T>(function);
		}
	};

	struct ComputeApi
	{
		SystemModule d3d{ L"d3d12.dll" }, dxgi{ L"dxgi.dll" }, compiler{ L"d3dcompiler_47.dll" };
		decltype(&D3D12GetDebugInterface) getDebug = d3d.Resolve<decltype(getDebug)>("D3D12GetDebugInterface");
		decltype(&D3D12CreateDevice) createDevice = d3d.Resolve<decltype(createDevice)>("D3D12CreateDevice");
		decltype(&D3D12SerializeRootSignature) serializeRoot = d3d.Resolve<decltype(serializeRoot)>("D3D12SerializeRootSignature");
		decltype(&CreateDXGIFactory1) createFactory = dxgi.Resolve<decltype(createFactory)>("CreateDXGIFactory1");
		decltype(&D3DCompile) compile = compiler.Resolve<decltype(compile)>("D3DCompile");
	};

	// One cooperative BFS workgroup per native source/filter query. Queues/visited
	// are disjoint between groups. Each discovery is atomic and enqueued once.
	static const char* Shader = R"(
StructuredBuffer<uint4> nodes:register(t0);
StructuredBuffer<uint4> edges:register(t1);
StructuredBuffer<uint4> requests:register(t2);
RWStructuredBuffer<uint> scratch:register(u0);
cbuffer Counts:register(b0){uint nodeCount;uint stride;};
groupshared uint head,tail,batchEnd;
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 uint base=group.x*stride;
 uint4 a=requests[group.x*2],b=requests[group.x*2+1];
 for(uint i=lane;i<nodeCount;i+=64)scratch[base+i]=0;
 if(lane==0){head=0;tail=1;scratch[base+nodeCount]=a.x;scratch[base+2*nodeCount]=0;}
 AllMemoryBarrierWithGroupSync();
 if(lane==0)scratch[base+a.x]=1;
 AllMemoryBarrierWithGroupSync();
 for(uint iteration=0;iteration<4096;iteration++){
  if(lane==0)batchEnd=min(tail,head+64);
  GroupMemoryBarrierWithGroupSync();
  if(head==batchEnd)break;
  uint at=head+lane;
  if(at<batchEnd){
   uint4 n=nodes[scratch[base+nodeCount+at]];
   for(uint e=0;e<n.y;e++){
    uint4 edge=edges[n.x+e],dest=nodes[edge.x];
    uint blocked=dest.z&a.z;
    bool reject=a.y!=0 ? blocked==a.z : blocked!=0;
    if(!reject && (dest.w==0 || (dest.w&a.w)!=0) && (edge.y!=0 || (edge.z&b.x)!=0)){
     uint previous;InterlockedCompareExchange(scratch[base+edge.x],0,1,previous);
     if(previous==0){uint slot;InterlockedAdd(tail,1,slot);scratch[base+nodeCount+slot]=edge.x;}
    }
   }
  }
  AllMemoryBarrierWithGroupSync();
  if(lane==0)head=batchEnd;
  GroupMemoryBarrierWithGroupSync();
 }
 if(lane==0)scratch[base+2*nodeCount]=(head==tail)?1:0;
}
)";

	struct Compute::Impl
	{
		ComputeApi api; // Destroy after all COM objects that depend on these modules.
		ComPtr<ID3D12Device> device;
		ComPtr<IDXGIAdapter3> adapter;
		ComPtr<ID3D12InfoQueue> debug;
		ComPtr<ID3D12CommandQueue> queue;
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> list;
		ComPtr<ID3D12RootSignature> root;
		ComPtr<ID3D12PipelineState> pso;
		ComPtr<ID3D12Fence> fence;
		ComPtr<ID3D12QueryHeap> timestamps;
		ComPtr<ID3D12Resource> nodeBuffer, edgeBuffer, nodeUpload, edgeUpload, requestBuffer, requestUpload, scratch, readback,
			timeReadback;
		std::shared_ptr<const Graph> graph;
		DispatchCompute dispatch = nullptr;
		std::string name;
		HANDLE event = nullptr;
		uint64_t serial = 0, frequency = 0;
		bool inFlight = false;
		static constexpr unsigned MaxBatch = 16;

		~Impl()
		{
			if (event) CloseHandle(event);
		}

		ComPtr<ID3D12Resource> Buffer(size_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, bool uav = false)
		{
			D3D12_HEAP_PROPERTIES properties{};
			properties.Type = heap;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = bytes ? bytes : 16;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
			ComPtr<ID3D12Resource> r;
			Hr(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r)));
			return r;
		}

		void Upload(ID3D12Resource* buffer, const void* data, size_t bytes)
		{
			if (!bytes) return;
			void* p = nullptr;
			D3D12_RANGE empty{ 0, 0 };
			Hr(buffer->Map(0, &empty, &p));
			memcpy(p, data, bytes);
			buffer->Unmap(0, nullptr);
		}

		void Transition(ID3D12Resource* buffer, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
		{
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition = { buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to };
			list->ResourceBarrier(1, &b);
		}

		void Reset()
		{
			Hr(allocator->Reset());
			Hr(list->Reset(allocator.Get(), pso.Get()));
		}

		void Submit()
		{
			Hr(list->Close());
			ID3D12CommandList* lists[] = { list.Get() };
			inFlight = true;
			queue->ExecuteCommandLists(1, lists);
			Hr(queue->Signal(fence.Get(), ++serial));
			Hr(fence->SetEventOnCompletion(serial, event));
			if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
				throw std::runtime_error("Bot GPU fence timeout; native CPU fallback remains active");
			Hr(device->GetDeviceRemovedReason());
			inFlight = false;
		}
	};

	Compute::Compute(DispatchCompute dispatch, bool debug, bool software) : impl_(std::make_unique<Impl>())
	{
		auto& p = *impl_;
		p.dispatch = dispatch;
		if (debug)
		{
			ComPtr<ID3D12Debug> layer;
			Hr(p.api.getDebug(IID_PPV_ARGS(&layer)));
			layer->EnableDebugLayer();
		}
		ComPtr<IDXGIFactory4> factory;
		Hr(p.api.createFactory(IID_PPV_ARGS(&factory)));
		ComPtr<IDXGIAdapter1> adapter;
		if (software)
		{
			Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
			Hr(p.api.createDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&p.device)));
			Hr(adapter.As(&p.adapter));
			DXGI_ADAPTER_DESC1 desc{};
			Hr(adapter->GetDesc1(&desc));
			if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) throw std::runtime_error("Bot software adapter is not WARP");
			char name[256]{};
			WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
			p.name = name;
		}
		else
			for (unsigned i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				DXGI_ADAPTER_DESC1 desc{};
				Hr(adapter->GetDesc1(&desc));
				if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
					SUCCEEDED(p.api.createDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&p.device))))
				{
					Hr(adapter.As(&p.adapter));
					char name[256]{};
					WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
					p.name = name;
					break;
				}
				adapter.Reset();
			}
		if (!p.device) throw std::runtime_error("No D3D12 bot compute device");
		if (debug) Hr(p.device.As(&p.debug));
		D3D12_COMMAND_QUEUE_DESC q{};
		q.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		Hr(p.device->CreateCommandQueue(&q, IID_PPV_ARGS(&p.queue)));
		Hr(p.queue->GetTimestampFrequency(&p.frequency));
		Hr(p.device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&p.allocator)));
		Hr(p.device->CreateCommandList(0, q.Type, p.allocator.Get(), nullptr, IID_PPV_ARGS(&p.list)));
		Hr(p.list->Close());
		Hr(p.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.fence)));
		p.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!p.event) throw std::runtime_error("Bot GPU event allocation failed");
		D3D12_ROOT_PARAMETER parameters[5]{};
		for (unsigned i = 0; i < 3; ++i)
		{
			parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			parameters[i].Descriptor.ShaderRegister = i;
		}
		parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[4].Constants = { 0, 0, 2 };
		D3D12_ROOT_SIGNATURE_DESC root{};
		root.NumParameters = 5;
		root.pParameters = parameters;
		ComPtr<ID3DBlob> signature, errors, shader;
		Hr(p.api.serializeRoot(&root, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
		Hr(p.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p.root)));
		auto result = p.api.compile(Shader, strlen(Shader), "native_bot_reachability", nullptr, nullptr, "main", "cs_5_1",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
		if (FAILED(result))
			throw std::runtime_error(
				errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Bot shader failed");
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
		pipeline.pRootSignature = p.root.Get();
		pipeline.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
		Hr(p.device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&p.pso)));
		D3D12_QUERY_HEAP_DESC queries{ D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0 };
		Hr(p.device->CreateQueryHeap(&queries, IID_PPV_ARGS(&p.timestamps)));
		p.timeReadback = p.Buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
		p.requestBuffer =
			p.Buffer(sizeof(Request) * Impl::MaxBatch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p.requestUpload = p.Buffer(sizeof(Request) * Impl::MaxBatch, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	Compute::~Compute()
	{
		// A timeout does not prove that the GPU stopped reading these resources.
		// Disable the optional backend, but retain this one bounded allocation until
		// process exit instead of releasing memory still referenced by queued work.
		if (impl_ && impl_->inFlight) impl_.release();
	}

	GpuResult Compute::Run(std::shared_ptr<const Graph> graph, const std::vector<Request>& requests)
	{
		const auto start = Clock::now();
		auto& p = *impl_;
		const auto n = unsigned(graph->nodes.size()), batch = unsigned(requests.size());
		if (!n || n > 131072 || !batch || batch > Impl::MaxBatch) throw std::runtime_error("Bot graph/batch outside compute limits");
		for (const auto& request : requests)
			if (request.source >= n) throw std::runtime_error("Bad bot source");
		const unsigned stride = 2 * n + 4;
		if (p.graph != graph)
		{
			const auto nb = n * sizeof(Node), eb = graph->edges.size() * sizeof(Edge);
			p.nodeBuffer = p.Buffer(nb, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
			p.edgeBuffer = p.Buffer(eb, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
			p.nodeUpload = p.Buffer(nb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			p.edgeUpload = p.Buffer(eb, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			p.scratch = p.Buffer(size_t(stride) * 4 * Impl::MaxBatch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			p.readback = p.Buffer(size_t(n + 1) * 4 * Impl::MaxBatch, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
			p.Upload(p.nodeUpload.Get(), graph->nodes.data(), nb);
			p.Upload(p.edgeUpload.Get(), graph->edges.data(), eb);
			p.Reset();
			p.list->CopyBufferRegion(p.nodeBuffer.Get(), 0, p.nodeUpload.Get(), 0, nb);
			if (eb) p.list->CopyBufferRegion(p.edgeBuffer.Get(), 0, p.edgeUpload.Get(), 0, eb);
			p.Transition(p.nodeBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			p.Transition(p.edgeBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			p.Submit();
			p.graph = graph;
		}
		p.Upload(p.requestUpload.Get(), requests.data(), sizeof(Request) * batch);
		p.Reset();
		p.Transition(p.requestBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		p.list->CopyBufferRegion(p.requestBuffer.Get(), 0, p.requestUpload.Get(), 0, sizeof(Request) * batch);
		p.Transition(p.requestBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p.list->SetComputeRootSignature(p.root.Get());
		p.list->SetComputeRootShaderResourceView(0, p.nodeBuffer->GetGPUVirtualAddress());
		p.list->SetComputeRootShaderResourceView(1, p.edgeBuffer->GetGPUVirtualAddress());
		p.list->SetComputeRootShaderResourceView(2, p.requestBuffer->GetGPUVirtualAddress());
		p.list->SetComputeRootUnorderedAccessView(3, p.scratch->GetGPUVirtualAddress());
		const unsigned counts[] = { n, stride };
		p.list->SetComputeRoot32BitConstants(4, 2, counts, 0);
		p.list->EndQuery(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
		if (p.dispatch)
		{
			if (!p.dispatch(p.list.Get(), batch, 1, 1)) throw std::runtime_error("Headless bot compute dispatch unavailable");
		}
		else
			p.list->Dispatch(batch, 1, 1);
		p.list->EndQuery(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
		p.Transition(p.scratch.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		for (unsigned q = 0; q < batch; ++q)
		{
			p.list->CopyBufferRegion(p.readback.Get(), size_t(q) * (n + 1) * 4, p.scratch.Get(), size_t(q) * stride * 4, size_t(n) * 4);
			p.list->CopyBufferRegion(p.readback.Get(), (size_t(q) * (n + 1) + n) * 4, p.scratch.Get(), (size_t(q) * stride + 2 * n) * 4, 4);
		}
		p.Transition(p.scratch.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		p.list->ResolveQueryData(p.timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, p.timeReadback.Get(), 0);
		p.Submit();
		GpuResult output;
		void* mapped = nullptr;
		D3D12_RANGE range{ 0, size_t(n + 1) * batch * 4 }, empty{ 0, 0 };
		Hr(p.readback->Map(0, &range, &mapped));
		bool complete = true;
		for (unsigned q = 0; q < batch; ++q)
		{
			const auto* values = static_cast<const uint32_t*>(mapped) + size_t(q) * (n + 1);
			complete = complete && values[n] == 1;
			output.visited.emplace_back(values, values + n);
		}
		p.readback->Unmap(0, &empty);
		if (!complete) throw std::runtime_error("Bot GPU BFS exceeded iteration limit; incomplete result rejected");
		uint64_t times[2]{};
		range.End = 16;
		Hr(p.timeReadback->Map(0, &range, &mapped));
		memcpy(times, mapped, 16);
		p.timeReadback->Unmap(0, &empty);
		output.kernelMs = (times[1] - times[0]) * 1000.0 / p.frequency;
		output.totalMs = Ms(start, Clock::now());
		DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
		Hr(p.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory));
		output.localMemory = memory.CurrentUsage;
		return output;
	}

	std::string Compute::Adapter() const
	{
		return impl_->name;
	}

	unsigned Compute::DebugErrors() const
	{
		unsigned count = 0;
		if (!impl_->debug) return 0;
		for (uint64_t i = 0; i < impl_->debug->GetNumStoredMessages(); ++i)
		{
			SIZE_T bytes = 0;
			Hr(impl_->debug->GetMessage(i, nullptr, &bytes));
			std::vector<unsigned char> storage(bytes);
			auto* m = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			Hr(impl_->debug->GetMessage(i, m, &bytes));
			if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) ++count;
		}
		return count;
	}
}
