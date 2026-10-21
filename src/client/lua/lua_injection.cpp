#include "client/lua/lua_injection.h"

#include "engine/addresses.hpp"

#include "common/console.h"

#include "dedicated/dedicated.h"

#include "common/embedded_assets.h"

#include "network/native_network.h"
#include "client/frontend/menu_art1645.h"

#include "engine/safemem.h"

#include "thirdparty/json.hpp"

#include <algorithm>
#include <array>
#include <atomic>

#include <bcrypt.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace lua_injection
{
	namespace
	{
		constexpr size_t kFileLimit = 1024 * 1024;
		constexpr size_t kCombinedLimit = 4 * kFileLimit;
		constexpr std::string_view kStartup = "ui/luistartup.lua";

		struct Binding
		{
			uintptr_t rva;
			std::array<uint8_t, 16> bytes;
		};

		// Exact bytes/ABI from replay_lua_compile_abi.json and
		// replay_lua_reader_and_base_engine.json, using the 1.20.4 Replay executable.
		constexpr Binding kLoadFile{ 0x19baee0,
			{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 } };
		constexpr Binding kLoad{ 0x208a6f0,
			{ 0x48, 0x83, 0xec, 0x38, 0x48, 0xc7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x0e, 0x00 } };
		constexpr Binding kGetTop{ 0x20838b0,
			{ 0x48, 0x8b, 0x41, 0x28, 0x48, 0x2b, 0x41, 0x20, 0x48, 0xc1, 0xf8, 0x03, 0xc3, 0xcc, 0xcc, 0xcc } };
		constexpr Binding kSetTop{ 0x2084790,
			{ 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x85, 0xd2, 0x78, 0x7a, 0x4c, 0x8b, 0x41 } };
		constexpr Binding kRemove{ 0x2084420,
			{ 0x4c, 0x8b, 0xc1, 0x85, 0xd2, 0x7e, 0x23, 0x8d, 0x42, 0xff, 0x48, 0x63, 0xd0, 0x48, 0x8b, 0x41 } };
		constexpr Binding kToString{ 0x2084950,
			{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x49 } };

		using LoadFileFn = int(__fastcall*)(void*, const char*);
		using ReaderFn = const char*(__fastcall*)(void*, void*, size_t*);
		using LoadFn = int(__fastcall*)(void*, ReaderFn, void*, const char*);
		using GetTopFn = int(__fastcall*)(void*);
		using StackFn = void(__fastcall*)(void*, int);
		using ToStringFn = const char*(__fastcall*)(void*, int, size_t*);
		std::atomic<LoadFileFn> g_original{ nullptr };
		std::atomic<bool> g_startupCompiled{ false };
		std::atomic<StartupCallback> g_startupCallback{ nullptr };
		uintptr_t g_base = 0;
		std::filesystem::path g_overrideRoot;
		std::filesystem::path g_menuRoot;
		std::unordered_map<std::string, std::string> g_menuSources;
		bool g_menu1645Enabled = false;

		// The original 1.64.5 startup/requires modules must never replace Replay's
		// profile, matchmaking, or Client Settings startup sequence.  Only builders
		// for the selected title and MP menu are in this opt-in allowlist.
		constexpr std::array<std::string_view, 31> kMenu1645Modules{
			"ui/frontend/cp/cpmainmenu.lua",
			"ui/frontend/cp/cpmainmenubuttons.lua",
			"ui/frontend/mainmenu.lua",
			"ui/frontend/mainmenublade.lua",
			"ui/frontend/mainmenubladedisableoverlay.lua",
			"ui/frontend/mainmenubladelayout.lua",
			"ui/frontend/mainmenubladelist.lua",
			"ui/frontend/mainmenubladenewbanner.lua",
			"ui/frontend/mainmenubladengoverlay.lua",
			"ui/frontend/mainmenubladepromptbutton.lua",
			"ui/frontend/mainmenubladestoreoverlay.lua",
			"ui/frontend/mainmenubladeupselloverlay.lua",
			"ui/frontend/mainmenubutton.lua",
			"ui/frontend/mainmenugrid.lua",
			"ui/frontend/mainmenulogo.lua",
			"ui/frontend/mainmenuoffline.lua",
			"ui/frontend/mp/brmainmenu.lua",
			"ui/frontend/mp/mpmainmenu.lua",
			"ui/frontend/mp/mpplaymenu.lua",
			"ui/frontend/mp/mpplaymenubuttons.lua",
			"ui/frontend/mpoperatormenu.lua",
			"ui/frontend/mpoperatormenubuttons.lua",
			"ui/frontend/mptabsmenu.lua",
			"ui/frontend/mpweaponmenu.lua",
			"ui/frontend/mpweaponmenubuttons.lua",
			"ui/frontend/wzmainmenu.lua",
			"ui/frontend/wzmainmenublade.lua",
			"ui/frontend/wzmainmenubladelayout.lua",
			"ui/utils/mainmenuutils.lua",
			"ui/utils/wzmainmenuutils.lua",
			"ui/widgets/mp/blueprintblitzmainmenuitem.lua",
		};

		template <class T>
		T Function(const Binding& binding)
		{
			return reinterpret_cast<T>(g_base + binding.rva);
		}

		bool NormalizeFilename(const char* filename, std::string& normalized)
		{
			char name[1024]{};
			const size_t length = safemem::ReadString(filename, name, sizeof(name));
			char terminator = 1;
			if (!length || length == sizeof(name) - 1 || !safemem::ReadBytes(filename + length, &terminator, 1) || terminator) return false;
			normalized.assign(name, length);
			for (char& c : normalized)
			{
				if (c == '\\')
					c = '/';
				else if (c >= 'A' && c <= 'Z')
					c = char(c - 'A' + 'a');
				if (static_cast<unsigned char>(c) < 32 || c == ':') return false;
			}
			if (normalized.front() == '/') return false;
			size_t start = 0;
			while (start < normalized.size())
			{
				const size_t end = normalized.find('/', start);
				const auto part = std::string_view(normalized).substr(start, end - start);
				if (part.empty() || part == "." || part == "..") return false;
				if (end == std::string::npos) break;
				start = end + 1;
			}
			return normalized.back() != '/';
		}

		enum class ReadResult
		{
			MissingOrEmpty,
			Loaded,
			Error
		};

		ReadResult ReadSource(const std::filesystem::path& path, std::string& source)
		{
			std::error_code error;
			const bool exists = std::filesystem::exists(path, error);
			if (!error && !exists) return ReadResult::MissingOrEmpty;
			if (error || !std::filesystem::is_regular_file(path, error) || error)
			{
				LOG_ERR("Lua", "cannot read source file: %ls", path.c_str());
				return ReadResult::Error;
			}
			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			const auto end = stream.tellg();
			if (!stream || end < 0 || static_cast<uint64_t>(end) > kFileLimit)
			{
				LOG_ERR("Lua", "source unreadable or exceeds 1 MiB: %ls", path.c_str());
				return ReadResult::Error;
			}
			if (!end) return ReadResult::MissingOrEmpty;
			source.resize(static_cast<size_t>(end));
			stream.seekg(0);
			if (!stream.read(source.data(), static_cast<std::streamsize>(source.size())) ||
				stream.peek() != std::ifstream::traits_type::eof())
			{
				LOG_ERR("Lua", "source changed or read failed: %ls", path.c_str());
				return ReadResult::Error;
			}
			return ReadResult::Loaded;
		}

		std::string Digest(std::string_view data)
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
			std::array<unsigned char, 32> hash{};
			const auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
				static_cast<ULONG>(data.size()), hash.data(), static_cast<ULONG>(hash.size()));
			BCryptCloseAlgorithmProvider(algorithm, 0);
			if (status < 0) return {};
			constexpr char digits[] = "0123456789abcdef";
			std::string result;
			result.reserve(64);
			for (unsigned char byte : hash)
			{
				result += digits[byte >> 4];
				result += digits[byte & 15];
			}
			return result;
		}

		void InitializeMenu1645(const std::filesystem::path& gameDirectory)
		{
			wchar_t config[MAX_PATH]{}, selected[32]{};
			if (!native_network::ConfigurationPath(config, MAX_PATH)) return;
			GetPrivateProfileStringW(L"ui", L"main_menu_ui", L"1.20", selected, 32, config);
			if (_wcsicmp(selected, L"1.64.5") != 0) return;
			const bool isolatedTest = GetPrivateProfileIntW(L"ui", L"main_menu_ui_test", 0, config) == 1;

			g_menuRoot = gameDirectory / L"magma" / L"ui" / L"1.64.5";
			std::string text;
			if (ReadSource(g_menuRoot / L"manifest.json", text) != ReadResult::Loaded)
			{
				LOG_ERR("Menu1645", "1.64.5 selected, but verified menu package is missing");
				return;
			}
			try
			{
				const auto manifest = nlohmann::json::parse(text);
				if (!manifest.is_object() || manifest.value("schema", 0) != 1 || manifest.value("source_build", "") != "1.64.5.13113888" ||
					manifest.value("target_build", "") != "1.20.4.7623265" ||
					manifest.value("source_code_post_gfx_sha256", "") !=
						"020f3f674498c4cca03c197d33c90cb06d0b29cdc3c7ce0fcbed35359cbb3d4a" ||
					!(manifest.value("validated", false) || (isolatedTest && manifest.value("test_only", false))) ||
					!manifest.contains("modules") || !manifest.at("modules").is_array() ||
					manifest.at("modules").size() < kMenu1645Modules.size() || manifest.at("modules").size() > 128)
					throw std::runtime_error("package receipt is incomplete or not validated");

				std::unordered_set<std::string> expected;
				for (auto name : kMenu1645Modules)
					expected.emplace(name);
				for (auto name :
					{ "ui/uieditor/conditions.lua", "ui/uieditor/colors.lua", "ui/uieditor/swatches.lua", "ui/uieditor/swatchthemes.lua",
						"ui/uieditor/soundsets.lua", "ui/utils/timeutils.lua", "ui/utils/themeutil.lua", "ui/utils/giftingutils.lua",
						"ui/utils/layoututils.lua", "ui/utils/progressionutils.lua", "ui/utils/storeutils.lua", "ui/utils/trialsutils.lua",
						"ui/utils/lootutils.lua", "ui/utils/operatorutils.lua", "ui/utils/weaponutils.lua",
						"ui/utils/dynamicattachmentutils.lua", "ui/utils/battlepassutils.lua", "ui/utils/randomutils.lua" })
					expected.emplace(name);
				std::unordered_map<std::string, std::string> loaded;
				for (const auto& record : manifest.at("modules"))
				{
					if (!record.is_object() || !record.contains("name") || !record.contains("sha256") || !record.at("name").is_string() ||
						!record.at("sha256").is_string())
						throw std::runtime_error("malformed module receipt");
					const auto name = record.at("name").get<std::string>();
					const auto hash = record.at("sha256").get<std::string>();
					std::string normalized;
					if (!NormalizeFilename(name.c_str(), normalized) || normalized != name || !name.ends_with(".lua") ||
						hash.size() != 64 ||
						!(name.starts_with("ui/frontend/") || name.starts_with("ui/utils/") || name.starts_with("ui/widgets/") ||
							name == "ui/uieditor/conditions.lua" || name == "ui/uieditor/colors.lua" ||
							name == "ui/uieditor/swatches.lua" || name == "ui/uieditor/swatchthemes.lua" ||
							name == "ui/uieditor/soundsets.lua") ||
						loaded.contains(name))
						throw std::runtime_error("unsafe or duplicate menu module");
					expected.erase(name);
					auto relative = std::filesystem::path(name);
					relative.replace_extension(L".bin");
					std::string bytecode;
					if (ReadSource(g_menuRoot / L"lua" / relative, bytecode) != ReadResult::Loaded || bytecode.size() < 5 ||
						std::memcmp(bytecode.data(), "\x1bLJ\x02\x0a", 5) || Digest(bytecode) != hash)
						throw std::runtime_error("menu bytecode is missing or differs from receipt: " + name);
					loaded.emplace(name, std::move(bytecode));
				}
				if (!expected.empty()) throw std::runtime_error("menu modules missing from receipt");
				g_menuSources = std::move(loaded);
				g_menu1645Enabled = true;
				LOG_INFO("Menu1645", "original 1.64.5 title/MP bytecode verified: %zu modules test=%d", g_menuSources.size(),
					isolatedTest && !manifest.value("validated", false));
			}
			catch (const std::exception& error)
			{
				LOG_ERR("Menu1645", "using stock 1.20 UI: %s", error.what());
			}
		}

		bool CollectSource(const std::string& filename, std::string& source)
		{
			auto result = ReadSource(g_overrideRoot / filename, source);
			if (result == ReadResult::MissingOrEmpty)
			{
				const auto bytes = embedded_assets::Find("data/lua/" + filename);
				if (!bytes.empty())
				{
					source.assign(bytes);
					result = ReadResult::Loaded;
				}
			}
			if (result != ReadResult::Loaded) return false;
			if (filename != kStartup) return true;

			// New embedded-only modules cannot pass native require's asset lookup.
			// Keep the generated, verified BR image-name table local to this chunk.
			const auto escapeArt = embedded_assets::Find("data/lua/ui/escapehudimages.lua");
			if (escapeArt.empty()) return false;
			source = "local escapeHudImages = (function()\n" + std::string(escapeArt) + "\nend)()\n" + source;

			// Native require checks its asset table before calling LoadFile, so a new
			// embedded-only module cannot be required by name. Append this scoped
			// addon after startup has registered the stock frontend widgets instead.
			for (const auto* path : { "data/lua/ui/replay/store.lua", "data/lua/ui/replay/match_ui.lua",
					 "data/lua/ui/replay/progression.lua", "data/lua/ui/replay/loadouts.lua", "data/lua/ui/replay/menu1645.lua",
					 "data/lua/ui/replay/frontend.lua", "data/lua/ui/replay/custom_mods.lua", "data/lua/ui/replay/client_settings.lua",
					 "data/lua/ui/replay/after_action.lua", "data/lua/ui/replay/weapon_pilots.lua" })
			{
				const auto addon = embedded_assets::Find(path);
				if (!addon.empty())
				{
					source += "\n;(function()\n";
					source.append(addon);
					source += "\nend)()\n";
				}
			}

			std::error_code error;
			if (!std::filesystem::exists(g_overrideRoot, error) && !error) return true;
			std::vector<std::filesystem::path> addons;
			std::filesystem::directory_iterator iterator(g_overrideRoot, error), end;
			for (; !error && iterator != end; iterator.increment(error))
			{
				const auto& entry = *iterator;
				if (!entry.is_regular_file(error)) continue;
				auto extension = entry.path().extension().wstring();
				for (auto& c : extension)
					if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
				if (extension == L".lua") addons.push_back(entry.path());
			}
			if (error)
			{
				LOG_ERR("Lua", "cannot enumerate startup addons: %ls", g_overrideRoot.c_str());
				return false;
			}
			std::sort(addons.begin(), addons.end(),
				[](const auto& a, const auto& b)
				{
					return a.filename().native() < b.filename().native();
				});
			for (const auto& path : addons)
			{
				std::string addon;
				const auto read = ReadSource(path, addon);
				if (read == ReadResult::Error) return false;
				if (read != ReadResult::Loaded) continue;
				if (source.size() + 1 + addon.size() > kCombinedLimit)
				{
					LOG_ERR("Lua", "startup source and addons exceed 4 MiB; keeping native chunk");
					return false;
				}
				source += '\n';
				source += addon;
			}
			return true;
		}

		struct SourceReader
		{
			const std::string* source;
			bool supplied = false;
		};

		const char* __fastcall ReadChunk(void*, void* userdata, size_t* length)
		{
			auto& reader = *static_cast<SourceReader*>(userdata);
			if (reader.supplied)
			{
				*length = 0;
				return nullptr;
			}
			reader.supplied = true;
			*length = reader.source->size();
			return reader.source->data();
		}

		int __fastcall Menu1645Compile(void* state)
		{
			if (!g_menu1645Enabled) return 0;
			const int before = Function<GetTopFn>(kGetTop)(state);
			if (before != 1) return 0;
			size_t length = 0;
			const auto* requested = Function<ToStringFn>(kToString)(state, 1, &length);
			std::string name;
			if (!requested || length >= 1024 || !NormalizeFilename(requested, name) || name.size() != length) return 0;
			const auto it = g_menuSources.find(name);
			if (it == g_menuSources.end()) return 0;
			SourceReader reader{ &it->second };
			const std::string chunkName = "@magma/ui/1.64.5/" + name;
			const int result = Function<LoadFn>(kLoad)(state, &ReadChunk, &reader, chunkName.c_str());
			const int after = Function<GetTopFn>(kGetTop)(state);
			if (result != 0 || after != before + 1)
			{
				Function<StackFn>(kSetTop)(state, before);
				LOG_ERR("Menu1645", "cannot compile original Lua module %s (status=%d)", name.c_str(), result);
				return 0;
			}
			return 1;
		}

		struct LuaRegistration
		{
			const char* name;
			int(__fastcall* function)(void*);
		};

		void RegisterMenu1645Impl(void* state)
		{
			if (!g_menu1645Enabled || !menu_art1645::Loaded())
			{
				if (g_menu1645Enabled) LOG_ERR("Menu1645", "original menu skipped because image zone is not loaded");
				return;
			}
			static const LuaRegistration entries[] = { { "Compile", Menu1645Compile }, { nullptr, nullptr } };
			const int top = Function<GetTopFn>(kGetTop)(state);
			reinterpret_cast<void(__fastcall*)(void*, const char*, const LuaRegistration*)>(g_base + 0x20885c0)(
				state, "ReplayMenu1645", entries);
			Function<StackFn>(kSetTop)(state, top);
			LOG_INFO("Menu1645", "original menu module compiler registered");
		}

		int __fastcall LoadFile(void* state, const char* filename)
		{
			const auto original = g_original.load(std::memory_order_acquire);
			const int before = Function<GetTopFn>(kGetTop)(state);
			const int stockResult = original(state, filename);
			const int stockTop = Function<GetTopFn>(kGetTop)(state);
			// Native loadfile returns one compiled function (or one error object).
			// Do not try an override if that verified contract no longer holds.
			if (before < 0 || before == std::numeric_limits<int>::max() || stockTop != before + 1)
			{
				LOG_ERR("Lua", "unexpected native loadfile stack: before=%d after=%d", before, stockTop);
				return stockResult;
			}

			std::string normalized, source, chunkName;
			try
			{
				if (!NormalizeFilename(filename, normalized)) return stockResult;
				if (normalized == kStartup)
				{
					g_startupCompiled.store(false, std::memory_order_release);
					if (const auto callback = g_startupCallback.load(std::memory_order_acquire))
					{
						callback(state);
						if (Function<GetTopFn>(kGetTop)(state) != stockTop)
						{
							Function<StackFn>(kSetTop)(state, stockTop);
							LOG_ERR("Lua", "startup callback changed stack; keeping native chunk");
							return stockResult;
						}
					}
				}
				if (!CollectSource(normalized, source)) return stockResult;
				chunkName = "@mw120rproxy/" + normalized;
			}
			catch (const std::exception& error)
			{
				LOG_ERR("Lua", "source collection failed; keeping native chunk: %s", error.what());
				return stockResult;
			}
			catch (...)
			{
				LOG_ERR("Lua", "source collection failed; keeping native chunk");
				return stockResult;
			}

			SourceReader reader{ &source };
			const int result = Function<LoadFn>(kLoad)(state, &ReadChunk, &reader, chunkName.c_str());
			const int compiledTop = Function<GetTopFn>(kGetTop)(state);
			if (result != 0 || compiledTop != stockTop + 1)
			{
				char error[1024]{};
				if (compiledTop > stockTop) safemem::ReadString(Function<ToStringFn>(kToString)(state, -1, nullptr), error, sizeof(error));
				// lua_settop is 0x2084790. 0x2084420 is lua_remove, despite an older
				// dormant autostart binding incorrectly labeling it as settop.
				Function<StackFn>(kSetTop)(state, stockTop);
				LOG_ERR("Lua", "custom compile failed for %s (status=%d stack=%d): %s; keeping native chunk", normalized.c_str(), result,
					compiledTop, error[0] ? error : "unexpected compiler stack");
				return stockResult;
			}

			// Stack: [..., native function/error, custom function]. Remove only the
			// native result; the caller executes the returned custom function later.
			// Calling pcall here would execute at the wrong point in startup.
			Function<StackFn>(kRemove)(state, stockTop);
			if (normalized == kStartup) g_startupCompiled.store(true, std::memory_order_release);
			LOG_INFO("Lua", "custom chunk ready: %s (%zu bytes, native status=%d)", normalized.c_str(), source.size(), stockResult);
			return 0;
		}
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (dedicated::Enabled() || g_original.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = moduleBase;
		for (const auto* binding : { &kLoadFile, &kLoad, &kGetTop, &kSetTop, &kRemove, &kToString })
		{
			std::array<uint8_t, 16> actual{};
			const auto* address = reinterpret_cast<const void*>(g_base + binding->rva);
			if (!safemem::RegionExecutable(address) || !safemem::ReadBytes(address, actual.data(), actual.size()) ||
				actual != binding->bytes)
			{
				LOG_ERR("Lua", "Replay Lua binding mismatch at RVA 0x%llX", static_cast<unsigned long long>(binding->rva));
				return hook::Status::NotReady;
			}
		}
		HMODULE self = nullptr;
		wchar_t path[MAX_PATH]{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&Install), &self))
			return hook::Status::Failed;
		const auto length = GetModuleFileNameW(self, path, MAX_PATH);
		if (!length || length >= MAX_PATH) return hook::Status::Failed;
		const auto directory = std::filesystem::path(path).parent_path();
		g_overrideRoot = directory / L"IW8x" / L".lua";
		InitializeMenu1645(directory);
		const auto status = hook::Install(
			reinterpret_cast<void*>(g_base + kLoadFile.rva), &LoadFile, kLoadFile.bytes.data(), kLoadFile.bytes.size(), g_original);
		if (status == hook::Status::Installed)
			LOG_INFO("Lua", "Replay custom loader installed; overrides=%ls bundled=DLL resources", g_overrideRoot.c_str());
		return status;
	}

	bool StartupCompiled()
	{
		return g_startupCompiled.load(std::memory_order_acquire);
	}

	void SetStartupCallback(StartupCallback callback)
	{
		g_startupCallback.store(callback, std::memory_order_release);
	}

	void RegisterMenu1645(void* state)
	{
		RegisterMenu1645Impl(state);
	}
}
