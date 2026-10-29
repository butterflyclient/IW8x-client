#include "diagnostics/island/island_render_formats.hpp"
#include "diagnostics/island/island_depth_quickfix.hpp"
#include "diagnostics/island/island_renderer_adapter.hpp"

#include "common/console.h"
#include "diagnostics/night/night_files.h"
#include "engine/safemem.h"

#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <Windows.h>

namespace island_render_formats
{
	namespace
	{
		struct pipeline
		{
			std::uint64_t hash;
			void* object;
		};
		struct variant
		{
			std::array<std::uint8_t, 184> header{};
			std::array<pipeline, 2> pipelines{};
			std::array<std::uint8_t, 5> format{0, 32, 1, 0, 0};
			std::array<std::uint64_t, 2> original_hashes{};
		};
		using setup_fn = bool(*)(std::uint8_t*);
		using create_fn = void(*)(void*, const char*, int);
		std::atomic<setup_fn> original{};
		create_fn create{};
		std::uintptr_t image_base{};
		std::mutex mutex;
		// Native PSO jobs and command lists may retain these handles. Keep the
		// variant storage alive for the process, matching the native cache.
		std::unordered_map<std::uint64_t, std::unique_ptr<variant>> variants;
		// Do not synchronously retry an unsupported pipeline every frame.
		std::unordered_map<std::uint64_t, std::array<std::uint64_t, 2>> failed_variants;
		std::atomic<bool> adapter_first_observation{};
		template <typename T>
		T get(const void* pointer, std::size_t offset);

		// Validate the *active* Replay world, not merely that the Island bundle
		// was configured. The same game process may subsequently render a native
		// 1.20 map or return to the lobby.
		bool island_world_active()
		{
			std::uintptr_t world{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(image_base + 0xb8f5a80 + 31u * sizeof(void*)),
					&world, sizeof(world)) || !world) return false;
			const char* world_name{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(world), &world_name, sizeof(world_name)) || !world_name)
				return false;
			constexpr char expected[] = "maps/mp/mp_sm_island_1.d3dbsp";
			std::array<char, sizeof(expected)> actual{};
			return safemem::ReadBytes(world_name, actual.data(), actual.size()) &&
				!std::memcmp(actual.data(), expected, actual.size());
		}

		void observe_island_setup(const std::uint8_t* state, const std::uint8_t* technique)
		{
			if (!island_renderer_adapter::enabled() || !state[0x1134] || !technique || !island_world_active())
				return;
			const auto type = get<unsigned>(technique, 8);
			if (type != 0 && type != 34) return;
			const auto* material = get<const std::uint8_t*>(state, 0x1138);
			if (!material) return;
			const auto* name = get<const char*>(material, 0);
			if (!name) return;
			std::array<char, 5> prefix{};
			if (!safemem::ReadBytes(name, prefix.data(), prefix.size())) return;
			const bool imported = !std::memcmp(prefix.data(), "tmo/", 4) ||
				!std::memcmp(prefix.data(), "tmco/", 5);
			const auto result = island_renderer_adapter::classify(
				{"mp_sm_island_1", imported, false /* requires verified same-instance prepass coverage */});
			if (imported && !adapter_first_observation.exchange(true))
				LOG_INFO("IslandRendererAdapter", "observing imported model technique type=%u; policy eligible=%u depth_translate=%u (fail closed)",
					type, unsigned(result.eligible), unsigned(result.translate_depth));
		}

		template <typename T>
		T get(const void* pointer, std::size_t offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const std::uint8_t*>(pointer) + offset, sizeof(value));
			return value;
		}

		variant* matching_variant(const std::uint8_t* technique)
		{
			const auto layouts = technique[0xe];
			if (layouts != 2) return nullptr;
			const auto* old = get<const pipeline*>(technique, 0x28);
			if (!old) return nullptr;
			// The shipping cache keys complete pipeline hashes, rather than the
			// address of a serialized record. A format change needs a distinct key.
			const auto key = old[0].hash ^ 0x49534c414e443320ull;
			const std::array<std::uint64_t, 2> original_hashes{old[0].hash, old[1].hash};
			std::lock_guard lock(mutex);
			if (const auto found = variants.find(key); found != variants.end())
			{
				for (unsigned i = 0; i < layouts; ++i)
					if (found->second->original_hashes[i] != old[i].hash) return nullptr;
				return found->second.get();
			}
			if (const auto failed = failed_variants.find(key);
				failed != failed_variants.end() && failed->second == original_hashes)
				return nullptr;
			auto value = std::make_unique<variant>();
			std::memcpy(value->header.data(), technique, value->header.size());
			for (unsigned i = 0; i < layouts; ++i)
			{
				value->original_hashes[i] = old[i].hash;
				value->pipelines[i].hash = old[i].hash ^ (0x49534c414e443320ull + i);
			}
			value->header[0x83] = 1;
			const auto pipelines = value->pipelines.data();
			const auto formats = value->format.data();
			std::memcpy(value->header.data() + 0x28, &pipelines, sizeof(pipelines));
			std::memcpy(value->header.data() + 0x88, &formats, sizeof(formats));
			auto flags = get<std::uint16_t>(technique, 0xc) & 0x7fff;
			const auto native_flags = static_cast<std::uint16_t>(flags);
			std::memcpy(value->header.data() + 0xc, &native_flags, sizeof(native_flags));
			// Called on the real render thread. Preserve native shader/input-layout
			// compilation and its driver cache; no foreign-thread game TLS access.
			create(value->header.data(), "Island imported smoke HDR format", -1);
			for (unsigned i = 0; i < layouts; ++i)
				if (!value->pipelines[i].object)
				{
					failed_variants[key] = original_hashes;
					return nullptr;
				}
			auto* result = value.get();
			variants.emplace(key, std::move(value));
			LOG_INFO("IslandRenderFormats", "created native smoke HDR pipeline variant: layouts=%u format=32", unsigned(layouts));
			return result;
		}

		bool setup(std::uint8_t* state)
		{
			const auto* technique = get<const std::uint8_t*>(state, 0x1140);
			observe_island_setup(state, technique);
			variant* replacement{};
			if (state[0x1134] && technique && get<unsigned>(technique, 8) == 30 && technique[0xe] == 2 &&
				technique[0x82] == 1 && technique[0x83] == 3 && get<std::uint32_t>(technique, 0xa0) == 0 &&
				get<std::uint64_t>(technique, 0xa8) == 0x28054)
			{
				const auto* material = get<const std::uint8_t*>(state, 0x1138);
				const auto name = get<const char*>(technique, 0);
				const auto material_name = material ? get<const char*>(material, 0) : nullptr;
				constexpr std::array<std::uint8_t, 15> expected{0,30,0,0,0, 0,7,0,0,0, 0,16,0,0,0};
				const auto* formats = get<const std::uint8_t*>(technique, 0x88);
				const auto* pool = *reinterpret_cast<const std::uint8_t* const*>(image_base + 0x11151fa0);
				const auto id = get<std::uint16_t>(state, 0x111e) & 0x7fff;
				const auto second_id = get<std::uint16_t>(state, 0x1120) & 0x7fff;
				if (name && material_name && !std::strcmp(name, "TECHNIQUE_EMISSIVE_a4a6f484c07c3a2e") &&
					!std::strcmp(material_name, "elcq/vfx_vol_smoke_ball_soft_loop_1") && formats &&
					!std::memcmp(formats, expected.data(), expected.size()) && id && second_id && pool && state[0x111c] == 2 &&
					(get<std::uint16_t>(state, 0x1126) & 0x7fff) == 0 &&
					pool[(id - 1) * 0x1f8 + 0x2c] == 32 && pool[(second_id - 1) * 0x1f8 + 0x2c] == 1)
					replacement = matching_variant(technique);
			}
			if (replacement)
			{
				const auto* pointer = replacement->header.data();
				std::memcpy(state + 0x1140, &pointer, sizeof(pointer));
			}
			const auto result = original.load(std::memory_order_acquire)(state);
			if (replacement) std::memcpy(state + 0x1140, &technique, sizeof(technique));
			return result;
		}
	}

	hook::Status install(std::uintptr_t base)
	{
		if (!night_files::MapEnabled("mp_sm_island_1")) return hook::Status::Installed;
		// The verified Island-only keystone repair is independent of the old,
		// global smoke/depth experiments. Enable only the exact-material PSO
		// replacement by default. Set IW8X_ISLAND_DEPTH_QUICKFIX=0 to opt out;
		// other maps still cannot install this hook.
		char repair_setting[16]{};
		const auto repair_length = GetEnvironmentVariableA("IW8X_ISLAND_DEPTH_QUICKFIX",
			repair_setting, static_cast<DWORD>(sizeof(repair_setting)));
		if (repair_length == 0 || (repair_length == 5 &&
			!std::strcmp(repair_setting, "arch8")) ||
			(repair_length == 9 && !std::strcmp(repair_setting, "arch8both")))
			return island_depth_quickfix::install(base);
		// Production/default Island loads must not intercept the renderer every
		// frame. These legacy shader/depth probes are diagnostic-only until a
		// verified asset-side conversion replaces their temporary PSO changes.
		// Opt in explicitly before launch to reproduce an old capture:
		// IW8X_ISLAND_RUNTIME_HOOKS=1
		char hook_setting[4]{};
		const bool diagnostic_hooks =
			GetEnvironmentVariableA("IW8X_ISLAND_RUNTIME_HOOKS", hook_setting,
				static_cast<DWORD>(sizeof(hook_setting))) == 1 && hook_setting[0] == '1';
		if (!diagnostic_hooks)
			return hook::Status::Installed;
		// Explicit opt-in for the Island-only adapter. The current policy merely
		// audits eligibility; it never changes depth/raster state until source
		// and Replay depth coverage have been matched for the *same* instance.
		char enabled[4]{};
		const bool requested = GetEnvironmentVariableA("IW8X_ISLAND_RENDER_ADAPTER", enabled,
			static_cast<DWORD>(sizeof(enabled))) == 1 && enabled[0] == '1';
		island_renderer_adapter::set_enabled(requested);
		if (requested)
			LOG_INFO("IslandRendererAdapter", "Island-specific adapter audit enabled; native prepass, index and depth states preserved");
			if (original.load(std::memory_order_acquire))
				return image_base == base ? island_depth_quickfix::install(base) : hook::Status::Failed;
		constexpr std::array<std::uint8_t, 9> create_bytes{0x40,0x53,0x56,0x57,0xb8,0x50,0x33,0,0};
		std::array<std::uint8_t, 9> observed{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0xecb4d0), observed.data(), observed.size()) || observed != create_bytes)
			return hook::Status::Failed;
		image_base = base;
		create = reinterpret_cast<create_fn>(base + 0xecb4d0);
		constexpr std::uint8_t expected[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
			const auto status = hook::Install(reinterpret_cast<void*>(base + 0x1989850),
				&setup, expected, sizeof(expected), original);
			if (status != hook::Status::Installed) return status;
			return island_depth_quickfix::install(base);
	}
}
