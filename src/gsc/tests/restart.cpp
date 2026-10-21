#include "plugin/sdk/gsc.hpp"

#include <algorithm>
#include <iostream>
extern "C" uint32_t IW8X_PluginLoad(const iw8x_plugin_host*, iw8x_plugin_id) noexcept;
extern "C" void IW8X_PluginUnload() noexcept;

namespace
{
	unsigned checks{};

	void expect(bool pass, const char* message)
	{
		++checks;
		if (!pass) throw std::runtime_error(message);
	}

	std::vector<std::string> actions, errors;
	iw8x_native_callback native{};
	void* native_data{};
	iw8x_destroy_callback native_destroy{};
	bool accepted{};
	uint64_t now{}, next_id{ 1 };
	bool stale_player{};

	struct task
	{
		uint64_t id, due;
		iw8x_task_callback callback;
		void* data;
		iw8x_destroy_callback destroy;
	};

	std::vector<task> tasks;

	std::string text(iw8x_string value)
	{
		return std::string(value.data, value.size);
	}

	iw8x_status register_native(
		iw8x_plugin_id, iw8x_string name, uint32_t method, iw8x_native_callback callback, void* data, iw8x_destroy_callback destroy)
	{
		expect(text(name) == "iw8x_restart_map" && !method, "existing global restart hook remains available");
		native = callback;
		native_data = data;
		native_destroy = destroy;
		return IW8X_OK;
	}

	iw8x_status return_value(uint64_t, const iw8x_value* value)
	{
		accepted = iw8x::value::copy(*value).as<bool>();
		return IW8X_OK;
	}

	void log(iw8x_plugin_id, iw8x_string message)
	{
		errors.push_back(text(message));
	}

	iw8x_status error(uint64_t, iw8x_string message)
	{
		errors.push_back(text(message));
		return IW8X_OK;
	}

	iw8x_status players(iw8x_players_callback receive, void* data)
	{
		const iw8x_player refs[]{ { 5, 0, 1, 0 }, { 6, 0, 1, 0 } };
		receive(data, refs, 2);
		return IW8X_OK;
	}

	iw8x_status player_entity(iw8x_player player, iw8x_entity* entity)
	{
		if (stale_player && player.client_num == 5) return IW8X_STALE;
		*entity = { player.client_num, 0, 1 };
		return IW8X_OK;
	}

	iw8x_status schedule(iw8x_plugin_id, uint32_t delay, uint32_t scope, iw8x_task_callback callback, void* data,
		iw8x_destroy_callback destroy, iw8x_registration* out)
	{
		expect(scope == IW8X_SCOPE_MAP, "restart tasks are canceled at map teardown");
		*out = next_id++;
		tasks.push_back({ *out, now + delay, callback, data, destroy });
		return IW8X_OK;
	}

	iw8x_status builtin(iw8x_plugin_id, uint32_t method, iw8x_entity entity, iw8x_string raw, const iw8x_value* args, uint32_t count,
		iw8x_value_callback receive, void* data)
	{
		const auto name = text(raw);
		if (name == "map_restart")
		{
			expect(!method && count == 1 && iw8x::value::copy(args[0]).as<int>() == 1, "restart passes the stock integer argument 1");
			actions.push_back(name);
		}
		else
		{
			expect(method && entity.generation == 1, "presentation uses a player entity receiver");
			if (name == "iprintlnbold")
				expect(count == 1 && iw8x::value::copy(args[0]).as<std::string>() == "^1Restarting Map...", "restart announcement");
			else if (name == "freezecontrols")
				expect(count == 1 && iw8x::value::copy(args[0]).as<int>() == 1, "controls freeze before wait");
			else
			{
				expect(name == "setclientomnvar" && count == 2 && iw8x::value::copy(args[0]).as<std::string>() == "ui_total_fade" &&
						iw8x::value::copy(args[1]).as<int>() == 1,
					"screen fade before wait");
			}
			actions.push_back(std::to_string(entity.index) + ":" + name);
		}
		const iw8x_value undefined{};
		receive(data, &undefined);
		return IW8X_OK;
	}

	iw8x_status notify(iw8x_entity entity, iw8x_string name, const iw8x_value*, uint32_t count)
	{
		expect(!entity.generation && !entity.index && !count && text(name) == "restarting", "notify targets level");
		actions.push_back("level:restarting");
		return IW8X_OK;
	}

	void pump()
	{
		const auto cutoff = next_id;
		for (;;)
		{
			const auto it = std::find_if(tasks.begin(), tasks.end(),
				[&](const task& t)
				{
					return t.due <= now && t.id < cutoff;
				});
			if (it == tasks.end()) return;
			const auto item = *it;
			tasks.erase(it);
			item.callback(item.data);
			item.destroy(item.data);
		}
	}

	void cancel_map()
	{
		for (const auto& item : tasks)
			item.destroy(item.data);
		tasks.clear();
	}
}

int main()
{
	try
	{
		iw8x_plugin_host host{};
		host.version = IW8X_PLUGIN_API_VERSION;
		host.size = sizeof(host);
		host.register_native = &register_native;
		host.return_value = &return_value;
		host.log = &log;
		host.callback_error = &log;
		host.error = &error;
		host.players = &players;
		host.player_entity = &player_entity;
		host.schedule = &schedule;
		host.call_builtin = &builtin;
		host.notify = &notify;
		expect(IW8X_PluginLoad(&host, 1) == 1, "actual restart plugin loads");
		native(native_data, 1, {});
		expect(accepted && actions.empty(), "native callback queues engine work");
		pump();
		const std::vector<std::string> presentation{ "5:iprintlnbold", "5:freezecontrols", "5:setclientomnvar", "6:iprintlnbold",
			"6:freezecontrols", "6:setclientomnvar" };
		expect(actions == presentation && tasks.size() == 1 && tasks[0].due == 500, "show/freeze/fade then wait 500ms");
		now = 499;
		pump();
		expect(actions == presentation, "restart never fires before the wait completes");
		now = 500;
		pump();
		auto completed = presentation;
		completed.insert(completed.end(), { "level:restarting", "map_restart" });
		expect(actions == completed && tasks.empty() && errors.empty(), "notify precedes restart exactly once");
		pump();
		expect(actions == completed, "restart task retires after execution");
		actions.clear();
		stale_player = true;
		native(native_data, 2, {});
		pump();
		expect(actions == std::vector<std::string>{ "6:iprintlnbold", "6:freezecontrols", "6:setclientomnvar" },
			"disconnecting player does not prevent other players preparing or restart scheduling");
		cancel_map();
		now += 500;
		pump();
		expect(actions.size() == 3, "map teardown cancels delayed restart");
		IW8X_PluginUnload();
		native_destroy(native_data);
		std::cout << "Restart plugin: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAILED: " << e.what() << '\n';
		return 1;
	}
}
