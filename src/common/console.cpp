#include "common/console.h"

#include "engine/commands.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>

#include <share.h>

#include <string>
#include <string_view>
#include <vector>

#include <ws2tcpip.h>
namespace
{
	std::mutex g_lock;
	HANDLE g_logFile = INVALID_HANDLE_VALUE;
	bool g_initialized = false;
	std::atomic<uintptr_t> g_commandBase{ 0 };
	std::atomic<bool> g_commandInputStarted{ false };
	std::atomic<int> g_logLevel{ static_cast<int>(console::LogLevel::Normal) };
	std::atomic<bool> g_dedicated{ false };
	std::atomic<console::CommandHandler> g_commandHandler{ nullptr };
	std::atomic<console::OutputHandler> g_outputHandler{ nullptr };

	HANDLE g_consoleOutput = INVALID_HANDLE_VALUE;
	WORD g_defaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	bool g_promptActive = false;
	std::wstring g_inputLine;
	size_t g_inputCursor = 0;
	std::vector<std::wstring> g_history;
	size_t g_historyIndex = 0;

	constexpr wchar_t kPrompt[] = L"] ";
	constexpr WORD kPromptColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	constexpr WORD kInfoColor = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
	constexpr WORD kWarnColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	constexpr WORD kErrorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;

	void OpenLogFile(bool dedicated)
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&OpenLogFile), &module))
			return;

		wchar_t path[MAX_PATH]{};
		if (!GetModuleFileNameW(module, path, MAX_PATH)) return;

		wchar_t* slash = wcsrchr(path, L'\\');
		if (!slash) return;

		const wchar_t* fileName = dedicated ? L"mw120rproxy.dedi.log" : L"mw120rproxy.log";
		const size_t remaining = MAX_PATH - static_cast<size_t>((slash + 1) - path);
		if (wcscpy_s(slash + 1, remaining, fileName) != 0) return;

		g_logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (g_logFile == INVALID_HANDLE_VALUE) OutputDebugStringA("Replay could not open its diagnostic log\n");
	}

	// Keep durable diagnostic output separate from the game/console CRT streams.
	// The live dedicated runtime produced console output with an empty CRT log.
	void WriteLog(const char* text)
	{
		if (g_logFile == INVALID_HANDLE_VALUE || !text) return;
		size_t remaining = strlen(text);
		while (remaining)
		{
			DWORD written = 0;
			if (!WriteFile(g_logFile, text, static_cast<DWORD>(remaining), &written, nullptr) || !written)
			{
				OutputDebugStringA("Replay diagnostic log write failed\n");
				return;
			}
			text += written;
			remaining -= written;
		}
	}

	bool ResolveLocalIPv4(char* out, size_t outSize)
	{
		if (!out || outSize < INET_ADDRSTRLEN) return false;
		out[0] = '\0';

		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;

		char host[256]{};
		bool found = false;
		int bestScore = -1;
		if (gethostname(host, sizeof(host)) == 0)
		{
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			hints.ai_protocol = IPPROTO_UDP;
			addrinfo* list = nullptr;
			if (getaddrinfo(host, nullptr, &hints, &list) == 0)
			{
				for (auto* it = list; it; it = it->ai_next)
				{
					if (!it->ai_addr || it->ai_addrlen < sizeof(sockaddr_in)) continue;
					const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
					const uint32_t hostOrder = ntohl(sin->sin_addr.s_addr);
					const uint8_t a = static_cast<uint8_t>(hostOrder >> 24);
					const uint8_t b = static_cast<uint8_t>(hostOrder >> 16);
					if (a == 127 || hostOrder == 0) continue;

					int score = 1;
					if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168))
						score = 3;
					else if (a == 169 && b == 254)
						score = 0;
					if (score <= bestScore) continue;

					char candidate[INET_ADDRSTRLEN]{};
					if (!inet_ntop(AF_INET, &sin->sin_addr, candidate, sizeof(candidate))) continue;
					strcpy_s(out, outSize, candidate);
					bestScore = score;
					found = true;
				}
				freeaddrinfo(list);
			}
		}

		WSACleanup();
		return found;
	}

	bool ConsoleOutputReady()
	{
		return g_consoleOutput && g_consoleOutput != INVALID_HANDLE_VALUE;
	}

	void SetColor(WORD attributes)
	{
		if (ConsoleOutputReady()) SetConsoleTextAttribute(g_consoleOutput, attributes);
	}

	void WriteConsoleBytes(const char* text, size_t length)
	{
		if (!text || !length) return;
		if (ConsoleOutputReady())
		{
			DWORD written = 0;
			WriteConsoleA(g_consoleOutput, text, static_cast<DWORD>(length), &written, nullptr);
		}
		else
		{
			fwrite(text, 1, length, stdout);
			fflush(stdout);
		}
	}

	void WriteConsoleWide(const wchar_t* text, size_t length)
	{
		if (!text || !length || !ConsoleOutputReady()) return;
		DWORD written = 0;
		WriteConsoleW(g_consoleOutput, text, static_cast<DWORD>(length), &written, nullptr);
	}

	void ClearCurrentConsoleLineLocked()
	{
		if (!ConsoleOutputReady()) return;
		CONSOLE_SCREEN_BUFFER_INFO info{};
		if (!GetConsoleScreenBufferInfo(g_consoleOutput, &info)) return;
		const COORD lineStart{ 0, info.dwCursorPosition.Y };
		DWORD written = 0;
		FillConsoleOutputCharacterW(g_consoleOutput, L' ', info.dwSize.X, lineStart, &written);
		FillConsoleOutputAttribute(g_consoleOutput, g_defaultAttributes, info.dwSize.X, lineStart, &written);
		SetConsoleCursorPosition(g_consoleOutput, lineStart);
	}

	void RenderPromptLocked()
	{
		if (!g_promptActive || !ConsoleOutputReady()) return;

		CONSOLE_SCREEN_BUFFER_INFO info{};
		if (!GetConsoleScreenBufferInfo(g_consoleOutput, &info) || info.dwSize.X <= 4) return;

		ClearCurrentConsoleLineLocked();
		const size_t available = static_cast<size_t>(info.dwSize.X - 3);
		size_t start = 0;
		if (g_inputLine.size() > available)
		{
			if (g_inputCursor >= available) start = g_inputCursor - available + 1;
			if (start + available > g_inputLine.size()) start = g_inputLine.size() - available;
		}

		SetColor(kPromptColor);
		WriteConsoleWide(kPrompt, 2);
		SetColor(g_defaultAttributes);

		const size_t visible = std::min(available, g_inputLine.size() - start);
		if (visible) WriteConsoleWide(g_inputLine.data() + start, visible);

		const SHORT cursorX = static_cast<SHORT>(2 + std::min(g_inputCursor - start, available));
		CONSOLE_SCREEN_BUFFER_INFO after{};
		if (GetConsoleScreenBufferInfo(g_consoleOutput, &after))
			SetConsoleCursorPosition(g_consoleOutput, COORD{ cursorX, after.dwCursorPosition.Y });
	}

	void CommitPromptLocked()
	{
		if (!g_promptActive) return;
		RenderPromptLocked();
		SetColor(g_defaultAttributes);
		WriteConsoleBytes("\r\n", 2);
	}

	void WriteColoredLogLocked(const char* text, const char* line)
	{
		if (!ConsoleOutputReady())
		{
			fputs(line, stdout);
			fflush(stdout);
			return;
		}

		const char* rest = text;
		WORD severity = g_defaultAttributes;
		size_t prefixLength = 0;
		if (strncmp(text, "[INFO]", 6) == 0)
		{
			severity = kInfoColor;
			prefixLength = 6;
		}
		else if (strncmp(text, "[WARN]", 6) == 0)
		{
			severity = kWarnColor;
			prefixLength = 6;
		}
		else if (strncmp(text, "[ERR ]", 6) == 0)
		{
			severity = kErrorColor;
			prefixLength = 6;
		}

		if (prefixLength)
		{
			SetColor(severity);
			WriteConsoleBytes(text, prefixLength);
			SetColor(g_defaultAttributes);
			rest += prefixLength;
			WriteConsoleBytes(rest, strlen(rest));
			WriteConsoleBytes("\r\n", 2);
		}
		else
		{
			SetColor(g_defaultAttributes);
			WriteConsoleBytes(line, strlen(line));
		}
	}

	void ReplaceInputLocked(const std::wstring& value)
	{
		g_inputLine = value;
		if (g_inputLine.size() > 1023) g_inputLine.resize(1023);
		g_inputCursor = g_inputLine.size();
	}

	std::string_view Trim(std::string_view value)
	{
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
			value.remove_prefix(1);
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
			value.remove_suffix(1);
		return value;
	}

	bool EqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			char ca = a[i], cb = b[i];
			if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
			if (ca != cb) return false;
		}
		return true;
	}

	bool HasCategory(const char* text, const char* category)
	{
		if (!text || !category) return false;
		char token[64]{};
		_snprintf_s(token, sizeof(token), _TRUNCATE, "][%s]", category);
		return strstr(text, token) != nullptr;
	}

	bool IsRawDiagnosticCategory(const char* text)
	{
		return HasCategory(text, "EngineNet") || HasCategory(text, "LANRX") || HasCategory(text, "LAN");
	}

	bool ShouldShowDiagnostic(const char* text)
	{
		if (!g_dedicated.load(std::memory_order_relaxed) && text &&
			(!strncmp(text, "[ERR ]", 6) || !strncmp(text, "[WARN]", 6))) return true;
		const auto level = static_cast<console::LogLevel>(g_logLevel.load(std::memory_order_acquire));
		if (level == console::LogLevel::Debug) return true;
		if (IsRawDiagnosticCategory(text)) return false;
		if (level == console::LogLevel::Verbose) return true;
		// Normal windows show useful load/matchmaking/server events; the full
		// engine/network trace remains in this process's own log file.
		if (HasCategory(text, "Fastfile") || HasCategory(text, "Server") || HasCategory(text, "OnlineServices") ||
			HasCategory(text, "Playlist"))
			return true;
		// Join diagnostics contain snapshots, stack frames and RVAs. Normal mode
		// gets a short operator event from join_diagnostics.cpp instead.
		if (HasCategory(text, "Join")) return false;
		// Normal mode is operator-first. Existing detailed messages still go to the
		// logfile, while fatal/actionable errors remain visible automatically.
		return text && strncmp(text, "[ERR ]", 6) == 0;
	}

	void EmitLine(const char* text, bool forceConsole)
	{
		if (!text) return;
		char line[2112]{};
		_snprintf_s(line, sizeof(line), _TRUNCATE, "%s\n", text);

		std::lock_guard<std::mutex> guard(g_lock);
		if (g_dedicated.load(std::memory_order_relaxed) || console::GetLogLevel() == console::LogLevel::Debug)
			OutputDebugStringA(line);
		WriteLog(line);

		if (!forceConsole && !ShouldShowDiagnostic(text)) return;
		if (g_promptActive) ClearCurrentConsoleLineLocked();
		WriteColoredLogLocked(text, line);
		SetColor(g_defaultAttributes);
		if (g_promptActive) RenderPromptLocked();
	}

	void EmitFormatted(bool forceConsole, const char* prefix, const char* format, va_list args)
	{
		// Client normal mode drops routine traces before formatting, locking or I/O.
		// Keep warnings/errors, operator output and dedicated diagnostics available.
		if (!forceConsole && !console::TraceEnabled() && format && !strncmp(format, "[INFO][", 7))
		{
			constexpr const char* routine_categories[] = { "EngineNet", "LUI", "LAN", "LANRX", "Fastfile", "Fence", "OnlineUI",
				"PublisherVars", "MOTD", "StoreBackend", "FrontendBackend", "Stats", "Progression", "ServerProgression",
				"Chat", "Social", "Voice", "Presence", "UserLists", "Maps" };
			for (const auto* category : routine_categories)
			{
				const auto length = strlen(category);
				if (!strncmp(format + 7, category, length) && format[7 + length] == ']') return;
			}
		}
		char body[2048]{};
		_vsnprintf_s(body, sizeof(body), _TRUNCATE, format ? format : "", args);
		if (!prefix)
		{
			if (const auto output = g_outputHandler.load(std::memory_order_acquire)) output(body);
			EmitLine(body, forceConsole);
			return;
		}
		char text[2112]{};
		_snprintf_s(text, sizeof(text), _TRUNCATE, "%s%s", prefix, body);
		if (const auto output = g_outputHandler.load(std::memory_order_acquire)) output(text);
		EmitLine(text, forceConsole);
	}

	const char* LevelName(console::LogLevel level)
	{
		switch (level)
		{
		case console::LogLevel::Verbose:
			return "verbose";
		case console::LogLevel::Debug:
			return "debug";
		default:
			return "normal";
		}
	}

	bool HandleConsoleBuiltin(std::string_view command)
	{
		command = Trim(command);
		if (command.size() < 8 || !EqualsIgnoreCase(command.substr(0, 8), "loglevel")) return false;
		if (command.size() > 8 && command[8] != ' ' && command[8] != '\t') return false;

		auto value = command.size() > 8 ? Trim(command.substr(8)) : std::string_view{};
		if (value.empty())
		{
			console::OperatorInfo("Log level: %s", LevelName(console::GetLogLevel()));
			return true;
		}

		console::LogLevel level{};
		if (EqualsIgnoreCase(value, "normal"))
			level = console::LogLevel::Normal;
		else if (EqualsIgnoreCase(value, "verbose"))
			level = console::LogLevel::Verbose;
		else if (EqualsIgnoreCase(value, "debug"))
			level = console::LogLevel::Debug;
		else
		{
			console::OperatorWarn("Usage: loglevel normal|verbose|debug");
			return true;
		}
		console::SetLogLevel(level);
		console::OperatorInfo("Log level: %s", LevelName(level));
		return true;
	}

	DWORD WINAPI CommandInputThread(LPVOID)
	{
		HANDLE input =
			CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (input == INVALID_HANDLE_VALUE)
		{
			console::Print("[ERR ][Console] could not open CONIN$ for dedicated command input (%lu)", GetLastError());
			g_commandInputStarted.store(false, std::memory_order_release);
			return 0;
		}

		DWORD mode = 0;
		if (GetConsoleMode(input, &mode))
		{
			mode |= ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS;
			mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE);
			SetConsoleMode(input, mode);
		}

		console::OperatorInfo("Console ready | type status or loglevel normal|verbose|debug");
		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_promptActive = true;
			g_inputLine.clear();
			g_inputCursor = 0;
			g_historyIndex = g_history.size();
			RenderPromptLocked();
		}

		for (;;)
		{
			INPUT_RECORD record{};
			DWORD read = 0;
			if (!ReadConsoleInputW(input, &record, 1, &read))
			{
				const DWORD error = GetLastError();
				if (error == ERROR_OPERATION_ABORTED || error == ERROR_INVALID_HANDLE) break;
				console::Print("[WARN][Console] ReadConsoleInputW failed (%lu)", error);
				Sleep(100);
				continue;
			}
			if (!read || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;

			const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
			std::string queued;
			std::string entered;
			bool submit = false;
			{
				std::lock_guard<std::mutex> guard(g_lock);
				switch (key.wVirtualKeyCode)
				{
				case VK_RETURN:
					if (!g_inputLine.empty())
					{
						CommitPromptLocked();
						char command[4096]{};
						const int bytes = WideCharToMultiByte(CP_UTF8, 0, g_inputLine.data(), static_cast<int>(g_inputLine.size()), command,
							static_cast<int>(sizeof(command) - 2), nullptr, nullptr);
						if (bytes > 0)
						{
							entered.assign(command, static_cast<size_t>(bytes));
							command[bytes] = '\n';
							command[bytes + 1] = '\0';
							queued.assign(command, static_cast<size_t>(bytes + 1));
							if (g_history.empty() || g_history.back() != g_inputLine) g_history.push_back(g_inputLine);
							if (g_history.size() > 64) g_history.erase(g_history.begin());
							submit = true;
						}
						g_inputLine.clear();
						g_inputCursor = 0;
						g_historyIndex = g_history.size();
					}
					RenderPromptLocked();
					break;
				case VK_BACK:
					if (g_inputCursor)
					{
						g_inputLine.erase(g_inputCursor - 1, 1);
						--g_inputCursor;
						RenderPromptLocked();
					}
					break;
				case VK_DELETE:
					if (g_inputCursor < g_inputLine.size())
					{
						g_inputLine.erase(g_inputCursor, 1);
						RenderPromptLocked();
					}
					break;
				case VK_LEFT:
					if (g_inputCursor)
					{
						--g_inputCursor;
						RenderPromptLocked();
					}
					break;
				case VK_RIGHT:
					if (g_inputCursor < g_inputLine.size())
					{
						++g_inputCursor;
						RenderPromptLocked();
					}
					break;
				case VK_HOME:
					g_inputCursor = 0;
					RenderPromptLocked();
					break;
				case VK_END:
					g_inputCursor = g_inputLine.size();
					RenderPromptLocked();
					break;
				case VK_UP:
					if (!g_history.empty() && g_historyIndex > 0)
					{
						--g_historyIndex;
						ReplaceInputLocked(g_history[g_historyIndex]);
						RenderPromptLocked();
					}
					break;
				case VK_DOWN:
					if (g_historyIndex < g_history.size())
					{
						++g_historyIndex;
						if (g_historyIndex < g_history.size())
							ReplaceInputLocked(g_history[g_historyIndex]);
						else
						{
							g_inputLine.clear();
							g_inputCursor = 0;
						}
						RenderPromptLocked();
					}
					break;
				default:
					if (key.uChar.UnicodeChar >= 32 && key.uChar.UnicodeChar != 127 && g_inputLine.size() < 1023)
					{
						g_inputLine.insert(g_inputCursor, 1, key.uChar.UnicodeChar);
						++g_inputCursor;
						RenderPromptLocked();
					}
					break;
				}
			}

			if (submit)
			{
				const auto command = Trim(std::string_view(entered));
				if (command.empty()) continue;
				if (HandleConsoleBuiltin(command)) continue;
				const auto handler = g_commandHandler.load(std::memory_order_acquire);
				if (handler && handler(command)) continue;
				const uintptr_t base = g_commandBase.load(std::memory_order_acquire);
				if (!base || !commands::Queue(base, queued.c_str())) console::OperatorWarn("Native Cbuf rejected command");
			}
		}

		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_promptActive = false;
		}
		CloseHandle(input);
		g_commandInputStarted.store(false, std::memory_order_release);
		return 0;
	}
}

namespace console
{
	void Init(bool dedicated)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		if (g_initialized) return;
		g_initialized = true;
		g_dedicated.store(dedicated, std::memory_order_relaxed);

		// A batch/terminal launch can inherit the parent's console. Detach first:
		// otherwise a client and dedicated launched there share output and input.
		FreeConsole();
		const bool allocated = AllocConsole() != FALSE;
		const DWORD allocationError = allocated ? ERROR_SUCCESS : GetLastError();

		FILE* stream = nullptr;
		freopen_s(&stream, "CONOUT$", "w", stdout);
		freopen_s(&stream, "CONOUT$", "w", stderr);
		g_consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (ConsoleOutputReady())
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (GetConsoleScreenBufferInfo(g_consoleOutput, &info)) g_defaultAttributes = info.wAttributes;
		}
		wchar_t title[128]{};
		if (dedicated)
			swprintf_s(title, L"IW8 Dedicated server logs | PID %lu", GetCurrentProcessId());
		else
			wcscpy_s(title, L"IW8x");
		SetConsoleTitleW(title);
		if (dedicated)
		{
			// Make the interactive server console available even when a launcher
			// requests a hidden process. Do not take focus away from the client.
			if (HWND window = GetConsoleWindow()) ShowWindowAsync(window, SW_SHOWNOACTIVATE);
		}
		OpenLogFile(dedicated);
		char banner[256]{};
		sprintf_s(banner, "[INFO] %s logs | PID %lu | %s\n", dedicated ? "Dedicated server" : "Client", GetCurrentProcessId(),
			dedicated ? "mw120rproxy.dedi.log" : "mw120rproxy.log");
		WriteLog(banner);
		if (!allocated)
		{
			char error[128]{};
			sprintf_s(error, "[ERR ][Console] separate log window allocation failed (%lu)\n", allocationError);
			WriteLog(error);
		}
		WriteConsoleBytes(banner, strlen(banner));
	}

	void EnableCommandInput(uintptr_t moduleBase)
	{
		if (!moduleBase) return;
		Init();
		g_commandBase.store(moduleBase, std::memory_order_release);
		bool expected = false;
		if (!g_commandInputStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

		HANDLE thread = CreateThread(nullptr, 0, &CommandInputThread, nullptr, 0, nullptr);
		if (!thread)
		{
			g_commandInputStarted.store(false, std::memory_order_release);
			Print("[ERR ][Console] could not create dedicated command-input thread (%lu)", GetLastError());
			return;
		}
		CloseHandle(thread);
	}

	void SetDedicatedTitle(const char* mapName)
	{
		Init();
		char ip[INET_ADDRSTRLEN] = "0.0.0.0";
		if (!ResolveLocalIPv4(ip, sizeof(ip))) strcpy_s(ip, "127.0.0.1");

		const char* map = mapName && *mapName ? mapName : "unknown";
		char titleUtf8[256]{};
		_snprintf_s(
			titleUtf8, sizeof(titleUtf8), _TRUNCATE, "IW8 Dedicated server logs | %s | %s | PID %lu", ip, map, GetCurrentProcessId());

		wchar_t title[256]{};
		const int converted = MultiByteToWideChar(CP_UTF8, 0, titleUtf8, -1, title, static_cast<int>(std::size(title)));
		if (converted > 0)
			SetConsoleTitleW(title);
		else
			SetConsoleTitleW(L"mw120rproxy");

		Print("[INFO][Console] window title: %s", titleUtf8);
	}

	bool GetLocalIPv4(char* out, size_t outSize)
	{
		return ResolveLocalIPv4(out, outSize);
	}

	void SetLogLevel(LogLevel level)
	{
		g_logLevel.store(static_cast<int>(level), std::memory_order_release);
	}

	LogLevel GetLogLevel()
	{
		return static_cast<LogLevel>(g_logLevel.load(std::memory_order_acquire));
	}

	bool TraceEnabled()
	{
		return g_dedicated.load(std::memory_order_relaxed) || GetLogLevel() != LogLevel::Normal;
	}

	bool HandleBuiltinCommand(std::string_view command)
	{
		return HandleConsoleBuiltin(command);
	}

	void SetCommandHandler(CommandHandler handler)
	{
		g_commandHandler.store(handler, std::memory_order_release);
	}

	void SetOutputHandler(OutputHandler handler)
	{
		g_outputHandler.store(handler, std::memory_order_release);
	}

	void Print(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		EmitFormatted(false, nullptr, format, args);
		va_end(args);
	}

	void OperatorInfo(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		EmitFormatted(true, "[INFO] ", format, args);
		va_end(args);
	}

	void OperatorWarn(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		EmitFormatted(true, "[WARN] ", format, args);
		va_end(args);
	}

	void OperatorError(const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		EmitFormatted(true, "[ERR ] ", format, args);
		va_end(args);
	}

}
