#pragma once
#include <windows.h>

#include <cstdint>
#include <string_view>
namespace console
{
	enum class LogLevel : int
	{
		Normal = 0,
		Verbose = 1,
		Debug = 2
	};
	using CommandHandler = bool (*)(std::string_view command);
	using OutputHandler = void (*)(std::string_view text);

	void Init(bool dedicated = false);
	void EnableCommandInput(uintptr_t moduleBase);
	void SetDedicatedTitle(const char* mapName);
	bool GetLocalIPv4(char* out, size_t outSize);

	void SetLogLevel(LogLevel level);
	LogLevel GetLogLevel();
	bool TraceEnabled();
	bool HandleBuiltinCommand(std::string_view command);
	void SetCommandHandler(CommandHandler handler);
	void SetOutputHandler(OutputHandler handler);

	// Client normal mode omits routine traces; verbose restores file diagnostics,
	// debug also enables debugger output. Dedicated logging retains its full trace.
	void Print(const char* format, ...);

	// Operator messages are always shown in the terminal and also written to the log.
	void OperatorInfo(const char* format, ...);
	void OperatorWarn(const char* format, ...);
	void OperatorError(const char* format, ...);
}
#define LOG(fmt, ...) ::console::Print(fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...) ::console::Print("[INFO][" cat "] " fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...) ::console::Print("[WARN][" cat "] " fmt, ##__VA_ARGS__)
#define LOG_ERR(cat, fmt, ...) ::console::Print("[ERR ][" cat "] " fmt, ##__VA_ARGS__)
#define OP_INFO(fmt, ...) ::console::OperatorInfo(fmt, ##__VA_ARGS__)
#define OP_WARN(fmt, ...) ::console::OperatorWarn(fmt, ##__VA_ARGS__)
#define OP_ERR(fmt, ...) ::console::OperatorError(fmt, ##__VA_ARGS__)
