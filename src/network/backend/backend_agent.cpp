#include "network/backend/backend_agent.h"
#include "network/native_network.h"
#include "dedicated/dedicated.h"
#include "common/deployment_profile.h"
#include "network/match_http.h"
#include "network/profile/profile_store.h"
#include "engine/camo_changer.h"
#include "network/commerce/unlock_policy.h"
#include "common/console.h"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
namespace backend_agent
{
	namespace
	{
		using Json = nlohmann::json;
		std::mutex mutex;
		std::condition_variable wake;
		std::thread worker;
		bool stopping = false;
		Json state, players = Json::array(), teams = Json::object();
		std::deque<challenges::Event> incoming;
		std::map<std::string, std::string> pending;
		std::map<std::pair<uint64_t, int>, std::pair<int, uint64_t>> ownership;
		std::deque<std::string> commands;
		std::wstring origin;
		std::string key, instance, command, lastCommand;
		int selection = -1, rejected = -1;
		uint64_t validUntil = 0;
		std::filesystem::path outbox = L"players/host-outbox";

		bool standalone()
		{
			static const bool enabled = []
			{
				wchar_t path[MAX_PATH]{}, mode[32]{};
				if (!native_network::ConfigurationPath(path, MAX_PATH)) return true;
				GetPrivateProfileStringW(L"online_services", L"backend", L"standalone", mode, 32, path);
				return _wcsicmp(mode, L"embedded") != 0;
			}();
			return enabled;
		}

		std::string id_impl(const challenges::Event& e)
		{
			return std::to_string(e.session) + "_" + std::to_string(e.xuid) + "_" + std::to_string(e.sequence);
		}

		bool save(const std::filesystem::path& path, std::string_view bytes)
		{
			const auto temp = path.wstring() + L".tmp";
			HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f == INVALID_HANDLE_VALUE) return false;
			DWORD n = 0;
			const bool ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &n, nullptr) && n == bytes.size() && FlushFileBuffers(f);
			CloseHandle(f);
			return ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		}

		void run()
		{
			uint64_t nextLog = 0;
			for (;;)
			{
				Json request, queries = Json::array();
				std::deque<challenges::Event> batch;
				std::string admin;
				{
					std::unique_lock lock(mutex);
					wake.wait_for(lock, std::chrono::milliseconds(500));
					if (stopping && incoming.empty()) break;
					batch.swap(incoming);
				}
				for (const auto& e : batch)
				{
					const auto bytes = challenges::Pack(e);
					const auto hex = profile_store::Encode(std::vector<uint8_t>(bytes.begin(), bytes.end()));
					const auto id = id_impl(e);
					if (save(outbox / (id + ".event"), hex))
						pending[id] = hex;
					else
					{
						std::lock_guard lock(mutex);
						incoming.push_back(e);
						LOG_ERR("BackendAgent", "could not persist award; retained in queue");
					}
				}
				{
					std::lock_guard lock(mutex);
					if (stopping) break;
					if (state.is_null()) continue;
					request = { { "version", 1 }, { "build", "iw8-1.20.4" }, { "key", key }, { "instance", instance }, { "state", state },
						{ "events", Json::array() }, { "rejected", rejected } };
					request["state"]["players"] = players;
					for (const auto& [item, value] : ownership)
						if (GetTickCount64() >= value.second && queries.size() < 64)
							queries.push_back({ { "xuid", std::to_string(item.first) }, { "item", item.second } });
					if (!commands.empty()) admin = commands.front();
				}
				std::map<std::tuple<uint64_t, uint64_t, uint64_t>, std::string> ordered;
				for (const auto& [id, hex] : pending)
				{
					auto bytes = profile_store::Decode(hex);
					auto e = challenges::Unpack(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
					if (e) ordered[{ e->session, e->xuid, e->sequence }] = hex;
				}
				size_t eventBytes = 0;
				for (const auto& [id, hex] : ordered)
				{
					if (request["events"].size() == 64 || eventBytes + hex.size() > 131072) break;
					request["events"].push_back(hex);
					eventBytes += hex.size();
				}
				request["ownership"] = queries;
				request["admin"] = admin;
				request["state"]["camos"] = Json::object();
				for (const auto& p : request["state"]["players"])
					request["state"]["camos"][p.at("xuid").get<std::string>()] = camo_changer::ServerStatus(chat::Id(p, "xuid"));
				const auto reply = match_http::Post(origin, L"/v1/host/poll", request.dump());
				try
				{
					if (reply.status != 200) throw std::runtime_error("HTTP " + std::to_string(reply.status));
					const auto result = Json::parse(reply.body);
					if (result.at("version") != 1 || result.at("instance") != instance) throw std::runtime_error("invalid host response");
					for (const auto& ack : result.at("events_ack"))
					{
						const auto id = ack.get<std::string>();
						if (pending.contains(id))
						{
							std::error_code ec;
							std::filesystem::remove(outbox / (id + ".event"), ec);
							if (!ec) pending.erase(id);
						}
					}
					std::lock_guard lock(mutex);
					selection = result.at("playlist");
					command = result.at("command");
					validUntil = GetTickCount64() + 3000;
					teams = result.at("tournament").at("teams");
					for (const auto& row : result.at("ownership"))
					{
						auto item = std::make_pair(chat::Id(row, "xuid"), row.at("item").get<int>());
						if (ownership.contains(item)) ownership[item] = { row.at("owns").get<int>(), GetTickCount64() + 2000 };
					}
					if (!admin.empty() && !commands.empty() && commands.front() == admin) commands.pop_front();
					rejected = -1;
				}
				catch (const std::exception& e)
				{
					if (GetTickCount64() >= nextLog)
					{
						LOG_WARN("BackendAgent", "standalone backend unavailable: %s; session and awards retained", e.what());
						nextLog = GetTickCount64() + 10000;
					}
				}
			}
		}
	}

	bool Enabled()
	{
		return dedicated::Enabled() && standalone();
	}

	bool Start(const std::wstring& target)
	{
		if (worker.joinable()) return true;
		try
		{
			wchar_t config[MAX_PATH]{}, path[MAX_PATH]{};
			native_network::ConfigurationPath(config, MAX_PATH);
			GetPrivateProfileStringW(L"online_services", L"host_key_file", L"players/backend/host.key", path, MAX_PATH, config);
			std::ifstream f(path);
			f >> key;
			if (!chat::Hex(key, 64))
			{
				LOG_ERR("BackendAgent", "missing host key file: start MagmaBackend120 first");
				return false;
			}
			std::vector<uint8_t> random(16);
			if (BCryptGenRandom(nullptr, random.data(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
			instance = profile_store::Encode(random);
			std::filesystem::create_directories(outbox);
			pending.clear();
			for (const auto& p : std::filesystem::directory_iterator(outbox))
				if (p.path().extension() == L".event")
				{
					if (p.file_size() > 16384) return false;
					std::ifstream stream(p.path());
					std::string hex;
					stream >> hex;
					auto bytes = profile_store::Decode(hex);
					auto event = challenges::Unpack(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
					if (!event || p.path().stem().string() != id_impl(*event)) return false;
					pending[id_impl(*event)] = hex;
				}
			origin = target;
			stopping = false;
			worker = std::thread(run);
			LOG_INFO("BackendAgent", "connected to external backend; dedicated process owns gameplay only");
			return true;
		}
		catch (...)
		{
			LOG_ERR("BackendAgent", "host configuration/outbox invalid; files retained");
			return false;
		}
	}

	void Stop()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		wake.notify_all();
		if (worker.joinable()) worker.join();
	}

	void Publish(Json snapshot)
	{
		auto& hex = snapshot["session"].get_ref<std::string&>();
		for (auto& c : hex)
			if (c >= 'A' && c <= 'F') c += 32;
		std::lock_guard lock(mutex);
		state = std::move(snapshot);
	}

	void Roster(const std::vector<chat::Player>& roster)
	{
		Json copy = Json::array();
		for (const auto& p : roster)
			copy.push_back({ { "xuid", std::to_string(p.xuid) }, { "name", p.name }, { "team", p.team } });
		std::lock_guard lock(mutex);
		players = std::move(copy);
	}

	bool TakePlaylist(int& result)
	{
		std::lock_guard lock(mutex);
		if (GetTickCount64() > validUntil || selection < 0 || command.empty() || command == lastCommand) return false;
		result = selection;
		lastCommand = command;
		return true;
	}

	void Reset()
	{
		std::lock_guard lock(mutex);
		if (!state.is_null())
		{
			state["resetting"] = true;
			state["ready"] = false;
			state["session"] = "";
		}
		wake.notify_all();
	}

	void Reject(int p)
	{
		std::lock_guard lock(mutex);
		rejected = p;
	}

	int Team(uint64_t id)
	{
		std::lock_guard lock(mutex);
		return teams.value(std::to_string(id), 0);
	}

	void Event(const challenges::Event& e)
	{
		std::lock_guard lock(mutex);
		if (incoming.size() >= 8192)
		{
			LOG_ERR("BackendAgent", "award queue exhausted");
			return;
		}
		incoming.push_back(e);
		wake.notify_all();
	}

	int Owns(uint64_t id, int item)
	{
		std::unique_lock lock(mutex, std::try_to_lock);
		if (!lock) return -1;
		if (ownership.size() >= 4096 && !ownership.contains({ id, item })) return -1;
		auto [it, fresh] = ownership.try_emplace({ id, item }, std::pair{ -1, uint64_t(0) });
		return GetTickCount64() < it->second.second ? it->second.first : -1;
	}

	bool Command(std::string_view cmd)
	{
		if (!unlock_policy::Parse(cmd) && !cmd.starts_with("progression_premium ") && cmd != "playlist_reload" && cmd != "progression_perf")
			return false;
		std::lock_guard lock(mutex);
		if (commands.size() < 32)
		{
			commands.emplace_back(cmd);
			wake.notify_all();
			OP_INFO("Command queued for standalone backend");
		}
		else
			OP_WARN("Backend command queue is full");
		return true;
	}

	bool EnsureLocal()
	{
		if (!standalone() || deployment::kClientOnly) return true;
		wchar_t config[MAX_PATH]{}, host[256]{};
		if (!native_network::ConfigurationPath(config, MAX_PATH)) return false;
		GetPrivateProfileStringW(L"online_services", L"host", deployment::kBackendHost, host, 256, config);
		if (_wcsicmp(host, L"127.0.0.1") && _wcsicmp(host, L"localhost")) return true;
		const auto port = GetPrivateProfileIntW(L"online_services", L"http_port", 27128, config),
				   udp = GetPrivateProfileIntW(L"online_services", L"port", 27126, config);
		const auto url = L"http://127.0.0.1:" + std::to_wstring(port);
		if (match_http::Post(url, L"/health", "{}").status == 200) return true;
		wchar_t game[MAX_PATH]{};
		GetModuleFileNameW(nullptr, game, MAX_PATH);
		const auto root = std::filesystem::path(game).parent_path(), exe = root / L"MagmaBackend120.exe";
		if (!std::filesystem::exists(exe))
		{
			LOG_ERR("BackendAgent", "MagmaBackend120.exe missing beside game executable");
			return false;
		}
		std::wstring args = L"\"" + exe.wstring() + L"\" --data-dir \"" + root.wstring() + L"\" --http-port " + std::to_wstring(port) +
			L" --discovery-port " + std::to_wstring(udp);
		STARTUPINFOW startup{ sizeof(startup) };
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_SHOWNOACTIVATE;
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(exe.c_str(), args.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process)) return false;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		for (int i = 0; i < 40; ++i)
		{
			Sleep(250);
			if (match_http::Post(url, L"/health", "{}").status == 200) return true;
		}
		return false;
	}
}
