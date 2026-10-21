#include "diagnostics/island/island_depth_quickfix.hpp"

#include "common/console.h"
#include "diagnostics/night/night_files.h"
#include "engine/safemem.h"

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "thirdparty/minhook/include/MinHook.h"

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

// Fast, reversible VISIBILITY workaround for imported Island 1.64 materials.
// Do not enable on any other map. Keep the native depth-prepass unchanged.
//
// This is intentionally not an attempted fix for index/vertex corruption:
// it replaces the depth comparison for a tightly verified main-color PSO
// and restores the original pipeline after each matching ExecuteIndirect.
namespace island_depth_quickfix
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		struct context { std::uint8_t* source; std::uint8_t* state; };
		using setup_fn = void(*)(context*, const char*);
		using indirect_fn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,
			ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64,
			ID3D12Resource*, UINT64);

		struct cached
		{
			const std::uint8_t* technique{};
			const void* vertex{};
			const void* pixel{};
			// A null PSO is an intentional negative cache entry. Failed PSO
			// creation must never be retried on every draw of the same shader.
			ComPtr<ID3D12PipelineState> pipeline;
		};
		struct cache_key
		{
			const std::uint8_t* technique{};
			const void* vertex{};
			const void* pixel{};
			bool operator==(const cache_key& other) const noexcept
			{
				return technique == other.technique && vertex == other.vertex && pixel == other.pixel;
			}
		};
		struct cache_key_hash
		{
			std::size_t operator()(const cache_key& key) const noexcept
			{
				const auto a = reinterpret_cast<std::uintptr_t>(key.technique);
				const auto b = reinterpret_cast<std::uintptr_t>(key.vertex);
				const auto c = reinterpret_cast<std::uintptr_t>(key.pixel);
				return static_cast<std::size_t>((a >> 4) ^ (b << 9) ^ (c >> 7));
			}
		};
		std::atomic<setup_fn> original_setup{};
		std::atomic<indirect_fn> original_indirect{};
		std::atomic<bool> indirect_installed{};
		std::mutex mutex;
		// Exact three-pointer key lookup replaces an O(512) scan performed for
		// every imported model draw on the render thread.
		std::unordered_map<cache_key, std::unique_ptr<cached>, cache_key_hash> variants;
		std::uintptr_t image_base{};
		bool enabled{};
		D3D12_COMPARISON_FUNC mode{D3D12_COMPARISON_FUNC_LESS_EQUAL};
		std::atomic<unsigned> replaced_draws{};
		std::atomic<bool> first_success{};
		std::atomic<bool> first_failure{};
		thread_local ID3D12GraphicsCommandList* active_list{};
		thread_local ID3D12PipelineState* native_pipeline{};
		thread_local ID3D12PipelineState* alternate_pipeline{};
		// All cached objects remain alive for the process. This avoids even the
		// cache mutex for consecutive draws using the same imported technique.
		thread_local cached* last_variant{};

		bool world_active()
		{
			std::uintptr_t world{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(image_base + 0xb8f5a80 + 31 * 8),
					&world, sizeof(world)) || !world) return false;
			const char* name{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(world), &name, sizeof(name)) || !name) return false;
			constexpr char expected[] = "maps/mp/mp_sm_island_1.d3dbsp";
			std::array<char, sizeof(expected)> actual{};
			return safemem::ReadBytes(name, actual.data(), actual.size()) &&
				std::memcmp(actual.data(), expected, actual.size()) == 0;
		}

		template <class T>
		bool read(const void* pointer, std::size_t offset, T& out)
		{
			return pointer && safemem::ReadBytes(
				static_cast<const std::uint8_t*>(pointer) + offset, &out, sizeof(T));
		}

		// The input was range-checked and copied into a local array once. Reading
		// individual fields from that array avoids another VirtualQuery per field.
		template <class T>
		T local_field(const std::uint8_t* snapshot, std::size_t offset)
		{
			T result{};
			std::memcpy(&result, snapshot + offset, sizeof(result));
			return result;
		}

		bool imported(const std::uint8_t* material)
		{
			const char* name{};
			std::array<char, 5> prefix{};
			return read(material, 0, name) && name &&
				safemem::ReadBytes(name, prefix.data(), prefix.size()) &&
				(!std::memcmp(prefix.data(), "tmo/", 4) || !std::memcmp(prefix.data(), "tmco/", 5));
		}

		ComPtr<ID3D12PipelineState> create_variant(
			ID3D12GraphicsCommandList* list, const std::uint8_t* technique)
		{
			ComPtr<ID3D12Device> device;
			if (!list || FAILED(list->GetDevice(IID_PPV_ARGS(&device)))) return {};

			const std::uint8_t* vertex{};
			const std::uint8_t* pixel{};
			if (!read(technique, 0x58, vertex) || !read(technique, 0x70, pixel) ||
				!vertex || !pixel) return {};
			const void* vertex_code{};
			const void* pixel_code{};
			unsigned vertex_bytes{}, pixel_bytes{};
			if (!read(vertex, 24, vertex_code) || !read(vertex, 32, vertex_bytes) ||
				!read(pixel, 24, pixel_code) || !read(pixel, 32, pixel_bytes) ||
				!vertex_code || !pixel_code || !vertex_bytes || !pixel_bytes ||
				vertex_bytes > 100000 || pixel_bytes > 100000) return {};

			ID3D12RootSignature* root{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(image_base + 0x46319d0),
				&root, sizeof(root)) || !root) return {};

			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
			desc.pRootSignature = root;
			desc.VS = {vertex_code, vertex_bytes};
			desc.PS = {pixel_code, pixel_bytes};
			auto& blend = desc.BlendState.RenderTarget[0];
			blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
			blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
			blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blend.RenderTargetWriteMask = 15;
			desc.SampleMask = UINT_MAX;
			desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
			desc.RasterizerState.DepthClipEnable = TRUE;
			desc.DepthStencilState.DepthEnable = TRUE;
			desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			desc.DepthStencilState.DepthFunc = mode;
			const D3D12_DEPTH_STENCILOP_DESC face{
				D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
				D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_NEVER
			};
			desc.DepthStencilState.FrontFace = desc.DepthStencilState.BackFace = face;
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			desc.NumRenderTargets = 2;
			desc.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
			desc.RTVFormats[1] = DXGI_FORMAT_R8_UNORM;
			desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			desc.SampleDesc.Count = 1;

			ComPtr<ID3D12PipelineState> result;
			if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&result))))
				return {};
			return result;
		}

		void STDMETHODCALLTYPE indirect(ID3D12GraphicsCommandList* list,
			ID3D12CommandSignature* signature, UINT maximum, ID3D12Resource* arguments,
			UINT64 offset, ID3D12Resource* count_buffer, UINT64 count_offset)
		{
			const bool replace = list && list == active_list && native_pipeline &&
				alternate_pipeline && world_active();
			if (replace) list->SetPipelineState(alternate_pipeline);
			original_indirect.load(std::memory_order_acquire)(
				list, signature, maximum, arguments, offset, count_buffer, count_offset);
			if (replace)
			{
				list->SetPipelineState(native_pipeline);
				if (!first_success.exchange(true))
					LOG_INFO("IslandDepthQuickfix", "first Island draw translated to %s; native PSO restored",
						mode == D3D12_COMPARISON_FUNC_ALWAYS ? "ALWAYS" : "LESS_EQUAL");
				replaced_draws.fetch_add(1, std::memory_order_relaxed);
			}
		}

		bool install_indirect(ID3D12GraphicsCommandList* list)
		{
			if (indirect_installed.load(std::memory_order_acquire)) return true;
			std::lock_guard lock(hook::detail::Mutex());
			if (indirect_installed.load(std::memory_order_acquire)) return true;
			// A failed enable must never be mistaken for a working hook.
			if (original_indirect.load(std::memory_order_acquire)) return false;
			const auto slot = (*reinterpret_cast<void***>(list))[59];
			if (!slot) return false;
			void* trampoline{};
			const auto initialized = MH_Initialize();
			if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
			if (MH_CreateHook(slot, reinterpret_cast<void*>(&indirect), &trampoline) != MH_OK)
				return false;
			// Publish the trampoline before enabling the detour on rendering threads.
			original_indirect.store(reinterpret_cast<indirect_fn>(trampoline), std::memory_order_release);
			if (MH_EnableHook(slot) != MH_OK) return false;
			indirect_installed.store(true, std::memory_order_release);
			return true;
		}

		void setup(context* ctx, const char* label)
		{
			original_setup.load(std::memory_order_acquire)(ctx, label);
			active_list = nullptr;
			native_pipeline = nullptr;
			alternate_pipeline = nullptr;
			if (!enabled || !ctx || !ctx->state) return;

			const auto* state = ctx->state;
			// These state fields form one contiguous region (0x1134..0x118f).
			// Snapshot it once instead of issuing three or more VirtualQuery calls.
			std::array<std::uint8_t, 0x5c> state_snapshot{};
			if (!safemem::ReadBytes(state + 0x1134, state_snapshot.data(),
				state_snapshot.size()) || !state_snapshot[0]) return;
			const std::uint8_t* material{};
			const std::uint8_t* technique{};
			unsigned type{};
			std::uint64_t raster_state{}, blend_state{};
			const std::uint8_t* format_table{};
			material = local_field<const std::uint8_t*>(state_snapshot.data(), 0x04);
			technique = local_field<const std::uint8_t*>(state_snapshot.data(), 0x0c);
			// Technique family 34 is the only family this workaround supports.
			if (!read(technique, 8, type) || type != 34) return;
			// Match the entire shader technique using a single validated read.
			std::array<std::uint8_t, 0xb0> technique_snapshot{};
			if (!safemem::ReadBytes(technique, technique_snapshot.data(),
				technique_snapshot.size())) return;
			raster_state = local_field<std::uint64_t>(technique_snapshot.data(), 0xa0);
			blend_state = local_field<std::uint64_t>(technique_snapshot.data(), 0xa8);
			format_table = local_field<const std::uint8_t*>(technique_snapshot.data(), 0x88);
			if (raster_state != 0x801 || blend_state ||
				technique_snapshot[0x82] != 2 || technique_snapshot[0x83] != 1 ||
				!format_table || !imported(material))
				return;
			constexpr std::uint8_t expected[5]{22, 32, 1, 0, 0};
			std::array<std::uint8_t, 5> observed{};
			if (!safemem::ReadBytes(format_table, observed.data(), observed.size()) ||
				std::memcmp(observed.data(), expected, sizeof(expected))) return;
			// Most render setups are not Island model techniques. Avoid doing
			// three VirtualQuery-backed world-name reads for those common cases.
			if (!world_active()) return;

			ID3D12GraphicsCommandList* list{};
			auto* native = local_field<ID3D12PipelineState*>(state_snapshot.data(), 0x54);
			const auto* vertex = local_field<const void*>(technique_snapshot.data(), 0x58);
			const auto* pixel = local_field<const void*>(technique_snapshot.data(), 0x70);
			if (!read(state, 0xc50, list) ||
				!list || !native || !vertex || !pixel) return;
			if (!install_indirect(list)) return;
			const cache_key key{technique, vertex, pixel};
			if (last_variant && key == cache_key{last_variant->technique,
				last_variant->vertex, last_variant->pixel})
			{
				if (!last_variant->pipeline) return;
				active_list = list;
				native_pipeline = native;
				alternate_pipeline = last_variant->pipeline.Get();
				return;
			}
			std::lock_guard lock(mutex);
			if (const auto found = variants.find(key); found != variants.end())
			{
				last_variant = found->second.get();
				if (!last_variant->pipeline) return;
				active_list = list;
				native_pipeline = native;
				alternate_pipeline = last_variant->pipeline.Get();
				return;
			}
			// Never allow an unbounded PSO cache to grow as foreign zones unload.
			if (variants.size() >= 512) return;
			auto replacement = create_variant(list, technique);
			auto value = std::make_unique<cached>();
			value->technique = technique;
			value->vertex = vertex;
			value->pixel = pixel;
			value->pipeline = std::move(replacement);
			last_variant = value.get();
			variants.emplace(key, std::move(value));
			if (!last_variant->pipeline)
			{
				if (!first_failure.exchange(true))
					LOG_INFO("IslandDepthQuickfix", "cached failed alternate PSO; skipping retries for this shader");
				return;
			}
			alternate_pipeline = last_variant->pipeline.Get();
			active_list = list;
			native_pipeline = native;
		}
	}

	hook::Status install(std::uintptr_t base)
	{
		if (!night_files::MapEnabled("mp_sm_island_1")) return hook::Status::Installed;
		char setting[16]{};
		const auto length = GetEnvironmentVariableA("IW8X_ISLAND_DEPTH_QUICKFIX", setting, sizeof(setting));
		// Only explicit 1/always permits a live D3D12 ExecuteIndirect detour.
		// No setting (and 0) leaves the native pipeline completely unhooked.
		const bool always = length == 6 && !std::strcmp(setting, "always");
		if (!(length == 1 && setting[0] == '1') && !always)
			return hook::Status::Installed;
		enabled = true;
		if (always) mode = D3D12_COMPARISON_FUNC_ALWAYS;
		image_base = base;
		constexpr std::uint8_t expected[]{
			0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0xa0, 0, 0, 0
		};
		const auto result = hook::Install(reinterpret_cast<void*>(base + 0x1982df0),
			&setup, expected, sizeof(expected), original_setup);
		if (result == hook::Status::Installed)
			LOG_INFO("IslandDepthQuickfix", "Island-only diagnostic depth preview enabled (%s); unset IW8X_ISLAND_DEPTH_QUICKFIX to disable",
				mode == D3D12_COMPARISON_FUNC_ALWAYS ? "ALWAYS" : "LESS_EQUAL");
		return result;
	}
}
