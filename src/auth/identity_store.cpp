#include "auth/identity_store.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <charconv>
#include <cwctype>
namespace offlineauth
{
	namespace
	{
		struct Record
		{
			char magic[8];
			uint32_t version;
			uint32_t server;
			uint64_t xuid;
			char name[36];
			uint32_t checksum;
		};

		static_assert(sizeof(Record) == 64 && offsetof(Record, checksum) == 60);

		uint32_t Checksum(const Record& record)
		{
			uint32_t hash = 2166136261u;
			const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
			for (size_t i = 0; i < offsetof(Record, checksum); ++i)
				hash = (hash ^ bytes[i]) * 16777619u;
			return hash;
		}

		class Lock
		{
			HANDLE mutex_ = nullptr;
			bool held_ = false;

		public:
			explicit Lock(const std::filesystem::path& path)
			{
				uint64_t hash = 14695981039346656037ull;
				for (wchar_t c : path.wstring())
					hash = (hash ^ uint64_t(towlower(c))) * 1099511628211ull;
				wchar_t name[80]{};
				swprintf_s(name, L"Local\\MW120Identity_%016llX", static_cast<unsigned long long>(hash));
				mutex_ = CreateMutexW(nullptr, FALSE, name);
				if (mutex_)
				{
					const DWORD wait = WaitForSingleObject(mutex_, 5000);
					held_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
				}
			}

			~Lock()
			{
				if (held_) ReleaseMutex(mutex_);
				if (mutex_) CloseHandle(mutex_);
			}

			explicit operator bool() const
			{
				return held_;
			}
		};
		enum class ReadResult
		{
			Missing,
			Invalid,
			Valid
		};

		ReadResult Read(const std::filesystem::path& path, bool server, Identity& identity)
		{
			HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
			if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? ReadResult::Missing : ReadResult::Invalid;
			Record record{};
			DWORD count = 0;
			LARGE_INTEGER size{};
			bool ok = GetFileSizeEx(file, &size) && size.QuadPart == sizeof(record) &&
				ReadFile(file, &record, sizeof(record), &count, nullptr) && count == sizeof(record);
			CloseHandle(file);
			ok = ok && memcmp(record.magic, "MW120ID", 8) == 0 && record.version == 1 && record.server == uint32_t(server) && record.xuid &&
				record.name[35] == 0 && record.checksum == Checksum(record) && IdentityStore::ValidName(record.name);
			if (!ok) return ReadResult::Invalid;
			identity = {};
			identity.xuid = record.xuid;
			strcpy_s(identity.name, record.name);
			return ReadResult::Valid;
		}

		bool Save(const std::filesystem::path& path, bool server, const Identity& identity, bool exists)
		{
			Record record{};
			memcpy(record.magic, "MW120ID", 8);
			record.version = 1;
			record.server = server;
			record.xuid = identity.xuid;
			strcpy_s(record.name, identity.name);
			record.checksum = Checksum(record);
			auto temporary = path;
			temporary += L".tmp";
			HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool ok =
				WriteFile(file, &record, sizeof(record), &written, nullptr) && written == sizeof(record) && FlushFileBuffers(file);
			CloseHandle(file);
			if (!ok) return false;
			if (!exists) return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
			auto backup = path;
			backup += L".bak";
			return ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr) != FALSE;
		}

		bool Import(const std::filesystem::path& path, bool server, Identity& identity)
		{
			char digits[32]{};
			// Wide API preserves non-ASCII installation paths; IDs themselves are decimal.
			wchar_t value[36]{};
			GetPrivateProfileStringW(L"local_identity", L"xuid", L"", value, 36, path.c_str());
			const size_t length = wcslen(value);
			if (!length || length >= sizeof(digits)) return false;
			for (size_t i = 0; i < length; ++i)
			{
				if (value[i] < L'0' || value[i] > L'9') return false;
				digits[i] = char(value[i]);
			}
			const auto parsed = std::from_chars(digits, digits + length, identity.xuid);
			if (parsed.ec != std::errc{} || parsed.ptr != digits + length || !identity.xuid) return false;
			GetPrivateProfileStringW(L"local_identity", L"name", server ? L"LocalServer" : L"Player 1", value, 36, path.c_str());
			for (size_t i = 0; value[i]; ++i)
				identity.name[i] = value[i] >= 32 && value[i] <= 126 ? char(value[i]) : '_';
			if (!IdentityStore::ValidName(identity.name)) strcpy_s(identity.name, server ? "LocalServer" : "Player 1");
			return true;
		}
	}

	bool IdentityStore::ValidName(std::string_view name)
	{
		if (name.empty() || name.size() > 35 || name.front() == ' ' || name.back() == ' ') return false;
		for (unsigned char c : name)
			if (c < 32 || c > 126 || c == '"' || c == ';' || c == '\\') return false;
		return true;
	}

	bool IdentityStore::Load(const std::filesystem::path& root, bool server, Identity& identity)
	{
		std::error_code error;
		auto directory = std::filesystem::absolute(root, error) / L"players";
		if (error) return false;
		std::filesystem::create_directories(directory, error);
		if (error) return false;
		path_ = directory / (server ? L"server_identity.bin" : L"identity.bin");
		server_ = server;
		Lock lock(path_);
		if (!lock) return false;
		const auto result = Read(path_, server_, identity);
		if (result == ReadResult::Valid) return true;
		// Keep invalid data intact. Silently replacing an ID would orphan stats.
		if (result == ReadResult::Invalid) return false;
		identity = {};
		auto legacy = root / (server ? L"mw120rproxy.server.identity.ini" : L"mw120rproxy.identity.ini");
		bool oldExists = std::filesystem::exists(legacy, error);
		if (error) return false;
		if (oldExists)
		{
			if (!Import(legacy, server, identity)) return false;
		}
		else
		{
			do
			{
				if (BCryptGenRandom(
						nullptr, reinterpret_cast<PUCHAR>(&identity.xuid), sizeof(identity.xuid), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
					return false;
			}
			while (!identity.xuid);
			strcpy_s(identity.name, server ? "LocalServer" : "Player 1");
		}
		return Save(path_, server_, identity, false);
	}

	bool IdentityStore::Rename(Identity& identity, std::string_view name)
	{
		if (path_.empty() || !ValidName(name)) return false;
		Lock lock(path_);
		Identity disk{};
		if (!lock || Read(path_, server_, disk) != ReadResult::Valid || disk.xuid != identity.xuid) return false;
		memset(disk.name, 0, sizeof(disk.name));
		memcpy(disk.name, name.data(), name.size());
		if (!Save(path_, server_, disk, true)) return false;
		// XUID is immutable after load; concurrent ID getters can keep reading it.
		memcpy(identity.name, disk.name, sizeof(identity.name));
		return true;
	}
}
