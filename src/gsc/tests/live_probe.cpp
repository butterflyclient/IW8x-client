// Opt-in dedicated-server acceptance probe. Never part of a normal build/load.
#include "plugin/sdk/gsc.hpp"

#include <cmath>
#include <string>
namespace
{
	void log(const iw8x::api& api, const std::string& message)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view("GSC_PROBE " + message));
	}

	void require(bool ok)
	{
		if (!ok) throw std::runtime_error("unexpected result");
	}

	template <class F>
	void test(const iw8x::api& api, const char* name, F fn)
	{
		log(api, std::string("BEGIN ") + name);
		try
		{
			fn();
			log(api, std::string("PASS ") + name);
		}
		catch (const std::exception& e)
		{
			log(api, std::string("FAIL ") + name + " " + e.what());
		}
	}

	void run(iw8x::api api)
	{
		test(api, "zero arguments",
			[&]
			{
				require(api.gsc().call("gettime").as<int>() >= 0);
			});
		test(api, "float argument and result",
			[&]
			{
				require(std::abs(api.gsc().call("sqrt", 9.0f).as<float>() - 3.0f) < 0.001f);
			});
		test(api, "temporary string",
			[&]
			{
				require(api.gsc().call("tolower", std::string("TeMp")).as<std::string>() == "temp");
			});
		test(api, "vector",
			[&]
			{
				require(std::abs(api.gsc().call("vectornormalize", iw8x::vec3{ 3, 0, 0 }).as<iw8x::vec3>().x - 1) < 0.001f);
			});
		test(api, "engine-created array result",
			[&]
			{
				const auto tokens = api.gsc().call("strtok", "a,b,c", ",").as_array();
				log(api, "strtok count=" + std::to_string(tokens.size()));
				for (size_t i = 0; i < tokens.size(); ++i)
					log(api, "strtok[" + std::to_string(i) + "]=" + std::string(tokens[i].as<std::string_view>()));
				require(tokens.size() == 3 && tokens[0].as<std::string_view>() == "a" && tokens[1].as<std::string_view>() == "b" &&
					tokens[2].as<std::string_view>() == "c");
			});
		test(api, "nested array argument and array result",
			[&]
			{
				auto keys = api.gsc().call("getarraykeys", iw8x::array{ 12, "owned", iw8x::array{ true, iw8x::vec3{ 1, 2, 3 } } });
				const auto& array = keys.as_array();
				log(api, "array key count=" + std::to_string(array.size()));
				int mask = 0;
				for (size_t i = 0; i < array.size(); ++i)
				{
					const auto key = array[i].as<int>();
					log(api, "array key[" + std::to_string(i) + "]=" + std::to_string(key));
					if (key >= 0 && key < 31) mask |= 1 << key;
				}
				require(array.size() == 3 && mask == 7);
			});
		test(api, "struct argument",
			[&]
			{
				require(api.gsc()
						.call("isstruct",
							iw8x::struct_value{
								{ iw8x::canonical_field(0x2c5), iw8x::vec3{ 1, 2, 3 } }, { iw8x::canonical_field(0xd4), "probe" } })
						.as<bool>());
			});
		test(api, "struct result",
			[&]
			{
				require(api.gsc().call("spawnstruct").as_struct().empty());
			});
		test(api, "entity array",
			[&]
			{
				auto v = api.gsc().call("getentarray", "player", "classname");
				log(api, "player entity count=" + std::to_string(v.as_array().size()));
			});
		test(api, "typed string and bool dvar",
			[&]
			{
				log(api,
					"chat=" + api.dvars().get_string("cl_textChatEnabled") +
						" bool=" + std::to_string(api.dvars().get_bool("cl_textChatEnabled")));
			});
		// Resolve through the Replay catalog; the native typed accessor still
		// determines whether the current server exposes the requested dvar/type.
		test(api, "typed integer dvar",
			[&]
			{
				log(api, "ui_maxclients=" + std::to_string(api.dvars().get_int("ui_maxclients")));
			});
		test(api, "typed float dvar",
			[&]
			{
				log(api, "timescale=" + std::to_string(api.dvars().get_float("timescale")));
			});
		test(api, "global dvar",
			[&]
			{
				log(api, "gametype=" + api.gsc().call("getdvar", "g_gametype").as<std::string>());
			});
		test(api, "entity method and stale generation",
			[&]
			{
				const auto entity = api.gsc().call("spawn", "script_origin", iw8x::vec3{ 0, 0, 0 }).as<iw8x_entity>();
				// MP setorigin is PlayerCmdMP_SetOrigin and intentionally rejects a
				// script_origin receiver. getorigin is the side-effect-free generic
				// entity method used to prove method dispatch on this spawned entity.
				const auto origin = api.gsc().method(entity, "getorigin").as<iw8x::vec3>();
				require(std::abs(origin.x) < 0.001f && std::abs(origin.y) < 0.001f && std::abs(origin.z) < 0.001f);
				api.gsc().method(entity, "delete");
				try
				{
					api.gsc().method(entity, "getorigin");
				}
				catch (const iw8x::api_error& e)
				{
					require(e.code() == IW8X_STALE);
					return;
				}
				throw std::runtime_error("deleted entity remained valid");
			});
		test(api, "script error cleanup",
			[&]
			{
				try
				{
					api.gsc().call("sqrt");
				}
				catch (const iw8x::api_error& e)
				{
					require(e.code() == IW8X_CALLBACK_ERROR);
					require(api.gsc().call("sqrt", 16.0f).as<float>() == 4.0f);
					return;
				}
				throw std::runtime_error("missing argument did not report script error");
			});
		for (int i = 0; i < 100; ++i)
			api.gsc().call("getarraykeys", iw8x::array{ "repeat", iw8x::array{ 1, 2, 3 } });
		log(api, "DONE 100 repeated collection calls");
	}
}

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner)
{
	try
	{
		iw8x::api api(host, owner);
		log(api, "loaded capabilities=" + std::to_string(host->capabilities()));
		api.subscribe(IW8X_SERVER_STARTED,
			[api](const iw8x_event&)
			{
				api.game_thread(
					[api]
					{
						run(api);
					},
					1000);
			});
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}
