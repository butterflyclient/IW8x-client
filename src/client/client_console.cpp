#include "client/client_console.h"

#include "engine/addresses.hpp"

#include "client/online_frontend.h"
#include "client/watermark.h"
#include "client/console/game_console.hpp"
#include "engine/console.hpp"

#include "engine/commands.h"

#include "common/console.h"

#include "engine/game.h"

#include "diagnostics/join.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_stats.h"
#include "auth/profile/local_progression.h"
#include "auth/profile/local_playercard.h"
#include "network/online_services.h"
#include "network/backend/frontend_backend.h"
#include "network/playlists.h"
#include "network/online_services_protocol.h"

#include "engine/noclip.h"
#include "engine/camo_changer.h"
#include "engine/safemem.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>
#include <string_view>
#include <mutex>
#include <deque>
namespace clientconsole
{
	namespace
	{
		std::mutex terminalLock;
		std::deque<std::string> terminalCommands;

		bool QueueTerminal(std::string_view command)
		{
			std::lock_guard guard(terminalLock);
			if (terminalCommands.size() >= 32) return false;
			terminalCommands.emplace_back(command);
			return true;
		}

		struct Binding
		{
			uintptr_t rva;
			const uint8_t* bytes;
			size_t size;
		};

		constexpr uint8_t kOverlayBytes[] = {
			0x8b,
			0xca,
			0xe9,
			0x79,
			0x39,
			0x01,
			0x00,
		};
		constexpr uint8_t kKeyEventBytes[] = {
			0x48,
			0x89,
			0x5c,
			0x24,
			0x10,
			0x48,
			0x89,
			0x6c,
			0x24,
			0x20,
			0x56,
			0x57,
			0x41,
			0x54,
		};
		constexpr uint8_t kIsMainThreadBytes[] = {
			0x48,
			0x83,
			0xec,
			0x28,
			0xff,
			0x15,
			0x56,
			0x22,
			0x08,
			0x01,
			0x3b,
			0x05,
			0x90,
			0x01,
			0x2b,
			0x0c,
			0x0f,
			0x94,
			0xc0,
			0x48,
			0x83,
			0xc4,
			0x28,
			0xc3,
		};

		constexpr Binding kOverlay{ 0x15e1340, kOverlayBytes, sizeof(kOverlayBytes) };
		constexpr Binding kKeyEvent{ 0x15beb80, kKeyEventBytes, sizeof(kKeyEventBytes) };
		constexpr Binding kIsMainThread{ 0x12cf280, kIsMainThreadBytes, sizeof(kIsMainThreadBytes) };
		constexpr Binding kAddConsoleText{ game::kAddConsoleTextRVA, game::kAddConsoleTextPrologue, sizeof(game::kAddConsoleTextPrologue) };
		constexpr Binding kPartyStartLANServerJoin{ game::kPartyStartLANServerJoinRVA, game::kPartyStartLANServerJoinPrologue,
			sizeof(game::kPartyStartLANServerJoinPrologue) };
		constexpr Binding kPartyAtomicAcceptInviteMP{ game::kPartyAtomicAcceptInviteMPRVA, game::kPartyAtomicAcceptInviteMPPrologue,
			sizeof(game::kPartyAtomicAcceptInviteMPPrologue) };

		struct bdSecurityID
		{
			uint8_t ab[8];
		};

		struct XNADDR
		{
			uint8_t addrBuff[84];
		};

		struct bdSecurityKey
		{
			uint8_t ab[16];
		};

		struct XSECURITY_INFO
		{
			bdSecurityID id;
			bdSecurityKey key;
		};

		struct XSESSION_INFO
		{
			XNADDR address;
			XSECURITY_INFO security;
		};

		static_assert(sizeof(XSESSION_INFO) == 0x6c);

		using OverlayFn = uintptr_t(__fastcall*)(uintptr_t self, int localClientNum);
		using KeyEventFn = void(__fastcall*)(int localClientNum, int key, bool down, unsigned time, int virtualKey, int controllerIndex);
		using AddTextFn = void(__fastcall*)(int localClientNum, const char* text);
		using PartyStartLANServerJoinFn = void(__fastcall*)(int localClientNum, const XSESSION_INFO* hostInfo);
		using PartyAtomicAcceptInviteMPFn = void(__fastcall*)(
			unsigned controllerIndex, const XSESSION_INFO* hostInfo, uint64_t invitedXuid, int joinLobby);

		uintptr_t g_base = 0;
		std::atomic<OverlayFn> g_overlay{ nullptr };
		std::atomic<KeyEventFn> g_keyEvent{ nullptr };
		std::atomic<AddTextFn> g_addText{ nullptr };
		std::atomic<bool> g_ready{ false };
		bool g_consumed[256]{};
		bool g_shift[2]{};
		bool g_rctrl = false;

		bool Matches(const Binding& binding)
		{
			uint8_t actual[32]{};
			return binding.size <= sizeof(actual) &&
				safemem::ReadBytes(reinterpret_cast<void*>(g_base + binding.rva), actual, binding.size) &&
				std::memcmp(actual, binding.bytes, binding.size) == 0;
		}

		template <typename Fn>
		Fn Function(const Binding& binding)
		{
			return reinterpret_cast<Fn>(g_base + binding.rva);
		}

		bool MainThread()
		{
			return Function<bool (*)()>(kIsMainThread)();
		}




		void ToggleLocalNoclip()
		{
			bool enabled = false;
			if (!noclip::LocalState(&enabled))
			{
				OP_WARN("Noclip: spawn in a locally hosted match first");
				return;
			}
			if (!commands::Queue(g_base, "cmd noclip\n")) OP_WARN("Noclip: command queue unavailable");
		}

		std::string TrimCommand(std::string value)
		{
			while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
				value.pop_back();
			size_t first = 0;
			while (first < value.size() && (value[first] == ' ' || value[first] == '\t'))
				++first;
			if (first) value.erase(0, first);
			return value;
		}

		bool HandleSessionCommand(const std::string& command)
		{
			constexpr std::string_view lanPrefix = "connectsession ";
			constexpr std::string_view invitePrefix = "connectsessioninvite ";
			std::string_view hex;
			bool invite = false;
			if (command.rfind(lanPrefix.data(), 0) == 0)
			{
				hex = std::string_view(command).substr(lanPrefix.size());
			}
			else if (command.rfind(invitePrefix.data(), 0) == 0)
			{
				hex = std::string_view(command).substr(invitePrefix.size());
				invite = true;
			}
			else
			{
				return false;
			}

			while (!hex.empty() && (hex.front() == ' ' || hex.front() == '\t'))
				hex.remove_prefix(1);
			while (!hex.empty() && (hex.back() == ' ' || hex.back() == '\t'))
				hex.remove_suffix(1);

			JoinSession(hex, invite);
			return true;
		}

		void __fastcall ConsoleText(int localClientNum, const char* text)
		{
			const auto original = g_addText.load(std::memory_order_acquire);
			if (!g_ready.load(std::memory_order_acquire) || localClientNum != 0 || !MainThread())
			{
				original(localClientNum, text);
				return;
			}

			char input[4096]{};
			const size_t length = safemem::ReadString(text, input, sizeof(input));
			if (!length || length >= sizeof(input) - 1)
			{
				original(localClientNum, text);
				return;
			}

			const std::string command = TrimCommand(input);
			if (command.empty()) return;
			if (console::HandleBuiltinCommand(command)) return;
			if (online_frontend::HandleCommand(command)) return;
			if (offlineauth::HandleCommand(command)) return;
			if (playlists::HandleCommand(command)) return;
			if (command == "join_status")
			{
				joindiag::Snapshot("join_status");
				offlineauth::LogSnapshot();
				return;
			}

			// Online / matchmaking commands â€” already fully implemented in online_services.
			// Add OP_INFO feedback so the user sees results in-game, and add 'playlist' alias.
			if (online_services::HandleCommand(command))
			{
				// Show user-visible feedback for key matchmaking commands.
				if (command == "online_status")
				{
					OP_INFO("Online: status logged (check mw120rproxy.log)");
				}
				else if (command == "online_start")
				{
					OP_INFO("Online: matchmaking client starting");
				}
				else if (command == "online_stop")
				{
					OP_INFO("Online: master client stopped");
				}
				else if (command == "mm_stop")
				{
					OP_INFO("Matchmaking: stopped");
				}
				return;
			}

			// 'playlist <id>' is a friendlier alias for 'mm_start <id>'.
			if (command == "playlist" || command.starts_with("playlist "))
			{
				const size_t sp = command.find(' ');
				if (sp == std::string::npos || sp + 1 >= command.size())
				{
					OP_WARN("Usage: playlist <0..128>");
				}
				else
				{
					const std::string mmCmd = "mm_start " + command.substr(sp + 1);
					online_services::HandleCommand(mmCmd);
				}
				return;
			}

			// 'addbots <n>' from the client console â€” queue to native server command buffer.
			if (command == "addbot" || command.starts_with("addbot ") || command == "addbots" || command.starts_with("addbots "))
			{
				std::string queued = command + "\n";
				if (commands::Queue(g_base, queued.c_str()))
				{
					LOG_INFO("ClientConsole", "queued bot command: %s", command.c_str());
					OP_INFO("Bot command queued: %s", command.c_str());
				}
				else
				{
					OP_WARN("Server not running or command buffer unavailable");
				}
				return;
			}

			if (HandleSessionCommand(command)) return;

			std::string lowerCmd = command;
			for (char& c : lowerCmd)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (lowerCmd == "noclip" || lowerCmd.rfind("noclip ", 0) == 0 || lowerCmd == "mw_noclip" ||
				lowerCmd.rfind("mw_noclip ", 0) == 0)
			{
				if (lowerCmd == "noclip" || lowerCmd == "mw_noclip")
				{
					ToggleLocalNoclip();
				}
				else
				{
					const size_t spacePos = lowerCmd.find(' ');
					std::string sub = spacePos != std::string::npos ? lowerCmd.substr(spacePos + 1) : "";
					while (!sub.empty() && (sub.front() == ' ' || sub.front() == '\t'))
						sub.erase(sub.begin());
					while (!sub.empty() && (sub.back() == ' ' || sub.back() == '\t'))
						sub.pop_back();
					if (sub == "on" || sub == "1" || sub == "off" || sub == "0")
					{
						bool current = false;
						if (!noclip::LocalState(&current))
							OP_WARN("Noclip: spawn in a locally hosted match first");
						else if (!commands::Queue(g_base, (sub == "on" || sub == "1") ? "cmd noclip on\n" : "cmd noclip off\n"))
							OP_WARN("Noclip: command queue unavailable");
					}
					else
						OP_WARN("Usage: noclip [on|off|1|0]");
				}
				return;
			}

			std::string queued(input);
			if (queued.empty() || queued.back() != '\n') queued.push_back('\n');
			if (commands::Queue(g_base, queued.c_str()))
			{
				LOG_INFO("ClientConsole", "queued native command: %s", command.c_str());
				return;
			}

			LOG_WARN("ClientConsole", "native Cbuf unavailable; forwarding console text to stock handler");
			original(localClientNum, text);
		}

		uintptr_t __fastcall DrawOverlay(uintptr_t self, int localClientNum)
		{
			const auto original = g_overlay.load(std::memory_order_acquire);
			const uintptr_t result = original(self, localClientNum);
			if (g_ready.load(std::memory_order_acquire) && localClientNum == 0 && MainThread())
			{
				noclip::Pump();
				camo_changer::Pump();
				frontend_backend::Pump();
				if (offlineauth::InitializeProfile())
				{
					local_progression::Pump();
					local_playercard::Pump();
					local_stats::Pump();
				}
				online_frontend::Pump();
				playlists::Pump();
				online_services::Pump();
				joindiag::Pump();
				game_console::draw();
				watermark::draw();
			}
			return result;
		}

		void __fastcall KeyEvent(int localClientNum, int key, bool down, unsigned time, int virtualKey, int controllerIndex)
		{
			const auto original = g_keyEvent.load(std::memory_order_acquire);
			if (!g_ready.load(std::memory_order_acquire) || localClientNum != 0 || !MainThread() || key < 0 || key >= 256)
			{
				original(localClientNum, key, down, time, virtualKey, controllerIndex);
				return;
			}

			if (key == 140 || key == 141) g_shift[key - 140] = down;

			// Track Right Control state.
			// In IW engine keycodes, 143 is K_RCTRL (142 is K_LCTRL).
			// In Windows, VK_RCONTROL is 0xa3. If the engine forwards generic VK_CONTROL (0x11),
			// query physical key state to distinguish Right Control.
			if (key == 143 || virtualKey == VK_RCONTROL)
			{
				g_rctrl = down;
			}
			else if (key == 142 || virtualKey == VK_CONTROL)
			{
				const bool physicalRctrl = ((GetKeyState(VK_RCONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0);
				g_rctrl = down && physicalRctrl;
			}

			if (!down)
			{
				if (g_consumed[key])
				{
					g_consumed[key] = false;
					return;
				}
				original(localClientNum, key, down, time, virtualKey, controllerIndex);
				return;
			}

			// Right Control + N hotkey: quick in-game toggle for noclip.
			const bool isRctrlDown =
				g_rctrl || ((GetKeyState(VK_RCONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0);
			const bool isNKey = (virtualKey == 'N' || virtualKey == 'n' || key == 'n' || key == 'N');

			if (isRctrlDown && isNKey)
			{
				if (!g_consumed[key])
				{
					QueueTerminal("noclip");
					LOG_INFO("ClientConsole", "noclip hotkey (Right Ctrl + N) pressed (key=%d virtual=%d)", key, virtualKey);
				}
				g_consumed[key] = true;
				return;
			}

			const bool toggle_key = key == 96 || virtualKey == VK_OEM_3 || key == 0xa0 || virtualKey == VK_F7;
			if (g_consumed[key] && (toggle_key || !engine::console::active())) return;
			const bool shift = g_shift[0] || g_shift[1] || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (game_console::key_event(key, down, virtualKey, shift))
			{
				g_consumed[key] = true;
				return;
			}

			original(localClientNum, key, down, time, virtualKey, controllerIndex);
		}
	}

	void pump_commands()
	{
		if (!g_ready.load(std::memory_order_acquire) || !MainThread()) return;
		std::deque<std::string> pending;
		{
			std::lock_guard guard(terminalLock);
			pending.swap(terminalCommands);
		}
		for (const auto& command : pending)
		{
			ConsoleText(0, command.c_str());
		}
	}

	bool RequestNoclip(bool enabled)
	{
		bool current = false;
		if (!g_ready.load() || !MainThread() || !noclip::LocalState(&current)) return false;
		return QueueTerminal(enabled ? "noclip on" : "noclip off");
	}

	bool JoinSession(std::string_view hex, bool invite, uint64_t invitedXuid)
	{
		if (!g_ready.load(std::memory_order_acquire) || !MainThread()) return false;
		online_services::protocol::Session bytes{};
		if (!online_services::protocol::ParseSession(hex, bytes))
		{
			LOG_ERR("LAN", "session must contain exactly 216 hexadecimal characters");
			return false;
		}
		joindiag::BeginAttempt(invite ? "PartyAtomic_AcceptInviteMP" : "Party_StartLANServerJoin");
		if (!offlineauth::InitializeProfile())
		{
			LOG_ERR("LAN", "join not dispatched: local auth/profile or MP stats assets are not ready; retry after entering local MP");
			return false;
		}
		XSESSION_INFO session{};
		memcpy(&session, bytes.data(), sizeof(session));
		if (invite)
		{
			LOG_INFO("LAN", "joining captured session through PartyAtomic_AcceptInviteMP");
			// Replay consumes four arguments; R9=0 selects the private menu party.
			Function<PartyAtomicAcceptInviteMPFn>(kPartyAtomicAcceptInviteMP)(0, &session, invitedXuid, 0);
		}
		else
		{
			LOG_INFO("LAN", "joining captured session through Party_StartLANServerJoin");
			Function<PartyStartLANServerJoinFn>(kPartyStartLANServerJoin)(0, &session);
		}
		joindiag::Pump();
		return true;
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (g_ready.load(std::memory_order_acquire)) return hook::Status::Installed;

		g_base = moduleBase;
		for (const Binding* binding : { &kIsMainThread, &kAddConsoleText,
				 &kPartyStartLANServerJoin, &kPartyAtomicAcceptInviteMP })
		{
			if (!Matches(*binding))
			{
				LOG_ERR("ClientConsole", "Replay binding mismatch at RVA 0x%llX", static_cast<unsigned long long>(binding->rva));
				return hook::Status::NotReady;
			}
		}

		auto status =
			hook::Install(reinterpret_cast<void*>(moduleBase + kOverlay.rva), &DrawOverlay, kOverlay.bytes, kOverlay.size, g_overlay);
		if (status != hook::Status::Installed) return status;

		status = hook::Install(reinterpret_cast<void*>(moduleBase + kKeyEvent.rva), &KeyEvent, kKeyEvent.bytes, kKeyEvent.size, g_keyEvent);
		if (status != hook::Status::Installed) return status;

		status = hook::Install(reinterpret_cast<void*>(moduleBase + kAddConsoleText.rva), &ConsoleText, kAddConsoleText.bytes,
			kAddConsoleText.size, g_addText);
		if (status == hook::Status::Installed)
		{
			status = game_console::install(moduleBase, &QueueTerminal);
			if (status != hook::Status::Installed) return status;
			g_ready.store(true, std::memory_order_release);
			// Optional local terminal for multiple-instance diagnostics. Commands
			// still execute through the existing client console on the game thread.
			if (wcsstr(GetCommandLineW(), L"--client-console"))
			{
				console::SetCommandHandler(&QueueTerminal);
				console::EnableCommandInput(moduleBase);
			}
			LOG_INFO("ClientConsole", "IW7-Mod console installed for Replay; ~ or F7 opens it; Shift+~ shows scrollback");
		}
		return status;
	}
}
