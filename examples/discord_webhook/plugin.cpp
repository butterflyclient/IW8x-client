#include "plugin/sdk/gsc.hpp"

#include <Windows.h>

#include <winhttp.h>

#include <stdexcept>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace
{

	void Log(const iw8x::plugin_host* host, iw8x::plugin_id owner, std::string_view message)
	{
		if (host && host->log) host->log(owner, iw8x::gsc::string_view(message));
	}

	std::wstring Utf8ToWide(std::string_view input)
	{
		if (input.empty()) return {};

		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
		if (count <= 0) throw std::runtime_error("webhook URL is not valid UTF-8");

		std::wstring output(static_cast<size_t>(count), L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), count) != count)
			throw std::runtime_error("failed to convert webhook URL");
		return output;
	}

	std::string JsonEscape(std::string_view input)
	{
		static constexpr char hex[] = "0123456789ABCDEF";
		std::string output;
		output.reserve(input.size() + 16);

		for (const unsigned char c : input)
		{
			switch (c)
			{
			case '"':
				output += "\\\"";
				break;
			case '\\':
				output += "\\\\";
				break;
			case '\b':
				output += "\\b";
				break;
			case '\f':
				output += "\\f";
				break;
			case '\n':
				output += "\\n";
				break;
			case '\r':
				output += "\\r";
				break;
			case '\t':
				output += "\\t";
				break;
			default:
				if (c < 0x20)
				{
					output += "\\u00";
					output.push_back(hex[(c >> 4) & 0xF]);
					output.push_back(hex[c & 0xF]);
				}
				else
				{
					output.push_back(static_cast<char>(c));
				}
				break;
			}
		}

		return output;
	}

	class InternetHandle
	{
	public:
		InternetHandle() = default;

		explicit InternetHandle(HINTERNET handle) : handle_(handle)
		{
		}

		~InternetHandle()
		{
			if (handle_) WinHttpCloseHandle(handle_);
		}

		InternetHandle(const InternetHandle&) = delete;
		InternetHandle& operator=(const InternetHandle&) = delete;

		InternetHandle(InternetHandle&& other) noexcept : handle_(other.handle_)
		{
			other.handle_ = nullptr;
		}

		InternetHandle& operator=(InternetHandle&& other) noexcept
		{
			if (this == &other) return *this;
			if (handle_) WinHttpCloseHandle(handle_);
			handle_ = other.handle_;
			other.handle_ = nullptr;
			return *this;
		}

		HINTERNET get() const noexcept
		{
			return handle_;
		}

		explicit operator bool() const noexcept
		{
			return handle_ != nullptr;
		}

	private:
		HINTERNET handle_{};
	};

	struct PostResult
	{
		bool success{};
		DWORD http_status{};
		std::string detail;
	};

	PostResult PostMapToDiscord(std::string_view webhook, std::string_view map)
	{
		const auto wide_url = Utf8ToWide(webhook);

		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		parts.dwSchemeLength = static_cast<DWORD>(-1);
		parts.dwHostNameLength = static_cast<DWORD>(-1);
		parts.dwUrlPathLength = static_cast<DWORD>(-1);
		parts.dwExtraInfoLength = static_cast<DWORD>(-1);

		if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts))
			return { false, 0, "WinHttpCrackUrl failed winerr=" + std::to_string(GetLastError()) };
		if (parts.nScheme != INTERNET_SCHEME_HTTPS) return { false, 0, "webhook URL is not HTTPS" };

		const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
		std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
		if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

		InternetHandle session(WinHttpOpen(
			L"IW8x Discord Webhook Example/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session) return { false, 0, "WinHttpOpen failed winerr=" + std::to_string(GetLastError()) };

		WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000);

		InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
		if (!connection) return { false, 0, "WinHttpConnect failed winerr=" + std::to_string(GetLastError()) };

		InternetHandle request(WinHttpOpenRequest(
			connection.get(), L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
		if (!request) return { false, 0, "WinHttpOpenRequest failed winerr=" + std::to_string(GetLastError()) };

		const std::string body = "{\"content\":\"ui_mapname: `" + JsonEscape(map) + "`\"}";
		constexpr wchar_t headers[] = L"Content-Type: application/json\r\n";

		if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L), const_cast<char*>(body.data()),
				static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0))
			return { false, 0, "WinHttpSendRequest failed winerr=" + std::to_string(GetLastError()) };

		if (!WinHttpReceiveResponse(request.get(), nullptr))
			return { false, 0, "WinHttpReceiveResponse failed winerr=" + std::to_string(GetLastError()) };

		DWORD status{};
		DWORD size = sizeof(status);
		if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				&status, &size, WINHTTP_NO_HEADER_INDEX))
			return { false, 0, "WinHttpQueryHeaders failed winerr=" + std::to_string(GetLastError()) };

		if (status < 200 || status >= 300) return { false, status, "Discord webhook returned HTTP " + std::to_string(status) };

		return { true, status, "request completed" };
	}

	struct AutoState
	{
		AutoState(iw8x::api value, const iw8x::plugin_host* host_value, iw8x::plugin_id owner_value, std::string webhook_value)
			: api(std::move(value)), host(host_value), owner(owner_value), webhook(std::move(webhook_value))
		{
		}

		iw8x::api api;
		const iw8x::plugin_host* host{};
		iw8x::plugin_id owner{};
		std::string webhook;
		std::atomic<bool> scripts_loaded_seen{ false };
	};

	struct PollState
	{
		std::shared_ptr<AutoState> automatic;
		std::uint64_t epoch{};
		unsigned attempts{};
		std::uint32_t scope{ IW8X_SCOPE_MAP };
		bool fallback{};
	};

	void QueueWebhook(const std::shared_ptr<PollState>& state, std::string map)
	{
		const auto& automatic = *state->automatic;
		Log(automatic.host, automatic.owner, "Discord webhook plugin: queueing HTTPS request for ui_mapname='" + map + "'");
		const auto task = automatic.api.worker(
			[webhook = automatic.webhook, map]
			{
				return PostMapToDiscord(webhook, map);
			},
			[host = automatic.host, owner = automatic.owner, map](PostResult result)
			{
				if (result.success)
				{
					Log(host, owner,
						"Discord webhook plugin: HTTPS completion success ui_mapname='" + map +
							"' http=" + std::to_string(result.http_status));
				}
				else
				{
					Log(host, owner,
						"Discord webhook plugin: HTTPS completion failure ui_mapname='" + map +
							"' http=" + std::to_string(result.http_status) + " detail=" + result.detail);
				}
			},
			IW8X_SCOPE_PLUGIN);
		Log(automatic.host, automatic.owner,
			"Discord webhook plugin: HTTPS async task queued id=" + std::to_string(static_cast<unsigned long long>(task)));
	}

	void ScheduleMapPoll(const std::shared_ptr<PollState>& state, std::uint32_t delay_ms)
	{
		const auto task = state->automatic->api.game_thread(
			[state]
			{
				auto& automatic = *state->automatic;
				if (state->fallback && automatic.scripts_loaded_seen.load(std::memory_order_acquire))
				{
					Log(automatic.host, automatic.owner, "Discord webhook plugin: fallback poll canceled because SCRIPTS_LOADED arrived");
					return;
				}

				++state->attempts;
				const auto map = automatic.api.dvar_string("NSQLTTMRMP");
				if (!map.empty())
				{
					Log(automatic.host, automatic.owner,
						"Discord webhook plugin: ui_mapname ready after poll " + std::to_string(state->attempts) + " value='" + map + "'");
					QueueWebhook(state, map);
					return;
				}

				if (state->attempts == 1 || state->attempts % 50 == 0)
				{
					Log(automatic.host, automatic.owner,
						"Discord webhook plugin: ui_mapname still empty poll=" + std::to_string(state->attempts));
				}

				if (state->attempts >= 300)
				{
					Log(automatic.host, automatic.owner,
						"Discord webhook plugin: ui_mapname remained empty for 30 seconds; giving up for this script epoch");
					return;
				}

				ScheduleMapPoll(state, 100);
			},
			delay_ms, state->scope);

		if (state->attempts == 0)
		{
			Log(state->automatic->host, state->automatic->owner,
				"Discord webhook plugin: map poll scheduled id=" + std::to_string(static_cast<unsigned long long>(task)) +
					" epoch=" + std::to_string(static_cast<unsigned long long>(state->epoch)));
		}
	}

} // namespace

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		Log(host, owner, "Discord webhook plugin: IW8X_PluginLoad entered");
		iw8x::api api(host, owner);

		const auto needed = IW8X_CAP_DVARS | IW8X_CAP_GAME_TASKS | IW8X_CAP_ASYNC | IW8X_CAP_COMMANDS;
		const auto capabilities = host->capabilities();
		Log(host, owner, "Discord webhook plugin: host capabilities=" + std::to_string(static_cast<unsigned long long>(capabilities)));
		if ((capabilities & needed) != needed)
		{
			Log(host, owner, "Discord webhook plugin: required DVAR/GAME_TASK/ASYNC/COMMAND capabilities missing");
			return 0;
		}

		const std::string webhook =
			"https://ptb.discord.com/api/webhooks/1554186768876707891/5uId5rhQreCt8x041Dgv4iQXg4JhPEN9unTh__AiRBitOIXwDiF9oKf8accyhuTPtOih";
		if (webhook.empty())
		{
			host->log(owner, iw8x::gsc::string_view("IW8X_DISCORD_WEBHOOK_URL is not set; Discord webhook example not loaded"));
			return 0;
		}

		Log(host, owner, "Discord webhook plugin: webhook configured");

		const auto events = host->supported_events();
		if (!(events & (1ull << IW8X_SCRIPTS_LOADED)))
		{
			Log(host, owner, "Discord webhook plugin: SCRIPTS_LOADED event is unavailable");
			return 0;
		}

		auto automatic = std::make_shared<AutoState>(api, host, owner, webhook);

		// Native plugins can be discovered after the initial script load has
		// already completed. This plugin-scoped fallback starts immediately
		// and is canceled if a real SCRIPTS_LOADED event arrives first.
		auto fallback = std::make_shared<PollState>(PollState{ automatic, 0, 0, IW8X_SCOPE_PLUGIN, true });
		ScheduleMapPoll(fallback, 0);

		api.subscribe(IW8X_SCRIPTS_LOADED,
			[automatic](const iw8x_event& event)
			{
				automatic->scripts_loaded_seen.store(true, std::memory_order_release);
				Log(automatic->host, automatic->owner,
					"Discord webhook plugin: SCRIPTS_LOADED epoch=" + std::to_string(static_cast<unsigned long long>(event.epoch)));
				auto state = std::make_shared<PollState>(PollState{ automatic, event.epoch, 0, IW8X_SCOPE_MAP, false });
				ScheduleMapPoll(state, 0);
			});

		api.command("discord_webhook_test",
			[api, host, owner, webhook](const iw8x_string* args, uint32_t count)
			{
				std::string map = "plugin_test";
				if (count >= 2 && args[1].data && args[1].size) map.assign(args[1].data, args[1].size);

				Log(host, owner, "Discord webhook plugin: test command received map='" + map + "'");
				const auto task = api.worker(
					[webhook, map]
					{
						return PostMapToDiscord(webhook, map);
					},
					[host, owner, map](PostResult result)
					{
						if (result.success)
						{
							Log(host, owner,
								"Discord webhook plugin: test HTTPS success map='" + map + "' http=" + std::to_string(result.http_status));
						}
						else
						{
							Log(host, owner,
								"Discord webhook plugin: test HTTPS failure map='" + map + "' http=" + std::to_string(result.http_status) +
									" detail=" + result.detail);
						}
					},
					IW8X_SCOPE_PLUGIN);
				Log(host, owner,
					"Discord webhook plugin: test async task queued id=" + std::to_string(static_cast<unsigned long long>(task)));
			});

		Log(host, owner, "Discord webhook plugin: registration complete; waiting for SCRIPTS_LOADED (or run discord_webhook_test <map>)");
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
}
