#pragma once
#include "common/utils/thread_pool.hpp"

namespace scheduler
{
	using milliseconds = utils::thread_pool::milliseconds;
	using callback = utils::thread_pool::callback;
	using task_handle = utils::thread_pool::task_handle;

	bool initialize();
	task_handle schedule(callback work, milliseconds delay = milliseconds{ 0 });
	// Native completions are consumed at the existing online-services frame hook.
	bool post_game(std::function<void()> work);
	void pump_game();
	void shutdown();
	utils::thread_pool::statistics status();
}
