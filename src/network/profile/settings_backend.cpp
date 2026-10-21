#include "network/profile/settings_backend.h"
#include "auth/profile/challenge_engine.h"
#include "common/console.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
namespace settings_backend
{
	namespace
	{
		using Json = settings120::Json;

		struct Record
		{
			settings120::Account account;
			bool damaged = false;
		};

		std::map<uint64_t, Record> accounts;
		std::mutex lock;

		std::filesystem::path path_impl(uint64_t id)
		{
			return std::filesystem::path(L"players/backend/settings") / (std::to_wstring(id) + L".json");
		}

		bool read(uint64_t owner, const std::filesystem::path& path, settings120::Account& account)
		{
			try
			{
				if (std::filesystem::file_size(path) > 1024 * 1024) return false;
				std::ifstream stream(path, std::ios::binary);
				Json wrapper;
				stream >> wrapper;
				const auto payload = wrapper.at("payload").get<std::string>();
				return wrapper.at("checksum") == std::to_string(challenges::Hash(payload)) && account.Restore(owner, Json::parse(payload));
			}
			catch (...)
			{
				return false;
			}
		}

		Record& load(uint64_t owner)
		{
			auto [it, fresh] = accounts.try_emplace(owner);
			auto& record = it->second;
			if (!fresh) return record;
			const auto path = path_impl(owner), backup = std::filesystem::path(path.wstring() + L".bak");
			if (!std::filesystem::exists(path) && !std::filesystem::exists(backup)) return record;
			if (read(owner, path, record.account)) return record;
			if (!read(owner, backup, record.account))
			{
				record.damaged = true;
				return record;
			}
			if (std::filesystem::exists(path) &&
				!MoveFileExW(
					path.c_str(), (path.wstring() + L".rejected." + std::to_wstring(GetTickCount64())).c_str(), MOVEFILE_WRITE_THROUGH))
				record.damaged = true;
			return record;
		}

		bool write(uint64_t owner, const settings120::Account& account)
		{
			try
			{
				const auto path = path_impl(owner);
				std::filesystem::create_directories(path.parent_path());
				const auto payload = account.Save().dump();
				const auto bytes = Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump();
				const auto temporary = path.wstring() + L".tmp";
				HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (file == INVALID_HANDLE_VALUE) return false;
				DWORD count = 0;
				const bool ok =
					WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file);
				CloseHandle(file);
				if (!ok) return false;
				if (std::filesystem::exists(path))
					return ReplaceFileW(path.c_str(), temporary.c_str(), (path.wstring() + L".bak").c_str(), 0, nullptr, nullptr) != FALSE;
				return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	void ResetCache()
	{
		std::lock_guard guard(lock);
		accounts.clear();
	}

	unsigned Request(uint64_t owner, const Json& request, Json& response)
	{
		std::lock_guard guard(lock);
		try
		{
			auto& record = load(owner);
			if (record.damaged)
			{
				response["settings"] = { { "error", "stored settings unreadable" } };
				return 503;
			}
			const auto before = record.account;
			auto reply = record.account.Handle(owner, request, GetTickCount64());
			if (reply.changed && !write(owner, record.account))
			{
				record.account = before;
				response["settings"] = { { "error", "settings persistence failed" } };
				return 503;
			}
			response["settings"] = std::move(reply.body);
			return reply.status;
		}
		catch (...)
		{
			return 503;
		}
	}
}
