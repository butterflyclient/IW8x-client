#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#ifdef IW8X_ISLAND_PORT_PROBE
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#endif
#include "dedicated/headless/d3d.h"
#include "common/console.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
namespace headless::d3d
{
	namespace
	{
		std::mutex g_tablesMutex;
		std::atomic<uint64_t> g_lists{ 0 }, g_draws{ 0 }, g_dispatches{ 0 }, g_indirect{ 0 }, g_clears{ 0 }, g_rays{ 0 };
		std::atomic<uint64_t> g_queueSubmissions{ 0 }, g_submittedLists{ 0 };

		using CreateListFn = decltype(ID3D12DeviceVtbl::CreateCommandList);
		using CreateList1Fn = decltype(ID3D12Device4Vtbl::CreateCommandList1);
		using CreateQueueFn = decltype(ID3D12DeviceVtbl::CreateCommandQueue);
		using ExecuteFn = decltype(ID3D12CommandQueueVtbl::ExecuteCommandLists);
#ifdef IW8X_ISLAND_PORT_PROBE
		using CreateGraphicsFn = decltype(ID3D12DeviceVtbl::CreateGraphicsPipelineState);
		using CreateRootFn = decltype(ID3D12DeviceVtbl::CreateRootSignature);
		using CreateCommittedFn = decltype(ID3D12DeviceVtbl::CreateCommittedResource);
		using CreatePlacedFn = decltype(ID3D12DeviceVtbl::CreatePlacedResource);
		using AllocationFn = decltype(ID3D12DeviceVtbl::GetResourceAllocationInfo);
#endif

		struct DeviceTable
		{
			const void* table = nullptr;
			CreateListFn create = nullptr;
			CreateList1Fn create1 = nullptr;
			CreateQueueFn createQueue = nullptr;
#ifdef IW8X_ISLAND_PORT_PROBE
			CreateGraphicsFn createGraphics = nullptr;
			CreateRootFn createRoot = nullptr;
			CreateCommittedFn createCommitted = nullptr;
			CreatePlacedFn createPlaced = nullptr;
			AllocationFn allocation = nullptr;
#endif
		};

		// COM tables belong to the pinned D3D runtime. Keep originals per table, since
		// the debug layer and different device interfaces may use different methods.
		std::array<DeviceTable, 32> g_devices{};
		using DispatchFn = decltype(ID3D12GraphicsCommandListVtbl::Dispatch);

		struct DispatchTable
		{
			const void* table = nullptr;
			DispatchFn function = nullptr;
		};

		std::array<DispatchTable, 32> g_dispatchTables{};

		struct QueueTable
		{
			std::atomic<const void*> table{ nullptr };
			std::atomic<ExecuteFn> execute{ nullptr };
			std::atomic<bool> sampled{ false };
		};

		std::array<QueueTable, 32> g_queues{};

		template <typename Fn>
		bool Replace(Fn* slot, Fn replacement)
		{
			if (*slot == replacement) return true;
			DWORD protection = 0;
			if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), reinterpret_cast<void*>(replacement));
			DWORD ignored = 0;
			if (!VirtualProtect(slot, sizeof(*slot), protection, &ignored))
			{
				LOG_ERR("Headless", "could not restore D3D12 vtable protection (%lu)", GetLastError());
				return false;
			}
			return true;
		}

		void STDMETHODCALLTYPE Draw(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT)
		{
			g_draws.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE DrawIndexed(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT)
		{
			g_draws.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList*, UINT, UINT, UINT)
		{
			g_dispatches.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE Indirect(
			ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64)
		{
			g_indirect.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE ClearColor(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*)
		{
			g_clears.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE ClearDepth(
			ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*)
		{
			g_clears.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE Rays(ID3D12GraphicsCommandList4*, const D3D12_DISPATCH_RAYS_DESC*)
		{
			g_rays.fetch_add(1, std::memory_order_relaxed);
		}

		void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
		{
			for (auto& entry : g_queues)
			{
				if (entry.table.load(std::memory_order_acquire) != queue->lpVtbl) continue;
				const auto native = entry.execute.load(std::memory_order_acquire);
				g_queueSubmissions.fetch_add(1, std::memory_order_relaxed);
				g_submittedLists.fetch_add(count, std::memory_order_relaxed);
				if (!entry.sampled.exchange(true, std::memory_order_relaxed))
				{
					D3D12_COMMAND_QUEUE_DESC desc{};
					queue->lpVtbl->GetDesc(queue, &desc);
					void* frames[8]{};
					const USHORT depth = CaptureStackBackTrace(1, 8, frames, nullptr);
					const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
					const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
					const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
					const auto imageSize = image->OptionalHeader.SizeOfImage;
					LOG_INFO("Headless",
						"native queue work: type=%u command_lists=%u; retained copies/barriers/queries/fences execute on the selected adapter",
						static_cast<unsigned>(desc.Type), count);
					for (USHORT i = 0; i < depth; ++i)
					{
						const auto pc = reinterpret_cast<uintptr_t>(frames[i]);
						LOG_INFO("Headless", "queue caller[%u]=%p host_RVA=0x%llX", i, frames[i],
							pc >= base && pc - base < imageSize ? pc - base : 0);
					}
				}
				native(queue, count, lists);
				return;
			}
		}

		bool AttachQueue(void* object)
		{
			auto* queue = static_cast<ID3D12CommandQueue*>(object);
			auto* table = const_cast<ID3D12CommandQueueVtbl*>(queue->lpVtbl);
			std::lock_guard lock(g_tablesMutex);
			for (auto& entry : g_queues)
			{
				if (entry.table.load(std::memory_order_acquire) == table) return Replace(&table->ExecuteCommandLists, &Execute);
			}
			for (auto& entry : g_queues)
			{
				if (entry.table.load(std::memory_order_relaxed)) continue;
				entry.execute.store(table->ExecuteCommandLists, std::memory_order_release);
				entry.table.store(table, std::memory_order_release);
				// Publish originals before interception. Signals, waits and fences are
				// untouched, and submission remains native to maintain resource state.
				return Replace(&table->ExecuteCommandLists, &Execute);
			}
			return false;
		}

		// Derive slots and prototypes from the SDK; do not use guessed driver offsets.
		static_assert(offsetof(ID3D12DeviceVtbl, CreateCommandList) == 12 * sizeof(void*));
		static_assert(offsetof(ID3D12GraphicsCommandListVtbl, DrawInstanced) == 12 * sizeof(void*));
		static_assert(offsetof(ID3D12GraphicsCommandListVtbl, ExecuteIndirect) == 59 * sizeof(void*));

		bool PatchListTable(void* object, bool hasRays)
		{
			std::lock_guard lock(g_tablesMutex);
			auto* list = static_cast<ID3D12GraphicsCommandList*>(object);
			auto* table = const_cast<ID3D12GraphicsCommandListVtbl*>(list->lpVtbl);
			if (table->Dispatch != &Dispatch)
			{
				auto it = std::find_if(g_dispatchTables.begin(), g_dispatchTables.end(),
					[&](const auto& e)
					{
						return e.table == table || !e.table;
					});
				if (it == g_dispatchTables.end()) return false;
				*it = { table, table->Dispatch };
			}
			bool ok = Replace(&table->DrawInstanced, &Draw);
			ok = Replace(&table->DrawIndexedInstanced, &DrawIndexed) && ok;
			ok = Replace(&table->Dispatch, &Dispatch) && ok;
			ok = Replace(&table->ExecuteIndirect, &Indirect) && ok;
			ok = Replace(&table->ClearRenderTargetView, &ClearColor) && ok;
			ok = Replace(&table->ClearDepthStencilView, &ClearDepth) && ok;
			if (hasRays)
			{
				auto* extended = const_cast<ID3D12GraphicsCommandList4Vtbl*>(static_cast<ID3D12GraphicsCommandList4*>(object)->lpVtbl);
				ok = Replace(&extended->DispatchRays, &Rays) && ok;
			}
			// Copies, resource barriers, UAV initialization, queries, Close/Reset,
			// queue submission and fences must remain real operations.
			return ok;
		}

		bool AttachList(void* object)
		{
			auto* unknown = static_cast<IUnknown*>(object);
			// Replay requests the base interface at all three creation sites. Also
			// cover the versioned interfaces it can acquire with QueryInterface.
			const IID* ids[] = { &IID_ID3D12GraphicsCommandList, &IID_ID3D12GraphicsCommandList1, &IID_ID3D12GraphicsCommandList2,
				&IID_ID3D12GraphicsCommandList3, &IID_ID3D12GraphicsCommandList4, &IID_ID3D12GraphicsCommandList5 };
			bool ok = true;
			bool found = false;
			for (size_t i = 0; i < std::size(ids); ++i)
			{
				void* queried = nullptr;
				const HRESULT result = unknown->lpVtbl->QueryInterface(unknown, *ids[i], &queried);
				if (SUCCEEDED(result) && queried)
				{
					found = true;
					ok = PatchListTable(queried, i >= 4) && ok;
					auto* acquired = static_cast<IUnknown*>(queried);
					acquired->lpVtbl->Release(acquired);
				}
				else if (result != E_NOINTERFACE)
				{
					ok = false;
				}
			}
			if (ok && found)
			{
				g_lists.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			LOG_ERR("Headless", "D3D12 command-list interception failed; rejecting this list");
			return false;
		}

		DeviceTable Originals(const void* table)
		{
			std::lock_guard lock(g_tablesMutex);
			for (const auto& entry : g_devices)
				if (entry.table == table) return entry;
			return {};
		}

#ifdef IW8X_ISLAND_PORT_PROBE
		ID3D12InfoQueue* BeginValidation(ID3D12Device* device, UINT64& first_message)
		{
			ID3D12InfoQueue* queue{};
			if (FAILED(device->lpVtbl->QueryInterface(device, IID_ID3D12InfoQueue, reinterpret_cast<void**>(&queue)))) return nullptr;
			first_message = queue->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(queue);
			// Grow in batches; resizing the validation queue at every resource
			// would make diagnostic startup dominate the native loading work.
			if (queue->lpVtbl->GetMessageCountLimit(queue) < first_message + 256)
				queue->lpVtbl->SetMessageCountLimit(queue, first_message + 16384);
			return queue;
		}

		void ReportValidation(ID3D12InfoQueue* queue, UINT64 first_message)
		{
			if (!queue) return;
			const auto count = queue->lpVtbl->GetNumStoredMessagesAllowedByRetrievalFilter(queue);
			for (auto i = first_message; i < count && i - first_message < 4096; ++i)
			{
				alignas(D3D12_MESSAGE) std::array<unsigned char, 4096> buffer{};
				SIZE_T size = buffer.size();
				auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
				if (SUCCEEDED(queue->lpVtbl->GetMessage(queue, i, message, &size)) &&
					message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR && unsigned(message->ID) != 1319)
					LOG_ERR("IslandPipelineProbe", "D3D12 id=%u: %s", unsigned(message->ID), message->pDescription);
			}
		}

		void ReportResource(const char* operation, const D3D12_RESOURCE_DESC* desc)
		{
			static std::atomic<unsigned> failures{};
			if (!desc || failures.fetch_add(1) >= 16) return;
			LOG_ERR("IslandResourceProbe", "%s dimension=%u alignment=%llu width=%llu height=%u depth=%u mips=%u format=%u samples=%u/%u layout=%u flags=0x%X",
				operation, unsigned(desc->Dimension), desc->Alignment, desc->Width, desc->Height, desc->DepthOrArraySize,
				desc->MipLevels, unsigned(desc->Format), desc->SampleDesc.Count, desc->SampleDesc.Quality,
				unsigned(desc->Layout), unsigned(desc->Flags));
			void* frames[8]{};
			const auto depth = CaptureStackBackTrace(1, 8, frames, nullptr);
			const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			for (unsigned i = 0; i < depth; ++i)
			{
				const auto address = reinterpret_cast<uintptr_t>(frames[i]);
				if (address >= base && address - base < 0x1324B000)
					LOG_ERR("IslandResourceProbe", "caller[%u] game_RVA=0x%llX", i, address - base);
			}
		}

		HRESULT STDMETHODCALLTYPE CreateCommitted(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap,
			D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
			const D3D12_CLEAR_VALUE* clear, REFIID iid, void** output)
		{
			const auto native = Originals(device->lpVtbl).createCommitted;
			if (!native) return E_UNEXPECTED;
			const auto result = native(device, heap, flags, desc, state, clear, iid, output);
			if (FAILED(result))
			{
				UINT64 first_message{};
				auto* queue = BeginValidation(device, first_message);
				ReportResource("CreateCommittedResource", desc);
				ReportValidation(queue, first_message > 32 ? first_message - 32 : 0);
				if (queue) queue->lpVtbl->Release(queue);
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE CreatePlaced(ID3D12Device* device, ID3D12Heap* heap, UINT64 offset,
			const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear, REFIID iid, void** output)
		{
			const auto native = Originals(device->lpVtbl).createPlaced;
			if (!native) return E_UNEXPECTED;
			const auto result = native(device, heap, offset, desc, state, clear, iid, output);
			if (FAILED(result))
			{
				UINT64 first_message{};
				auto* queue = BeginValidation(device, first_message);
				ReportResource("CreatePlacedResource", desc);
				ReportValidation(queue, first_message > 32 ? first_message - 32 : 0);
				if (queue) queue->lpVtbl->Release(queue);
			}
			return result;
		}

		D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE Allocation(ID3D12Device* device, D3D12_RESOURCE_ALLOCATION_INFO* output,
			UINT visible, UINT count, const D3D12_RESOURCE_DESC* desc)
		{
			const auto native = Originals(device->lpVtbl).allocation;
			if (!native) return output;
			const auto result = native(device, output, visible, count, desc);
			if (result && result->SizeInBytes == UINT64_MAX)
			{
				UINT64 first_message{};
				auto* queue = BeginValidation(device, first_message);
				if (count) ReportResource("GetResourceAllocationInfo", desc);
				ReportValidation(queue, first_message > 32 ? first_message - 32 : 0);
				if (queue) queue->lpVtbl->Release(queue);
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE CreateRoot(ID3D12Device* device, UINT node, const void* bytes, SIZE_T size,
			REFIID iid, void** output)
		{
			const auto native = Originals(device->lpVtbl).createRoot;
			if (!native) return E_UNEXPECTED;
			const auto result = native(device, node, bytes, size, iid, output);
			static std::mutex capture_lock;
			static unsigned captures{};
			std::lock_guard lock(capture_lock);
			if (!bytes || !size || size > 65536 || captures++ >= 32) return result;
			wchar_t image[MAX_PATH]{};
			if (!GetModuleFileNameW(nullptr, image, MAX_PATH)) return result;
			const auto path = std::filesystem::path(image).parent_path() / L"mw120rproxy.root-signatures.bin";
			std::ofstream file(path, std::ios::binary | std::ios::app);
			const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			void* frames[8]{};
			const auto depth = CaptureStackBackTrace(1, 8, frames, nullptr);
			const std::array<uint32_t, 4> header{ 0x52535049, node, static_cast<uint32_t>(size), static_cast<uint32_t>(result) };
			file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
			const uint64_t object = SUCCEEDED(result) && output ? reinterpret_cast<uintptr_t>(*output) : 0;
			file.write(reinterpret_cast<const char*>(&object), sizeof(object));
			for (unsigned i = 0; i < 8; ++i)
			{
				const uint64_t rva = i < depth && reinterpret_cast<uintptr_t>(frames[i]) >= base &&
					reinterpret_cast<uintptr_t>(frames[i]) - base < 0x1324B000
					? reinterpret_cast<uintptr_t>(frames[i]) - base : 0;
				file.write(reinterpret_cast<const char*>(&rva), sizeof(rva));
			}
			file.write(static_cast<const char*>(bytes), size);
			LOG_INFO("IslandRootProbe", "native root=%p bytes=%zu hr=0x%08X captured=%u", reinterpret_cast<void*>(object), size,
				unsigned(result), unsigned(file.good()));
			return result;
		}

		void CapturePipeline(unsigned index, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
		{
			const std::array stages{ desc.VS, desc.PS, desc.HS, desc.DS, desc.GS };
			for (const auto& stage : stages)
			{
				if (stage.BytecodeLength > 8 * 1024 * 1024 || (stage.BytecodeLength && !stage.pShaderBytecode)) return;
			}
			wchar_t executable[MAX_PATH]{};
			if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return;
			void* frames[8]{};
			const auto depth = CaptureStackBackTrace(1, 8, frames, nullptr);
			const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			std::array<uint64_t, 8> callers{};
			for (unsigned i = 0; i < depth; ++i)
			{
				const auto address = reinterpret_cast<uintptr_t>(frames[i]);
				if (address >= base && address - base < image->OptionalHeader.SizeOfImage) callers[i] = address - base;
			}
			const std::array<uint32_t, 4> header{ 0x50535049, index, desc.InputLayout.NumElements, desc.NumRenderTargets };
			const auto root = reinterpret_cast<uint64_t>(desc.pRootSignature);
			std::array<uint64_t, 5> lengths{};
			for (unsigned i = 0; i < stages.size(); ++i) lengths[i] = stages[i].BytecodeLength;
			static std::mutex capture_mutex;
			std::lock_guard lock(capture_mutex);
			std::ofstream capture(std::filesystem::path(executable).parent_path() / L"mw120rproxy.pipeline-shaders.bin",
				std::ios::binary | std::ios::app);
			capture.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
			capture.write(reinterpret_cast<const char*>(&root), sizeof(root));
			capture.write(reinterpret_cast<const char*>(callers.data()), sizeof(callers));
			capture.write(reinterpret_cast<const char*>(lengths.data()), sizeof(lengths));
			for (const auto& stage : stages)
			{
				if (stage.BytecodeLength) capture.write(static_cast<const char*>(stage.pShaderBytecode), stage.BytecodeLength);
			}
			LOG_INFO("IslandPipelineProbe", "captured failing pipeline=%u shaders=%zu/%zu/%zu/%zu/%zu inputs=%u",
				index, desc.VS.BytecodeLength, desc.PS.BytecodeLength, desc.HS.BytecodeLength, desc.DS.BytecodeLength,
				desc.GS.BytecodeLength, desc.InputLayout.NumElements);
		}

		HRESULT STDMETHODCALLTYPE CreateGraphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
			REFIID iid, void** output)
		{
			const auto native = Originals(device->lpVtbl).createGraphics;
			if (!native) return E_UNEXPECTED;
			UINT64 first_message{};
			auto* queue = BeginValidation(device, first_message);
			const auto result = native(device, desc, iid, output);
			static std::atomic<unsigned> failures{};
			const auto failure = FAILED(result) && desc ? failures.fetch_add(1) : 8;
			if (failure < 8)
			{
				CapturePipeline(failure, *desc);
				LOG_ERR("IslandPipelineProbe", "native PSO failed: hr=0x%08X VS=%zu PS=%zu HS=%zu DS=%zu inputs=%u targets=%u",
					unsigned(result), desc->VS.BytecodeLength, desc->PS.BytecodeLength, desc->HS.BytecodeLength,
					desc->DS.BytecodeLength, desc->InputLayout.NumElements, desc->NumRenderTargets);
				LOG_ERR("IslandPipelineProbe", "descriptor: root=%p cached=%zu topology=%u DSV=%u RTV0=%u RTV1=%u samples=%u mask=0x%X flags=%u",
					desc->pRootSignature, desc->CachedPSO.CachedBlobSizeInBytes, unsigned(desc->PrimitiveTopologyType),
					unsigned(desc->DSVFormat), unsigned(desc->RTVFormats[0]), unsigned(desc->RTVFormats[1]),
					desc->SampleDesc.Count, desc->SampleMask, unsigned(desc->Flags));
				ReportValidation(queue, first_message);
			}
			if (queue) queue->lpVtbl->Release(queue);
			return result;
		}
#endif

		HRESULT STDMETHODCALLTYPE CreateQueue(ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC* desc, REFIID iid, void** output)
		{
			const auto native = Originals(device->lpVtbl).createQueue;
			if (!native) return E_UNEXPECTED;
			const HRESULT result = native(device, desc, iid, output);
			if (SUCCEEDED(result) && output && *output && !AttachQueue(*output))
				LOG_ERR("Headless", "could not attach D3D12 queue diagnostics; native queue preserved");
			return result;
		}

		HRESULT FinishCreate(HRESULT result, void** output)
		{
			if (SUCCEEDED(result) && output && *output && !AttachList(*output))
			{
				auto* unknown = static_cast<IUnknown*>(*output);
				unknown->lpVtbl->Release(unknown);
				*output = nullptr;
				return E_FAIL;
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE CreateList(ID3D12Device* device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type,
			ID3D12CommandAllocator* allocator, ID3D12PipelineState* state, REFIID iid, void** output)
		{
			const auto original = Originals(device->lpVtbl).create;
			if (!original) return E_UNEXPECTED;
			return FinishCreate(original(device, nodeMask, type, allocator, state, iid, output), output);
		}

		HRESULT STDMETHODCALLTYPE CreateList1(
			ID3D12Device4* device, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID iid, void** output)
		{
			const auto original = Originals(device->lpVtbl).create1;
			if (!original) return E_UNEXPECTED;
			return FinishCreate(original(device, nodeMask, type, flags, iid, output), output);
		}

		bool PatchDeviceTable(void* object, bool hasCreate1)
		{
			std::lock_guard lock(g_tablesMutex);
			auto* table = const_cast<ID3D12DeviceVtbl*>(static_cast<ID3D12Device*>(object)->lpVtbl);
			DeviceTable* entry = nullptr;
			for (auto& candidate : g_devices)
			{
				if (candidate.table == table)
				{
					entry = &candidate;
					break;
				}
				if (!entry && !candidate.table) entry = &candidate;
			}
			if (!entry) return false;
			entry->table = table;
			if (!entry->create) entry->create = table->CreateCommandList;
			if (!entry->createQueue) entry->createQueue = table->CreateCommandQueue;
			bool ok = Replace(&table->CreateCommandList, &CreateList);
			ok = Replace(&table->CreateCommandQueue, &CreateQueue) && ok;
#ifdef IW8X_ISLAND_PORT_PROBE
			if (!entry->createGraphics) entry->createGraphics = table->CreateGraphicsPipelineState;
			ok = Replace(&table->CreateGraphicsPipelineState, &CreateGraphics) && ok;
			if (!entry->createRoot) entry->createRoot = table->CreateRootSignature;
			ok = Replace(&table->CreateRootSignature, &CreateRoot) && ok;
			if (!entry->createCommitted) entry->createCommitted = table->CreateCommittedResource;
			ok = Replace(&table->CreateCommittedResource, &CreateCommitted) && ok;
			if (!entry->createPlaced) entry->createPlaced = table->CreatePlacedResource;
			ok = Replace(&table->CreatePlacedResource, &CreatePlaced) && ok;
			if (!entry->allocation) entry->allocation = table->GetResourceAllocationInfo;
			ok = Replace(&table->GetResourceAllocationInfo, &Allocation) && ok;
#endif
			if (hasCreate1)
			{
				auto* extended = const_cast<ID3D12Device4Vtbl*>(static_cast<ID3D12Device4*>(object)->lpVtbl);
				if (!entry->create1) entry->create1 = extended->CreateCommandList1;
				ok = Replace(&extended->CreateCommandList1, &CreateList1) && ok;
			}
			return ok;
		}
	}

#ifdef IW8X_ISLAND_PORT_PROBE
	void EnableProbeDiagnostics()
	{
		static std::once_flag enabled;
		std::call_once(enabled, []
		{
			wchar_t skip[2]{};
			if (GetEnvironmentVariableW(L"IW8X_ISLAND_SKIP_GPU_VALIDATION", skip, 2) == 1 && skip[0] == L'1')
			{
				LOG_INFO("IslandPipelineProbe", "native device creation retained without diagnostic GPU validation");
				return;
			}
			const auto runtime = GetModuleHandleW(L"d3d12.dll");
			using get_debug_fn = HRESULT(WINAPI*)(REFIID, void**);
			const auto get_debug = reinterpret_cast<get_debug_fn>(GetProcAddress(runtime, "D3D12GetDebugInterface"));
			ID3D12Debug* debug{};
			if (get_debug && SUCCEEDED(get_debug(IID_ID3D12Debug, reinterpret_cast<void**>(&debug))))
			{
				debug->lpVtbl->EnableDebugLayer(debug);
				debug->lpVtbl->Release(debug);
				LOG_INFO("IslandPipelineProbe", "D3D12 validation enabled before native device creation");
			}
			else LOG_WARN("IslandPipelineProbe", "D3D12 debug interface unavailable");
		});
	}
#endif

	bool AttachDevice(void* device)
	{
		if (!device) return false;
		auto* unknown = static_cast<IUnknown*>(device);
		const IID* ids[] = { &IID_ID3D12Device, &IID_ID3D12Device1, &IID_ID3D12Device2, &IID_ID3D12Device3, &IID_ID3D12Device4,
			&IID_ID3D12Device5 };
		bool ok = true;
		bool found = false;
		for (size_t i = 0; i < std::size(ids); ++i)
		{
			void* queried = nullptr;
			const HRESULT result = unknown->lpVtbl->QueryInterface(unknown, *ids[i], &queried);
			if (SUCCEEDED(result) && queried)
			{
				found = true;
				ok = PatchDeviceTable(queried, i >= 4) && ok;
				auto* acquired = static_cast<IUnknown*>(queried);
				acquired->lpVtbl->Release(acquired);
			}
			else if (result != E_NOINTERFACE)
			{
				ok = false;
			}
		}
		if (ok && found)
			LOG_INFO("Headless", "D3D12 device intercepted before command-list creation; frame drawing disabled");
		else
			LOG_ERR("Headless", "D3D12 device interception failed");
		return ok && found;
	}

	bool DispatchCompute(void* object, unsigned x, unsigned y, unsigned z)
	{
		auto* list = static_cast<ID3D12GraphicsCommandList*>(object);
		if (!list) return false;
		DispatchFn function = nullptr;
		{
			std::lock_guard lock(g_tablesMutex);
			function = list->lpVtbl->Dispatch;
			if (function == &Dispatch)
			{
				function = nullptr;
				for (const auto& e : g_dispatchTables)
					if (e.table == list->lpVtbl)
					{
						function = e.function;
						break;
					}
			}
		}
		if (!function) return false;
		function(list, x, y, z);
		return true;
	}

	Counters ReadCounters()
	{
		return { g_lists.load(std::memory_order_relaxed), g_draws.load(std::memory_order_relaxed),
			g_dispatches.load(std::memory_order_relaxed), g_indirect.load(std::memory_order_relaxed),
			g_clears.load(std::memory_order_relaxed), g_rays.load(std::memory_order_relaxed),
			g_queueSubmissions.load(std::memory_order_relaxed), g_submittedLists.load(std::memory_order_relaxed) };
	}
}
