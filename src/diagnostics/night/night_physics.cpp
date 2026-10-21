#include "diagnostics/night/night_physics.h"
#include "diagnostics/night/night_physics_stub.h"
#include "diagnostics/night/night_physics_bindings.h"
#include "diagnostics/night/night_files.h"
#include "network/native_network.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <windows.h>
#include <array>
#include <mutex>
#include <new>
namespace night_physics
{
	namespace
	{
		namespace binding = night_physics_binding;
		uintptr_t g_base{};
		std::mutex g_mutex;
		SoundState g_state;
		std::atomic<bool> g_installed{ false };
		using Fn = void (*)();

		struct Stub
		{
			void* allocation{};
			std::atomic<Fn>* original{};
		};

		std::array<Stub, 3> g_stubs{};

		[[noreturn]] void invalid(const char* what)
		{
			LOG_ERR("NightPhysics", "%s", what);
			RaiseFailFastException(nullptr, nullptr, 0);
			std::terminate();
		}

		bool allow_impact(uintptr_t pipeline, uintptr_t shape, int particle, int world)
		{
			std::lock_guard lock(g_mutex);
			if (!g_state.Contains(shape)) return true;
			if (g_state.Scope(shape) == MapScope::Escape130)
			{
				const char* selected{};
				std::array<char, sizeof("mp_escape2")> name{};
				SIZE_T copied{};
				// Read only the registered collision-map name; no VirtualQuery over a
				// large streamed allocation on the per-particle hot path.
				if (!night_files::MapEnabled("mp_escape2") ||
					!ReadProcessMemory(
						GetCurrentProcess(), reinterpret_cast<void*>(g_base + 0x5CC7538), &selected, sizeof(selected), &copied) ||
					copied != sizeof(selected) || !selected ||
					!ReadProcessMemory(GetCurrentProcess(), selected, name.data(), name.size(), &copied) || copied != name.size() ||
					std::memcmp(name.data(), "mp_escape2", name.size()))
					return true;
			}
			if (!pipeline || particle < 0) invalid("invalid converted particle identity");
			const int local = reinterpret_cast<int (*)(int)>(g_base + binding::LocalClient.rva)(world);
			if (local != 0) invalid("converted collision sound outside a client physics world");
			std::array<std::uint8_t, 0x28> receiver{};
			// Native virtual receiver carries its local-client index at +20h.
			std::memcpy(receiver.data() + 0x20, &local, 4);
			const auto now = reinterpret_cast<std::uint32_t (*)(void*)>(g_base + binding::ClientClock.rva)(receiver.data());
			return g_state.Allow(pipeline, shape, particle, now);
		}

		void particles_created(uintptr_t pipeline, const int* particles, int count)
		{
			if (count < 0 || (count && !particles)) invalid("invalid particle allocation result");
			std::lock_guard lock(g_mutex);
			g_state.Created(pipeline, { particles, static_cast<size_t>(count) });
		}

		void pipeline_released(uintptr_t pipeline)
		{
			std::lock_guard lock(g_mutex);
			g_state.Released(pipeline);
		}

		bool matches(const binding::Entry& at)
		{
			std::array<std::uint8_t, 16> actual{};
			const auto pointer = reinterpret_cast<const void*>(g_base + at.rva);
			return safemem::RegionExecutable(pointer) && safemem::ReadBytes(pointer, actual.data(), actual.size()) && actual == at.bytes;
		}

		bool prepare_stub(Stub& stub, GateKind kind, uintptr_t callback, uintptr_t reject)
		{
			if (stub.allocation) return true;
			SYSTEM_INFO info{};
			GetSystemInfo(&info);
			const size_t page = info.dwPageSize;
			auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			if (!memory) return false;
			auto* original = new (memory + page) std::atomic<Fn>{ nullptr };
			const auto code = MakeStub(kind, reinterpret_cast<uintptr_t>(memory), callback, reinterpret_cast<uintptr_t>(original), reject);
			if (code.empty() || code.size() > page)
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return false;
			}
			std::memcpy(memory, code.data(), code.size());
			DWORD previous{};
			if (!VirtualProtect(memory, page, PAGE_EXECUTE_READ, &previous) ||
				!FlushInstructionCache(GetCurrentProcess(), memory, code.size()))
			{
				VirtualFree(memory, 0, MEM_RELEASE);
				return false;
			}
			stub = { memory, original };
			return true;
		}
	}

	bool RegisterShape(uintptr_t shape, SoundRule rule)
	{
		std::lock_guard lock(g_mutex);
		return g_installed.load(std::memory_order_acquire) && g_state.Register(shape, rule);
	}

	void UnregisterShape(uintptr_t shape)
	{
		std::lock_guard lock(g_mutex);
		g_state.Unregister(shape);
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed.load(std::memory_order_acquire)) return hook::Status::Installed;
		wchar_t config[MAX_PATH]{};
		if (!native_network::ConfigurationPath(config, MAX_PATH)) return hook::Status::Failed;
			if (!night_files::MapEnabled("mp_escape2") && !night_files::MapEnabled("mp_kstenod"))
			return hook::Status::Installed;
		g_base = base;
		for (const auto& at :
			{ binding::CollisionGate, binding::CreatedGate, binding::ReleasedGate, binding::LocalClient, binding::ClientClock })
			if (!matches(at))
			{
				LOG_ERR("NightPhysics", "native binding mismatch at RVA %llX", static_cast<unsigned long long>(at.rva));
				return hook::Status::Failed;
			}
		// Creation resets before the writer sees new particles. Release erases
		// state after collider destruction and before the pipeline can be freed.
		if (!prepare_stub(g_stubs[0], GateKind::Collision, reinterpret_cast<uintptr_t>(&allow_impact), base + 0x1B486DD) ||
			!prepare_stub(g_stubs[1], GateKind::Created, reinterpret_cast<uintptr_t>(&particles_created), 0) ||
			!prepare_stub(g_stubs[2], GateKind::Released, reinterpret_cast<uintptr_t>(&pipeline_released), 0))
			return hook::Status::Failed;
		hook::Batch batch;
		const std::array entries{ binding::CollisionGate, binding::CreatedGate, binding::ReleasedGate };
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const auto& at = entries[i];
			batch.Add(reinterpret_cast<void*>(base + at.rva), reinterpret_cast<Fn>(g_stubs[i].allocation), at.bytes.data(), at.bytes.size(),
				*g_stubs[i].original);
		}
		const auto status = batch.Commit();
		if (status == hook::Status::Installed)
		{
			g_installed.store(true, std::memory_order_release);
			LOG_INFO("NightPhysics", "original particle sound timing and lifetime bridge ready; converted shape admission required");
		}
		return status;
	}
}
