#include "diagnostics/night/night_movers.h"
#include "diagnostics/night/night_movers_state.h"
#include "diagnostics/night/night_mover_bindings.h"
#include "diagnostics/night/night_entity_bindings.h"
#include "diagnostics/night/night_high_lod_bindings.h"
#include "diagnostics/night/night_playlist_bindings.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include "engine/safemem.h"
#include <intrin.h>
namespace night_movers
{
	namespace
	{
		namespace binding = night_mover_binding;
		uintptr_t imageBase{};
		Registry registry;
		using VoidFn = void (*)();
		using EntityFn = void (*)(void*);
		using PhysicsFn = bool (*)(void*, bool);
		std::atomic<VoidFn> beforePhysics{}, destroyAll{};
		std::atomic<EntityFn> destroy{}, linked{};
		std::atomic<PhysicsFn> physics{};
		thread_local void* linkedWithoutWarp{};
		bool installed{};
		bool reportedFrame{};

		template <class Fn>
		Fn native(uintptr_t rva)
		{
			return reinterpret_cast<Fn>(imageBase + rva);
		}

		bool read_map_bytes(const void* source, void* destination, std::size_t size)
		{
			// VirtualQuery walks the whole contiguous region. The live map-name
			// allocation spans 579 MB; querying it costs 8-10 ms on every mover call.
			// Read only the requested bytes, retaining inaccessible-pointer failure
			// without walking that allocation or caching a name across map changes.
			const auto error = GetLastError();
			SIZE_T copied{};
			const bool ok = source && ReadProcessMemory(GetCurrentProcess(), source, destination, size, &copied) && copied == size;
			SetLastError(error);
			return ok;
		}

		bool is_night()
		{
			if (!night_files::Enabled()) return false;
			const char* map{};
			std::array<char, sizeof("mp_kstenod")> name{};
			return read_map_bytes(reinterpret_cast<void*>(imageBase + 0x5CC7538), &map, sizeof(map)) && map &&
				read_map_bytes(map, name.data(), name.size()) && name.back() == 0 && night_files::MapEnabled(name.data());
		}

		bool is_marked(void* entity)
		{
			if (!entity || registry.Empty()) return false;
			const unsigned id = Read<std::uint16_t>(entity, 0);
			auto* inUse = *reinterpret_cast<const bool**>(imageBase + 0xBC20F08);
			return id < 2048 && inUse && registry.Contains(id, inUse[id]);
		}

		void object_error(void* context, unsigned code, const char* text)
		{
			native<void (*)(unsigned, void*, const char*)>(night_builtin_binding::ObjectError.rva)(code, context, text);
		}

		void script_error(void* context, unsigned code, const char* text)
		{
			native<void (*)(unsigned, void*, const char*)>(night_builtin_binding::ScriptError.rva)(code, context, text);
		}

		void* get_entity(void* context, std::uint64_t reference, unsigned error)
		{
			if ((reference >> 32) & 0xFF)
			{
				object_error(context, error, "not an entity");
				return nullptr;
			}
			if (static_cast<std::uint32_t>(reference) >= 2048)
			{
				object_error(context, error, "invalid entity number");
				return nullptr;
			}
			return native<void* (*)(std::uint64_t)>(night_builtin_binding::GetEntity.rva)(reference);
		}

		void set_flag(void* context, std::uint64_t reference, unsigned mask, unsigned error)
		{
			auto* entity = get_entity(context, reference, 0xAE3);
			if (!entity) return;
			const auto classname = Read<std::uint32_t>(entity, 0x17C);
			bool accepted = false;
			for (auto rva : binding::Classes)
				if (classname == *reinterpret_cast<std::uint32_t*>(imageBase + rva))
				{
					accepted = true;
					break;
				}
			if (!accepted)
			{
				object_error(context, 0xAE2,
					"entity is not a script_brushmodel, script_model, script_origin, script_arms, script_weapon, light or script_vehicle");
				return;
			}
			if (!native<int (*)(int)>(binding::IsMovingPlatform.rva)(Read<std::int16_t>(entity, 0)))
			{
				script_error(context, error, "entity is not a moving platform");
				return;
			}
			const auto value = native<int (*)(void*, unsigned)>(night_builtin_binding::GetInt.rva)(context, 0);
			const auto flags = Read<std::uint32_t>(entity, 0x7C);
			Write(entity, 0x7C, value ? flags | mask : flags & ~mask);
		}

		void linked_impl(void* entity)
		{
			// Source 20CBFA0 differs from 20CBEE0 only in suppressing the immediate
			// physics warp. Replay inlines that operation at 1281D2B in its wrapper.
			// Preserve the actual native link, trace, trajectory reset and SV_Link.
			auto* previous = linkedWithoutWarp;
			if (is_marked(entity) && is_night()) linkedWithoutWarp = entity;
			linked.load(std::memory_order_acquire)(entity);
			linkedWithoutWarp = previous;
		}

		bool physics_impl(void* entity, bool activate)
		{
			const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - imageBase;
			if (is_marked(entity) && is_night() &&
				(caller == 0xFF9373 || caller == 0xFFA8BA || (caller == 0x1281D30 && linkedWithoutWarp == entity)))
				return false;
			return physics.load(std::memory_order_acquire)(entity, activate);
		}

		struct Services
		{
			bool Instances(void* entity, int& main, int& detail)
			{
				const auto* object = native<const void* (*)(void*)>(binding::PhysicsObject.rva)(entity);
				if (!object) return false;
				main = Read<int>(object, 4);
				detail = Read<int>(object, 8);
				return true;
			}

			int Instance(void* entity)
			{
				return native<int (*)(int, int)>(binding::PhysicsInstance.rva)(0, Read<std::int16_t>(entity, 0));
			}

			unsigned Contents(int id)
			{
				return native<unsigned (*)(int, int, int)>(binding::PhysicsContents.rva)(0, id, 0);
			}

			void LinkedTransform(void* entity)
			{
				linked_impl(entity);
			}

			void Link(void* entity)
			{
				native<EntityFn>(binding::LinkEntity.rva)(entity);
			}

			void Deltas(void* entity, const Vec3& inputMove, const Vec3& inputTurn, Vec3& move, Vec3& turn)
			{
				// Original 1C909D0 was inlined in Replay's FF9572..FF9688.
				CalculateDeltas(entity, inputMove, inputTurn, move, turn, *this);
			}

			void Evaluate(void* entity, Vec3& move, Vec3& turn)
			{
				alignas(16) std::array<std::byte, 32> trajectory{};
				native<void* (*)(void*, void*)>(binding::Trajectory.rva)(trajectory.data(), entity);
				native<void (*)(void*, int, float*, float*)>(binding::EvaluateTrajectory.rva)(
					trajectory.data(), *reinterpret_cast<int*>(imageBase + 0xBC21730), move.data(), turn.data());
			}

			void AnglesToQuat(const Vec3& angles, Quat& quat)
			{
				native<void (*)(const float*, float*)>(binding::AnglesToQuat.rva)(angles.data(), quat.data());
			}

			void Keyframe(int id, const Vec3& origin, const Quat& quat)
			{
				native<void (*)(int, int, const float*, const float*, float, bool, bool, float)>(binding::PhysicsKeyframe.rva)(
					0, id, origin.data(), quat.data(), 1.0f, true, false, 0.0f);
			}

			void WarpDetail(int id, const Vec3& origin, const Quat& quat)
			{
				native<bool (*)(int, int, const float*, const float*, bool)>(binding::PhysicsWarp.rva)(
					1, id, origin.data(), quat.data(), false);
			}
		};

		void before_physics()
		{
			const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - imageBase;
			beforePhysics.load(std::memory_order_acquire)();
			if (caller != 0x121F150 || registry.Empty() || !is_night()) return;
			const auto count = *reinterpret_cast<const int*>(imageBase + 0xBC216D8);
			const auto first = *reinterpret_cast<const int*>(imageBase + 0xC4EA868);
			auto* entities = *reinterpret_cast<std::byte**>(imageBase + 0xBC20F00);
			auto* inUse = *reinterpret_cast<const bool**>(imageBase + 0xBC20F08);
			if (!entities || !inUse || first < 0 || first > count || count > 2048)
			{
				LOG_ERR("NightMovers", "invalid native entity bounds %d..%d", first, count);
				return;
			}
			Services services;
			unsigned updated{};
			for (int id = first; id < count; ++id)
			{
				auto* entity = entities + id * 0x5A0;
				if (inUse[id] && is_marked(entity) && UpdateEntity(entity, services)) ++updated;
			}
			if (updated && !reportedFrame)
			{
				reportedFrame = true;
				LOG_INFO("NightMovers", "native server phase updated %u registered movers before physics", updated);
			}
		}

		void destroy_impl(void* entity)
		{
			if (entity) registry.Remove(Read<std::uint16_t>(entity, 0));
			destroy.load(std::memory_order_acquire)(entity);
		}

		void destroy_all()
		{
			registry.Clear();
			reportedFrame = false;
			destroyAll.load(std::memory_order_acquire)();
		}

		bool matches(const night_builtin_binding::Entry& entry)
		{
			std::array<uint8_t, 16> bytes{};
			const auto* at = reinterpret_cast<const void*>(imageBase + entry.rva);
			return safemem::RegionExecutable(at) && safemem::ReadBytes(at, bytes.data(), bytes.size()) && bytes == entry.bytes;
		}
	}

	void ResetServerContext(void* context)
	{
		if (reinterpret_cast<uintptr_t>(context) == imageBase + 0xD591B00)
		{
			registry.Clear();
			reportedFrame = false;
		}
	}

	void MarkKeyframed(void* context, std::uint64_t reference)
	{
		auto* entity = get_entity(context, reference, 0xF67);
		if (!entity) return;
		if (Read<std::uint16_t>(entity, 8) != 6)
		{
			script_error(context, 0x1A07, "markkeyframedmover requires a script mover");
			return;
		}
		if (!is_night())
		{
			script_error(context, 0x1A07, "keyframed mover port requires the registered Night Verdansk map");
			return;
		}
		const auto id = Read<std::uint16_t>(entity, 0);
		auto* inUse = *reinterpret_cast<const bool**>(imageBase + 0xBC20F08);
		// Source's registered keyframing dvar defaults true (1EC6906).
		registry.Mark(id, id < 2048 && inUse && inUse[id]);
		LOG_INFO("NightMovers", "registered keyframed mover entity=%u", id);
	}

	void SetOptimized(void* context, std::uint64_t reference)
	{
		set_flag(context, reference, 0x20, 0x19E2);
	}

	void SetAntilagged(void* context, std::uint64_t reference)
	{
		set_flag(context, reference, 0x40, 0x19E9);
	}

	hook::Status Install(uintptr_t base)
	{
		if (installed) return imageBase == base ? hook::Status::Installed : hook::Status::Failed;
		imageBase = base;
		for (const auto& entry :
			{ binding::PhysicsObject, binding::PhysicsInstance, binding::PhysicsContents, binding::PhysicsUpdate, binding::PhysicsKeyframe,
				binding::PhysicsWarp, binding::AnglesToQuat, binding::LinkEntity, binding::LinkedTransform, binding::Trajectory,
				binding::EvaluateTrajectory, binding::FrameBeforePhysics, binding::DestroyPhysics, binding::DestroyAllPhysics,
				binding::IsMovingPlatform, binding::FrameCall, binding::MoverWarp, binding::LinkedWarp, binding::LinkedMoverWarp })
			if (!matches(entry))
			{
				LOG_ERR("NightMovers", "native binding differs at RVA %llX", static_cast<unsigned long long>(entry.rva));
				return hook::Status::Failed;
			}
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + binding::FrameBeforePhysics.rva), &before_physics,
			binding::FrameBeforePhysics.bytes.data(), 16, beforePhysics);
		batch.Add(
			reinterpret_cast<void*>(base + binding::PhysicsUpdate.rva), &physics_impl, binding::PhysicsUpdate.bytes.data(), 16, physics);
		batch.Add(
			reinterpret_cast<void*>(base + binding::LinkedTransform.rva), &linked_impl, binding::LinkedTransform.bytes.data(), 16, linked);
		batch.Add(
			reinterpret_cast<void*>(base + binding::DestroyPhysics.rva), &destroy_impl, binding::DestroyPhysics.bytes.data(), 16, destroy);
		batch.Add(reinterpret_cast<void*>(base + binding::DestroyAllPhysics.rva), &destroy_all, binding::DestroyAllPhysics.bytes.data(), 16,
			destroyAll);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed)
		{
			installed = true;
			LOG_INFO("NightMovers", "keyframed mover registration, pre-physics update, linked transforms and native destruction installed");
		}
		return status;
	}
}
