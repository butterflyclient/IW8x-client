#include "gsc/internal/bindings.hpp"

#include <atomic>
#include <stdexcept>
namespace iw8x::gsc::internal
{
	namespace
	{
		const engine_bindings empty;
		std::atomic<const engine_bindings*> engine{ &empty };
		thread_local unsigned game_depth{};
		thread_local bool game_allowed{};
		// Recursive entries on the same thread are allowed. Competing engine threads
		// fail closed rather than guessing which one owns ScriptContext.
		std::atomic_flag game_busy = ATOMIC_FLAG_INIT;
	}

	const engine_bindings& bindings()
	{
		return *engine.load(std::memory_order_acquire);
	}

	void configure_bindings(engine_bindings value)
	{
		// Immutable snapshots have host/process lifetime: lifecycle readers may
		// overlap initial hook installation and must never see partially set fields.
		engine.store(new engine_bindings(value), std::memory_order_release);
	}

	bool in_game_scope() noexcept
	{
		return game_depth != 0 && game_allowed;
	}

	game_scope::game_scope(bool allow_vm)
	{
		if (!game_depth && allow_vm)
		{
			if (game_busy.test_and_set(std::memory_order_acquire)) throw std::runtime_error("concurrent GSC engine boundary");
			owns_vm_ = true;
		}
		previous_allowed_ = game_allowed;
		game_allowed = allow_vm && (!game_depth || game_allowed);
		++game_depth;
		entered_ = true;
	}

	game_scope::~game_scope()
	{
		if (entered_)
		{
			game_allowed = previous_allowed_;
			--game_depth;
			if (owns_vm_) game_busy.clear(std::memory_order_release);
		}
	}
}
