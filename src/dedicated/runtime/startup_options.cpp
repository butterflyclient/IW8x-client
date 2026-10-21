#include "asset_mark_guard.h"
#include "auto_server.h"
#include "dedicated.h"

#include "window.h"

#include "bap.h"
#include "bot_command_cache.h"
#include "bot_goal_wait.h"
#include "bot_profiler.h"
#include "bot_movement_batch.h"
#include "bot_nav_gpu.h"
#include "commands.h"
#include "console.h"

#include "game.h"

#include "join.h"
#include "noclip.h"
#include "online_services.h"
#include "playlists.h"

#include "safemem.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <string_view>

#include <intrin.h>

#include "dedicated/runtime/internal.hpp"
namespace dedicated::detail
{
	bool HasFlag(const wchar_t* flag)
	{
		// Keep the normal game completely untouched unless --dedi is supplied.
		// Match a complete command-line argument, not a substring of another flag/path.
		const wchar_t* commandLine = GetCommandLineW();
		if (!commandLine) return false;

		const size_t flagLength = wcslen(flag);
		for (const wchar_t* p = commandLine; (p = wcsstr(p, flag)) != nullptr; p += flagLength)
		{
			const bool leftBoundary = p == commandLine || p[-1] == L' ' || p[-1] == L'\t' || p[-1] == L'"';
			const wchar_t tail = p[flagLength];
			const bool rightBoundary = tail == L'\0' || tail == L' ' || tail == L'\t' || tail == L'"';
			if (leftBoundary && rightBoundary) return true;
		}
		return false;
	}

	bool StartupTokenChar(wchar_t c)
	{
		return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
	}

	ArgumentStatus ReadStartupToken(const wchar_t* flag, char* out, size_t outSize)
	{
		if (!flag || !out || outSize < 2) return ArgumentStatus::Invalid;
		const wchar_t* commandLine = GetCommandLineW();
		if (!commandLine) return ArgumentStatus::Missing;
		const size_t flagLength = wcslen(flag);
		for (const wchar_t* p = commandLine; (p = wcsstr(p, flag)) != nullptr; ++p)
		{
			const bool leftBoundary = p == commandLine || p[-1] == L' ' || p[-1] == L'\t' || p[-1] == L'"';
			if (!leftBoundary) continue;
			const wchar_t* value = p + flagLength;
			if (*value == L'=')
			{
				++value;
			}
			else if (*value == L' ' || *value == L'\t')
			{
				while (*value == L' ' || *value == L'\t')
					++value;
			}
			else
			{
				continue;
			}
			bool quoted = false;
			if (*value == L'"')
			{
				quoted = true;
				++value;
			}
			size_t length = 0;
			while (value[length] && ((quoted && value[length] != L'"') || (!quoted && value[length] != L' ' && value[length] != L'\t')))
			{
				if (value[length] > 0x7F || length + 1 >= outSize) return ArgumentStatus::Invalid;
				out[length] = static_cast<char>(value[length]);
				++length;
			}
			if (!length || (quoted && value[length] != L'"')) return ArgumentStatus::Invalid;
			out[length] = '\0';
			// Map arguments name a zone; accept its filename without passing the
			// extension to devmap. Other arguments retain their token-only grammar.
			if (!wcscmp(flag, L"--map") && length > 3 && !_stricmp(out + length - 3, ".ff"))
			{
				length -= 3;
				out[length] = '\0';
			}
			for (size_t i = 0; i < length; ++i)
			{
				if (!StartupTokenChar(static_cast<wchar_t>(out[i]))) return ArgumentStatus::Invalid;
			}
			return ArgumentStatus::Valid;
		}
		return ArgumentStatus::Missing;
	}

	void LoadStartupOptions()
	{
		strcpy_s(g_mapName, "mp_shipment");
		strcpy_s(g_gameType, "war");
		char value[64]{};
		const auto mapStatus = ReadStartupToken(L"--map", value, sizeof(value));
		g_explicitMap = mapStatus != ArgumentStatus::Missing;
		if (mapStatus == ArgumentStatus::Valid)
			strcpy_s(g_mapName, value);
		else if (mapStatus == ArgumentStatus::Invalid)
		{
			LOG_WARN("Dedicated", "invalid --map value; using default %s", g_mapName);
			OP_WARN("Invalid --map value | using %s", g_mapName);
		}

		value[0] = '\0';
		auto modeStatus = ReadStartupToken(L"--mode", value, sizeof(g_gameType));
		if (modeStatus == ArgumentStatus::Missing) modeStatus = ReadStartupToken(L"--gametype", value, sizeof(g_gameType));
		g_explicitMode = modeStatus != ArgumentStatus::Missing;
		if (modeStatus == ArgumentStatus::Valid)
			strcpy_s(g_gameType, value);
		else if (modeStatus == ArgumentStatus::Invalid)
		{
			LOG_WARN("Dedicated", "invalid --mode/--gametype value; using default %s", g_gameType);
			OP_WARN("Invalid --mode/--gametype value | using %s", g_gameType);
		}

		const auto number = [&](const wchar_t* flag, int& destination, int low, int high)
		{
			char text[32]{};
			const auto status = ReadStartupToken(flag, text, sizeof(text));
			if (status == ArgumentStatus::Missing) return;
			int parsed = -1;
			const auto result = std::from_chars(text, text + strlen(text), parsed);
			if (status != ArgumentStatus::Valid || result.ec != std::errc{} || result.ptr != text + strlen(text) || parsed < low ||
				parsed > high)
				g_optionsValid = false;
			else
				destination = parsed;
		};
		number(L"--playlist", g_playlistId, 0, 128);
		number(L"--playlist-entry", g_playlistEntry, 0, 2047);
		number(L"--playlist-games-remaining", g_resumedEntryGames, 1, 255);
		g_rotatePlaylist = HasFlag(L"--playlist-rotate");
		if (g_resumedEntryGames && !g_rotatePlaylist) g_optionsValid = false;
		if (g_rotatePlaylist && (g_playlistId < 0 || g_explicitMap || g_explicitMode)) g_optionsValid = false;
		number(L"--min-players", g_minPlayers, 1, 152);
		if (g_playlistId < 0 && !AutoPlaylist() && (g_playlistEntry != 0 || g_minPlayers != -1)) g_optionsValid = false;
		if (AutoPlaylist() && (g_playlistId >= 0 || g_explicitMap || g_explicitMode || g_rotatePlaylist || g_playlistEntry != 0))
			g_optionsValid = false;
		LOG_INFO("Dedicated", "startup selection: map=%s gametype=%s playlist=%d entry=%d min_players=%d", g_mapName, g_gameType,
			g_playlistId, g_playlistEntry, g_minPlayers);
	}
}
