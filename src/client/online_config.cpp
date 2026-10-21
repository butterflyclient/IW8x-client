#include "client/online_config.h"

#include "common/console.h"
#include "engine/image_identity.h"
#include "network/native_network.h"
#include "diagnostics/night/night_files.h"
#include "thirdparty/json.hpp"

#include <winhttp.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace online_config
{
	namespace
	{
		constexpr std::size_t size_limit = 64 * 1024;
		constexpr auto remote_path = L"/choppedshyt/iw8x-dll/refs/heads/main/mw120rproxy.online.ini";

		struct internet
		{
			HINTERNET handle{};
			~internet() { if (handle) WinHttpCloseHandle(handle); }
		};

		struct temporary_file
		{
			std::filesystem::path path;
			~temporary_file()
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}
		};

		std::wstring header(HINTERNET request, DWORD field)
		{
			std::array<wchar_t, 512> value{};
			DWORD bytes = static_cast<DWORD>(sizeof(value));
			if (!WinHttpQueryHeaders(request, field, nullptr, value.data(), &bytes, nullptr)) return {};
			return std::wstring(value.data());
		}

		void write(const std::filesystem::path& path, std::string_view bytes)
		{
			const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create temporary configuration");
			DWORD written{};
			const bool okay = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
				written == bytes.size() && FlushFileBuffers(file);
			CloseHandle(file);
			if (!okay) throw std::runtime_error("cannot flush temporary configuration");
		}

		bool valid(const std::filesystem::path& path)
		{
			const auto enabled = GetPrivateProfileIntW(L"online_services", L"enabled", 2, path.c_str());
			const auto port = GetPrivateProfileIntW(L"online_services", L"port", 0, path.c_str());
			const auto http = GetPrivateProfileIntW(L"online_services", L"http_port", 0, path.c_str());
			wchar_t host[256]{};
			GetPrivateProfileStringW(L"online_services", L"host", L"", host, std::size(host), path.c_str());
			return enabled <= 1 && port > 0 && port <= 65535 && http > 0 && http <= 65535 && host[0];
		}

		void synchronize_impl()
		{
			wchar_t path[MAX_PATH]{};
			if (!native_network::ConfigurationPath(path, std::size(path))) throw std::runtime_error("configuration path unavailable");
			const std::filesystem::path local = path;
			const auto cache = local.parent_path() / L"IW8x" / L"cache" / L"online-config.json";
			nlohmann::json previous = nlohmann::json::object();
			try
			{
				if (std::filesystem::file_size(cache) <= 4096)
				{
					std::ifstream stream(cache);
					stream >> previous;
					if (!previous.is_object() || !previous.contains("sha256") || !previous["sha256"].is_string() ||
						!previous.contains("etag") || !previous["etag"].is_string()) previous = nlohmann::json::object();
				}
			}
			catch (...) { previous = nlohmann::json::object(); }
			const auto known = previous.value("sha256", std::string{});
			auto etag_text = previous.value("etag", std::string{});
			for (const unsigned char byte : etag_text)
				if (byte < 32 || byte > 126)
				{
					etag_text.clear();
					break;
				}
			const std::wstring etag(etag_text.begin(), etag_text.end());
			internet session{ WinHttpOpen(L"IW8x/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0) };
			if (!session.handle || !WinHttpSetTimeouts(session.handle, 2000, 3000, 3000, 3000))
				throw std::runtime_error("HTTP session unavailable");
			internet connection{ WinHttpConnect(session.handle, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0) };
			if (!connection.handle) throw std::runtime_error("HTTP connection unavailable");
			internet request{ WinHttpOpenRequest(connection.handle, L"GET", remote_path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) };
			if (!request.handle) throw std::runtime_error("HTTP request unavailable");
			DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
			if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
				!WinHttpSetOption(request.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
				throw std::runtime_error("HTTP options unavailable");
			if (std::filesystem::is_regular_file(local) && known.size() == 64 && !etag.empty() && etag.size() < 256 &&
				etag.find_first_of(L"\r\n") == std::wstring::npos)
			{
				const auto conditional = L"If-None-Match: " + etag + L"\r\n";
				if (!WinHttpAddRequestHeaders(request.handle, conditional.c_str(), static_cast<DWORD>(conditional.size()), WINHTTP_ADDREQ_FLAG_ADD))
					throw std::runtime_error("HTTP conditional header unavailable");
			}
			if (!WinHttpSendRequest(request.handle, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.handle, nullptr))
				throw std::runtime_error("download failed or timed out");
			DWORD status{}, bytes = sizeof(status);
			if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr))
				throw std::runtime_error("HTTP status unavailable");
			if (status == 304)
			{
				LOG_INFO("OnlineConfig", "remote configuration unchanged; local configuration retained");
				return;
			}
			if (status != 200) throw std::runtime_error("HTTP " + std::to_string(status));
			DWORD expected{}, length_bytes = sizeof(expected);
			const bool has_length = WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &expected, &length_bytes, nullptr) != FALSE;
			if (has_length && expected > size_limit) throw std::runtime_error("configuration exceeds size limit");
			std::string body;
			std::array<char, 4096> buffer{};
			const auto deadline = GetTickCount64() + 10000;
			for (;;)
			{
				DWORD received{};
				if (GetTickCount64() > deadline || !WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &received))
					throw std::runtime_error("incomplete configuration download");
				if (!received) break;
				if (received > size_limit - body.size()) throw std::runtime_error("configuration exceeds size limit");
				body.append(buffer.data(), received);
			}
			if (body.empty() || (has_length && body.size() != expected) || body.find('\0') != std::string::npos)
				throw std::runtime_error("incomplete or invalid configuration");
			const auto suffix = L".download." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
			temporary_file download{ local.wstring() + suffix };
			write(download.path, body);
			if (!valid(download.path)) throw std::runtime_error("remote configuration validation failed");
			const auto digest = image_identity::Digest(download.path.c_str());
			if (digest.empty()) throw std::runtime_error("configuration digest unavailable");
			if (digest != known || !std::filesystem::is_regular_file(local))
			{
				if (!night_files::preserve_settings(local, download.path)) throw std::runtime_error("local port settings could not be retained");
				const auto installed_digest = image_identity::Digest(download.path.c_str());
				if (installed_digest.empty()) throw std::runtime_error("merged configuration digest unavailable");
				if (image_identity::Digest(local.c_str()) != installed_digest)
				{
					if (!MoveFileExW(download.path.c_str(), local.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
						throw std::runtime_error("atomic configuration replacement failed");
					WritePrivateProfileStringW(nullptr, nullptr, nullptr, local.c_str());
					LOG_INFO("OnlineConfig", "updated configuration atomically: sha256=%s", installed_digest.c_str());
				}
			}
			else LOG_INFO("OnlineConfig", "remote content unchanged; local configuration retained");
			try
			{
				std::filesystem::create_directories(cache.parent_path());
				temporary_file metadata{ cache.wstring() + suffix };
				const auto remote_etag = header(request.handle, WINHTTP_QUERY_ETAG);
				std::string etag_bytes;
				for (const auto byte : remote_etag)
				{
					if (byte < 32 || byte > 126)
					{
						etag_bytes.clear();
						break;
					}
					etag_bytes.push_back(static_cast<char>(byte));
				}
				write(metadata.path, nlohmann::json{ { "sha256", digest },
					{ "etag", etag_bytes } }.dump());
				if (!MoveFileExW(metadata.path.c_str(), cache.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
					throw std::runtime_error("configuration cache replacement failed");
			}
			catch (const std::exception& error)
			{
				LOG_WARN("OnlineConfig", "%s; configuration is valid, next startup will check full content", error.what());
			}
		}
	}

	void synchronize() noexcept
	{
		try { synchronize_impl(); }
		catch (const std::exception& error)
		{
			LOG_WARN("OnlineConfig", "%s; existing configuration retained", error.what());
		}
		catch (...) { LOG_WARN("OnlineConfig", "update check failed; existing configuration retained"); }
	}
}
