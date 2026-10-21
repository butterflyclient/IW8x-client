#include "plugin/sdk/gsc.hpp"

#include <limits>

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		iw8x::api api(host, owner);
		const auto needed = IW8X_CAP_FUNCTIONS | IW8X_CAP_METHODS | IW8X_CAP_NOTIFY | IW8X_CAP_GAME_TASKS;
		if ((host->capabilities() & needed) != needed) return 0;
		api.function("native_add",
			[](iw8x::gsc::context& ctx)
			{
				const int64_t sum = int64_t(ctx.argument<int>(0)) + ctx.argument<int>(1);
				if (sum < INT32_MIN || sum > INT32_MAX) ctx.error("native_add integer overflow");
				ctx.return_value(static_cast<int>(sum));
			});
		api.method("native_test",
			[api](iw8x::gsc::context& ctx, iw8x::gsc::entity self)
			{
				// Deliver on the next server pump so GSC has time to enter waittill.
				// The captured generation is revalidated if the player disconnected.
				api.game_thread(
					[self]
					{
						if (self.valid()) self.notify("native_test_complete", { "hello from C++", 1337 });
					});
				ctx.return_value(true);
			});
		api.command("native_example_status",
			[host, owner](const iw8x_string*, uint32_t)
			{
				host->log(owner, iw8x::gsc::string_view("example plugin is loaded"));
			});
		return 1;
	}
	catch (const std::exception& e)
	{
		if (host && host->version == IW8X_PLUGIN_API_VERSION && host->size >= sizeof(iw8x::plugin_host))
			host->log(owner, iw8x::gsc::string_view(e.what()));
		return 0;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void IW8X_PluginUnload() noexcept
{
	// Host callback admission has closed and active callbacks have drained.
	// Join independently created threads before host-owned userdata is destroyed.
}
