#include "backend/backend.h"

#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <thread>
#include <algorithm>
#include <atomic>
namespace
{
	HWND window, list, status, command, runButton, playlistChoice;
	HFONT font, mono;
	HBRUSH background = CreateSolidBrush(RGB(18, 18, 20)), panel = CreateSolidBrush(RGB(30, 32, 36));
	std::atomic<bool> loading = false;
	std::thread starter;
	int tab = 0;
	uint64_t cursor = 0;
	uint16_t httpPort = 27128, udpPort = 27126;
	constexpr const wchar_t* tabs[] = { L"ALL LOGS", L"PLAYERS", L"LOBBY", L"HTTP", L"MATCHMAKING", L"STORAGE", L"BGS", L"ERRORS",
		L"METRICS" };

	std::wstring Wide(const std::string& s)
	{
		if (s.empty()) return {};
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
		return w;
	}

	void Cell(int row, int col, const std::wstring& text)
	{
		if (!col)
		{
			LVITEMW item{};
			item.mask = LVIF_TEXT;
			item.iItem = row;
			item.pszText = const_cast<wchar_t*>(text.c_str());
			ListView_InsertItem(list, &item);
		}
		else
			ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
	}

	void Row(std::string a, std::string b, std::string c)
	{
		int row = ListView_GetItemCount(list);
		Cell(row, 0, Wide(a));
		Cell(row, 1, Wide(b));
		Cell(row, 2, Wide(c));
	}

	void Reset()
	{
		ListView_DeleteAllItems(list);
		cursor = 0;
	}

	void Refresh()
	{
		if (!loading && backend120::Running()) backend120::PollPlaylistScene();
		const auto catalog = backend120::PlaylistStatus();
		const auto catalogMode = catalog.value("mode", std::string("auto"));
		SendMessageW(playlistChoice, CB_SETCURSEL, catalogMode == "tu19" ? 1 : catalogMode == "tu27" ? 2 : 0, 0);
		EnableWindow(playlistChoice, !loading && backend120::Running());
		const auto s = backend120::Status();
		std::wstring text = loading ? L"Starting services…" : backend120::Running() ? L"ONLINE" : L"STOPPED";
		text += L"  •  HTTP " + std::to_wstring(httpPort) + L"  •  " + Wide(s.value("state", std::string{})) + L"  •  " +
			std::to_wstring(s.value("requests", uint64_t{})) + L" requests";
		text += L"  |  Playlist: " + Wide(catalog.value("active", std::string{}));
		SetWindowTextW(status, text.c_str());
		SetWindowTextW(runButton, backend120::Running() ? L"Stop backend" : L"Start backend");
		EnableWindow(runButton, !loading);
		if (tab == 1 || tab == 2 || tab == 5 || tab == 8)
		{
			ListView_DeleteAllItems(list);
			if (tab == 1)
				for (const auto& p : backend120::Players())
					Row(p.value("name", std::string{}), p.value("xuid", std::string{}),
						p.value("activity", std::string{}) + " | " + std::to_string(p.value("friends", size_t{})) + " friends | " +
							p.value("map", std::string{}));
			if (tab == 2)
			{
				Row("Dedicated", s.value("state", std::string{}), s.value("map", std::string{}));
				Row("Playlist", std::to_string(s.value("playlist", -1)), s.value("mode", std::string{}));
				Row("Queue", std::to_string(s.value("queue_players", size_t{})), "Waiting players");
				for (const auto& p : s.at("players"))
					Row(p.value("name", std::string{}), p.value("xuid", std::string{}), "Team " + std::to_string(p.value("team", 0)));
			}
			if (tab == 5)
			{
				for (const char* dir : { "profiles", "settings", "progression", "custom_mods", "tournaments" })
				{
					std::error_code e;
					uint64_t count = 0, size = 0;
					const auto path = std::filesystem::path("players/backend") / dir;
					if (std::filesystem::exists(path))
						for (const auto& f : std::filesystem::directory_iterator(path, e))
							if (f.is_regular_file())
							{
								++count;
								size += f.file_size();
							}
					Row(dir, std::to_string(count) + " files", std::to_string(size) + " bytes");
				}
				Row("Data folder", "", std::filesystem::current_path().string());
			}
			if (tab == 8)
			{
				Row("HTTP requests", std::to_string(s.value("requests", uint64_t{})), "Since process start");
				Row("HTTP errors", std::to_string(s.value("errors", uint64_t{})), "Responses >= 400");
				Row("Uptime", std::to_string(s.value("uptime_ms", uint64_t{}) / 1000) + " seconds", "");
				Row("Dedicated heartbeat", s.value("dedicated_online", false) ? "Live" : "Offline", "Lease expires after 8 seconds");
			}
		}
		else
		{
			for (const auto& r : backend120::Logs(cursor))
			{
				cursor = r.sequence;
				if (tab == 0 || r.category == std::string(tab == 3 ? "HTTP" : tab == 4 ? "MATCHMAKING" : tab == 6 ? "BGS" : "ERRORS"))
					Row(r.time, r.category, r.text);
			}
			while (ListView_GetItemCount(list) > 1500)
				ListView_DeleteItem(list, 0);
		}
	}

	void Start()
	{
		if (loading || backend120::Running()) return;
		if (starter.joinable()) starter.join();
		loading = true;
		starter = std::thread(
			[]
			{
				backend120::Start(httpPort, udpPort);
				loading = false;
			});
	}

	LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_CREATE: {
			font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
			mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
			for (int i = 0; i < 9; ++i)
				CreateWindowW(
					L"BUTTON", tabs[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, w, HMENU(INT_PTR(100 + i)), nullptr, nullptr);
			status = CreateWindowW(L"STATIC", L"Starting…", WS_CHILD | WS_VISIBLE, 16, 62, 700, 24, w, nullptr, nullptr, nullptr);
			SendMessage(status, WM_SETFONT, WPARAM(font), TRUE);
			runButton = CreateWindowW(
				L"BUTTON", L"Start backend", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 16, 98, 150, 32, w, HMENU(201), nullptr, nullptr);
			CreateWindowW(
				L"BUTTON", L"Open data folder", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 178, 98, 155, 32, w, HMENU(202), nullptr, nullptr);
			CreateWindowW(
				L"BUTTON", L"Clear view", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 346, 98, 115, 32, w, HMENU(203), nullptr, nullptr);
			auto label =
				CreateWindowW(L"STATIC", L"Push playlist:", WS_CHILD | WS_VISIBLE, 484, 104, 100, 24, w, nullptr, nullptr, nullptr);
			SendMessageW(label, WM_SETFONT, WPARAM(font), TRUE);
			playlistChoice = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 590, 98, 275, 180, w,
				HMENU(206), nullptr, nullptr);
			SendMessageW(playlistChoice, WM_SETFONT, WPARAM(font), TRUE);
			for (auto name : { L"Auto (selected frontend)", L"TU19", L"TU27" })
				SendMessageW(playlistChoice, CB_ADDSTRING, 0, LPARAM(name));
			SendMessageW(playlistChoice, CB_SETCURSEL, 0, 0);
			list = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 16, 142, 950,
				450, w, nullptr, nullptr, nullptr);
			SendMessage(list, WM_SETFONT, WPARAM(mono), TRUE);
			ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
			ListView_SetBkColor(list, RGB(22, 23, 26));
			ListView_SetTextBkColor(list, RGB(22, 23, 26));
			ListView_SetTextColor(list, RGB(198, 211, 220));
			const wchar_t* cols[] = { L"Time / Name", L"Category / Value", L"Details" };
			for (int i = 0; i < 3; ++i)
			{
				LVCOLUMNW c{};
				c.mask = LVCF_TEXT | LVCF_WIDTH;
				c.pszText = const_cast<wchar_t*>(cols[i]);
				c.cx = i == 2 ? 670 : 150;
				ListView_InsertColumn(list, i, &c);
			}
			command = CreateWindowExW(
				WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 610, 750, 30, w, HMENU(204), nullptr, nullptr);
			SendMessage(command, WM_SETFONT, WPARAM(mono), TRUE);
			SendMessageW(
				command, EM_SETCUEBANNER, TRUE, LPARAM(L"Command: unlock all Player#0  |  relock all Player#0  |  playlist_reload"));
			CreateWindowW(L"BUTTON", L"Execute", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 780, 610, 120, 30, w, HMENU(205), nullptr, nullptr);
			SetTimer(w, 1, 750, nullptr);
			Start();
			return 0;
		}
		case WM_SIZE: {
			int width = LOWORD(lp), height = HIWORD(lp);
			for (int i = 0; i < 9; ++i)
				MoveWindow(GetDlgItem(w, 100 + i), 12 + i * (width - 24) / 9, 12, (width - 24) / 9 - 2, 36, TRUE);
			MoveWindow(status, 16, 62, width - 32, 25, TRUE);
			MoveWindow(list, 16, 142, width - 32, height - 202, TRUE);
			ListView_SetColumnWidth(list, 2, std::max(250, width - 355));
			MoveWindow(command, 16, height - 46, width - 166, 30, TRUE);
			MoveWindow(GetDlgItem(w, 205), width - 138, height - 46, 122, 30, TRUE);
			return 0;
		}
		case WM_GETMINMAXINFO:
			reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { 960, 500 };
			return 0;
		case WM_TIMER:
			Refresh();
			return 0;
		case WM_COMMAND:
			if (LOWORD(wp) >= 100 && LOWORD(wp) < 109)
			{
				tab = LOWORD(wp) - 100;
				Reset();
				InvalidateRect(w, nullptr, FALSE);
				Refresh();
			}
			else if (LOWORD(wp) == 206 && HIWORD(wp) == CBN_SELCHANGE)
			{
				const int choice = int(SendMessageW(playlistChoice, CB_GETCURSEL, 0, 0));
				backend120::SetPlaylistMode(choice == 1 ? "tu19" : choice == 2 ? "tu27" : "auto");
				Refresh();
			}
			else if (LOWORD(wp) == 201 && !loading)
			{
				if (backend120::Running())
				{
					if (starter.joinable()) starter.join();
					loading = true;
					starter = std::thread(
						[]
						{
							backend120::Stop();
							backend120::Record("CONTROL", "Backend stopped by operator");
							loading = false;
						});
				}
				else
					Start();
			}
			else if (LOWORD(wp) == 202)
				ShellExecuteW(w, L"open", std::filesystem::current_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			else if (LOWORD(wp) == 203)
			{
				ListView_DeleteAllItems(list);
				auto logs = backend120::Logs();
				if (!logs.empty()) cursor = logs.back().sequence;
			}
			else if (LOWORD(wp) == 205)
			{
				wchar_t buffer[256];
				GetWindowTextW(command, buffer, 256);
				char utf[1024];
				int n = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf, sizeof(utf), nullptr, nullptr);
				if (n > 1 && !backend120::Command(utf)) backend120::Record("ERRORS", "Unknown command");
				SetWindowTextW(command, L"");
			}
			return 0;
		case WM_DRAWITEM: {
			auto d = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
			const bool selected = d->CtlID == 100 + tab;
			HBRUSH b = CreateSolidBrush(selected ? RGB(32, 95, 116) : RGB(27, 48, 59));
			FillRect(d->hDC, &d->rcItem, b);
			DeleteObject(b);
			SetBkMode(d->hDC, TRANSPARENT);
			SetTextColor(d->hDC, RGB(224, 236, 242));
			SelectObject(d->hDC, font);
			wchar_t text[100];
			GetWindowTextW(d->hwndItem, text, 100);
			DrawTextW(d->hDC, text, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			return TRUE;
		}
		case WM_CTLCOLORSTATIC:
			SetTextColor(HDC(wp), RGB(142, 189, 203));
			SetBkColor(HDC(wp), RGB(18, 18, 20));
			return LRESULT(background);
		case WM_CTLCOLOREDIT:
			SetTextColor(HDC(wp), RGB(225, 232, 238));
			SetBkColor(HDC(wp), RGB(30, 32, 36));
			return LRESULT(panel);
		case WM_CLOSE:
			DestroyWindow(w);
			return 0;
		case WM_DESTROY:
			KillTimer(w, 1);
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(w, msg, wp, lp);
	}
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int show)
{
	int count;
	auto args = CommandLineToArgvW(GetCommandLineW(), &count);
	bool headless = false;
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::filesystem::path data = std::filesystem::path(exe).parent_path();
	for (int i = 1; i < count; ++i)
	{
		std::wstring arg = args[i];
		if (arg == L"--data-dir" && i + 1 < count)
			data = args[++i];
		else if (arg == L"--http-port" && i + 1 < count)
			httpPort = uint16_t(_wtoi(args[++i]));
		else if (arg == L"--discovery-port" && i + 1 < count)
			udpPort = uint16_t(_wtoi(args[++i]));
		else if (arg == L"--headless")
			headless = true;
	}
	LocalFree(args);
	std::error_code ec;
	std::filesystem::create_directories(data, ec);
	if (ec || !SetCurrentDirectoryW(data.c_str()) || !httpPort || !udpPort) return 2;
	if (headless)
	{
		if (!backend120::Start(httpPort, udpPort)) return 3;
		while (backend120::Running())
		{
			backend120::PollPlaylistScene();
			Sleep(1000);
		}
		backend120::Stop();
		return 0;
	}
	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
	InitCommonControlsEx(&controls);
	WNDCLASSW cls{};
	cls.lpfnWndProc = Proc;
	cls.hInstance = app;
	cls.lpszClassName = L"MagmaBackend120";
	cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
	cls.hbrBackground = background;
	RegisterClassW(&cls);
	window = CreateWindowW(cls.lpszClassName, L"Magma Backend — MW2019 1.20", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
		nullptr, nullptr, app, nullptr);
	ShowWindow(window, show);
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (starter.joinable()) starter.join();
	backend120::Stop();
	DeleteObject(font);
	DeleteObject(mono);
	return 0;
}
