#include "common/embedded_assets.h"

#include <filesystem>
#include <string>
namespace embedded_assets
{
	namespace
	{
		HMODULE g_module = nullptr;

		struct Asset
		{
			WORD id;
			std::string_view path;
			bool native;
		};

		constexpr Asset kAssets[] = {
#define EMBED_ASSET(id, path, native) {id, path, native},
#include "../../data/embedded_assets.inc"
#undef EMBED_ASSET
		};

		std::string_view Bytes(WORD id)
		{
			const auto resource = FindResourceW(g_module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
			if (!resource) return {};
			const auto size = SizeofResource(g_module, resource);
			const auto loaded = LoadResource(g_module, resource);
			const auto data = loaded ? LockResource(loaded) : nullptr;
			return data ? std::string_view(static_cast<const char*>(data), size) : std::string_view{};
		}

		bool Restore(const std::filesystem::path& path, std::string_view bytes)
		{
			const auto attributes = GetFileAttributesW(path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES) return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
			if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return false;
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error) return false;
			// Publish a complete file atomically. Concurrent client/server startup must
			// never expose partially written metadata to the native storage reader.
			auto temporary = path;
			temporary += L".proxy-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
			const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool wrote =
				WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
			CloseHandle(file);
			const bool moved = wrote && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
			const DWORD moveError = GetLastError();
			if (!moved) DeleteFileW(temporary.c_str());
			return moved || (wrote && (moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS));
		}
	}

	std::string_view Find(std::string_view path)
	{
		for (const auto& asset : kAssets)
			if (asset.path == path) return Bytes(asset.id);
		return {};
	}

	bool PrepareNativeMetadata(HMODULE module) noexcept
	{
		g_module = module;
		try
		{
			wchar_t path[MAX_PATH]{};
			const auto length = GetModuleFileNameW(module, path, MAX_PATH);
			if (!length || length >= MAX_PATH) return false;
			const auto root = std::filesystem::path(path).parent_path();
			for (const auto& asset : kAssets)
			{
				const auto bytes = Bytes(asset.id);
				if (bytes.empty() || (asset.native && !Restore(root / asset.path, bytes))) return false;
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
}
