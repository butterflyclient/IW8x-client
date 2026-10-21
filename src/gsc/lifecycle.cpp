#include "api.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/players.hpp"
#include "gsc/internal/state.hpp"

#include "common/commands/command.hpp"
#ifndef IW8X_CLIENT_ONLY
#include "dedicated/runtime/commands.hpp"
#endif
namespace iw8x::gsc
{
	namespace
	{
		std::mutex readiness_mutex;
		uint32_t pending_ready{};
		uint64_t ready_epoch{};

		void queue_ready(uint32_t flag)
		{
			std::lock_guard ready_lock(readiness_mutex);
			auto& s = internal::state();
			std::lock_guard lock(s.mutex);
			ready_epoch = s.lifecycle_epoch;
			pending_ready |= flag;
		}

		void clear_ready()
		{
			std::lock_guard lock(readiness_mutex);
			pending_ready = 0;
		}

		void deliver_ready()
		{
			uint32_t events{};
			{
				std::lock_guard ready_lock(readiness_mutex);
				if (!pending_ready) return;
				auto& s = internal::state();
				std::lock_guard lock(s.mutex);
				if (ready_epoch == s.lifecycle_epoch) events = pending_ready;
				pending_ready = 0;
			}
			if (events & 1) emit(IW8X_MAP_READY);
			if (events & 2) emit(IW8X_SERVER_READY);
		}

		template <class F>
		void boundary(F action, bool allow_vm = false) noexcept
		{
			try
			{
				internal::game_scope scope(allow_vm);
				action();
			}
			catch (const std::exception& e)
			{
				internal::report(nullptr, e.what());
			}
			catch (...)
			{
				internal::report(nullptr, "GSC lifecycle callback failed");
			}
		}
	}

	void scripts_begin_load() noexcept
	{
		boundary(
			[]
			{
				internal::end_map();
				internal::invalidate_script_exports();
				if (internal::bindings().invalidate_entities) internal::bindings().invalidate_entities();
				internal::invalidate_player_entities();
				auto& s = internal::state();
				{
					std::lock_guard lock(s.mutex);
					s.map_active = true;
				}
				emit(IW8X_SCRIPTS_BEGIN_LOAD);
			});
	}

	void scripts_loaded() noexcept
	{
		boundary(
			[]
			{
				emit(IW8X_SCRIPTS_LOADED);
			});
	}

	void scripts_shutdown() noexcept
	{
		boundary(
			[]
			{
				emit(IW8X_SCRIPTS_SHUTDOWN);
				internal::end_map();
				if (internal::bindings().invalidate_entities) internal::bindings().invalidate_entities();
				internal::invalidate_player_entities();
			});
	}

	void map_starting() noexcept
	{
		clear_ready();
		boundary(
			[]
			{
				emit(IW8X_MAP_STARTING);
			});
	}

	// The authoritative startup transition can run on the client/main thread.
	// Only signal here; delivery happens at the verified server RunFrame tail.
	void map_ready() noexcept
	{
		try
		{
			queue_ready(1);
		}
		catch (...)
		{
		}
	}

	void map_shutdown() noexcept
	{
		clear_ready();
		internal::invalidate_script_exports();
		boundary(
			[]
			{
				emit(IW8X_MAP_SHUTDOWN);
			});
		internal::invalidate_player_entities();
	}

	void map_restart() noexcept
	{
		boundary(
			[]
			{
				emit(IW8X_MAP_RESTART);
			});
	}

	void server_starting() noexcept
	{
		clear_ready();
		{
			auto& s = internal::state();
			std::lock_guard lock(s.mutex);
			++s.lifecycle_epoch;
		}
		boundary(
			[]
			{
				emit(IW8X_SERVER_STARTING);
			});
	}

	void server_ready() noexcept
	{
		try
		{
			queue_ready(2);
		}
		catch (...)
		{
		}
	}

	void server_stopping() noexcept
	{
		clear_ready();
		boundary(
			[]
			{
				emit(IW8X_SERVER_STOPPING);
			});
	}

	void server_stopped() noexcept
	{
		clear_ready();
		internal::invalidate_script_exports();
		internal::invalidate_players();
		boundary(
			[]
			{
				emit(IW8X_SERVER_STOPPED);
			});
	}

	void pump_game() noexcept
	{
		thread_local bool pumping{};
		if (pumping) return;
		pumping = true;

		struct reset
		{
			bool& value;

			~reset()
			{
				value = false;
			}
		} guard{ pumping };

		internal::slow_scope timer("frame.plugins", {}, 0, 0, 10000);
		boundary(
			[]
			{
				deliver_ready();
#ifndef IW8X_CLIENT_ONLY
				dedicated::detail::PumpMapRestartVm();
#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
					dedicated::detail::PumpIslandOriginalPrematchSpawnVm();
#endif
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
					dedicated::detail::PumpIslandNativeCrateProbeVm();
					dedicated::detail::PumpIslandNativeCacheAuditVm();
#endif
#endif
				command::pump();
				internal::pump_tasks(false);
			},
			true);
	}

	void pump_worker() noexcept
	{
		try
		{
			internal::pump_tasks(true);
		}
		catch (const std::exception& e)
		{
			internal::report(nullptr, e.what());
		}
		catch (...)
		{
			internal::report(nullptr, "worker pump failed");
		}
		internal::drain_logs();
	}
}
