// Exercise the production hook around a fake native DB loader. No game process.
#include "../gsc.cpp"

#include <array>
#include <cstdarg>
#include <iostream>
#include <stdexcept>
namespace
{
	extern "C" void* iw8x_native_error;
	extern "C" void iw8x_error_trace(int, const char*, ...);
	std::uintptr_t image_base = 0;
	constexpr std::uint32_t image_size = 0x17400000;

	struct TestBlock6
	{
		std::array<char, 16> compressed;
		std::array<std::uint8_t, 16> bytecode;
		std::array<std::uint8_t, 224> tail;
	} block6;

	auto& compressed = block6.compressed;
	auto& bytecode = block6.bytecode;
	gsc::ScriptFile asset{};
	gsc::ScriptFile* registered_asset = &asset;
	unsigned native_calls = 0;
	unsigned native_links = 0, native_releases = 0, native_restarts = 0;
	unsigned map_shutdown_events{}, script_shutdown_events{};
	bool gameplay_fixture = true;
	bool shutdown_clears = true, last_full_clear = false;
	bool saw_pending{}, saw_reload{}, saw_savegame{};
	bool throw_during_load = false;
	bool program_range_enabled = true;
	std::vector<std::uint8_t> linked_input;
	char stock_buffer = 'S';
	std::uint8_t stock_code = 0xCC;
	std::size_t stream_position = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	bool error_arguments_preserved{};

	void FakeNativeError(int kind, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
		bool ok = kind == 1 && std::strcmp(format, "boundary") == 0;
		for (std::uint64_t i = 0; i < 12; ++i)
			ok = (va_arg(args, std::uint64_t) == 0x1234567800000000ull + i) && ok;
		ok = va_arg(args, double) == 3.25 && ok;
		va_end(args);
		error_arguments_preserved = ok && GetLastError() == 421;
	}

	void FakeFloatError(int kind, const char* format, double a, double b)
	{
		error_arguments_preserved = kind == 2 && std::strcmp(format, "float") == 0 && a == 1.25 && b == 9.5;
	}

	void ErrorBoundaryChecks()
	{
		iw8x_native_error = reinterpret_cast<void*>(&FakeNativeError);
		SetLastError(421);
		iw8x_error_trace(1, "boundary", 0x1234567800000000ull, 0x1234567800000001ull, 0x1234567800000002ull, 0x1234567800000003ull,
			0x1234567800000004ull, 0x1234567800000005ull, 0x1234567800000006ull, 0x1234567800000007ull, 0x1234567800000008ull,
			0x1234567800000009ull, 0x123456780000000Aull, 0x123456780000000Bull, 3.25);
		Check(error_arguments_preserved, "error trace changed variadic register/stack arguments or LastError");
		iw8x_native_error = reinterpret_cast<void*>(&FakeFloatError);
		reinterpret_cast<void (*)(int, const char*, double, double)>(&iw8x_error_trace)(2, "float", 1.25, 9.5);
		Check(error_arguments_preserved, "error trace changed floating-point argument registers");
		iw8x_native_error = nullptr;
	}

	using BatchFunction = int (*)();
	BatchFunction batch_original{};

	int BatchDetour()
	{
		return batch_original() + 100;
	}

	void BatchRollbackChecks()
	{
		auto* bytes = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		Check(bytes != nullptr, "batch hook fixture allocation");
		std::memset(bytes, 0x90, 4096);
		bytes[0] = 0xB8;
		bytes[1] = 10;
		bytes[2] = bytes[3] = bytes[4] = 0;
		bytes[5] = 0xC3;
		FlushInstructionCache(GetCurrentProcess(), bytes, 4096);
		void* original{};
		Check(hook::detail::Prepare(bytes, reinterpret_cast<void*>(&BatchDetour), bytes, 24, &original) == hook::Status::Installed,
			"batch hook fixture preparation");
		batch_original = reinterpret_cast<BatchFunction>(original);
		const std::array<void*, 2> invalid{ bytes, bytes + 64 }; // Second hook was never prepared.
		Check(!hook::detail::EnableBatch(invalid), "partially queued installation falsely succeeded");
		Check(reinterpret_cast<BatchFunction>(bytes)() == 10 && batch_original() == 10,
			"failed hook batch left a callback reachable or destroyed its original");
		const std::array<void*, 1> retry{ bytes };
		Check(hook::detail::EnableBatch(retry) && reinterpret_cast<BatchFunction>(bytes)() == 110,
			"prepared hook batch could not be retried safely");
		Check(MH_DisableHook(bytes) == MH_OK && MH_RemoveHook(bytes) == MH_OK, "batch fixture cleanup");
		VirtualFree(bytes, 0, MEM_RELEASE);
		batch_original = nullptr;
	}

	std::uint8_t* FakeProgramRange(std::uint32_t* size)
	{
		if (!program_range_enabled)
		{
			*size = 0;
			return nullptr;
		}
		*size = static_cast<std::uint32_t>(sizeof(block6));
		return reinterpret_cast<std::uint8_t*>(&block6);
	}

	void CaptureTail()
	{
		std::uint64_t sizes[8]{};
		sizes[6] = compressed.size() + bytecode.size();
		gsc::ZoneAllocation allocations[4]{};
		allocations[1].allocation = reinterpret_cast<std::uint8_t*>(&block6);
		allocations[1].size = sizeof(block6);
		gsc::BlockLayout layouts[8]{};
		layouts[6].address = reinterpret_cast<std::uintptr_t>(&block6);
		layouts[6].size = sizes[6];
		gsc::CaptureBlock6Tail(sizes[6], allocations, layouts);
	}

	void NativeLoad(int start)
	{
		Check(start == 1, "DBStreamStart was changed");
		++native_calls;
		stream_position += 8 + 8;
		CaptureTail();
		compressed.fill(stock_buffer);
		bytecode.fill(stock_code);
		asset = {};
		asset.name_ = "sample.gsc";
		asset.compressed_len_ = 8;
		asset.len_ = 24;
		asset.bytecode_len_ = 8;
		asset.buffer_ = compressed.data();
		asset.bytecode_ = bytecode.data();
		*game::at<gsc::ScriptFile*>(game::addresses::gsc::kVarScriptFileRva) = &asset;
	}

	std::vector<std::uint8_t> File(std::int32_t compressed_size = 3, std::int32_t code_size = 4)
	{
		std::vector<std::uint8_t> bytes{ 'x', 0 };
		const std::int32_t sizes[]{ compressed_size, 17, code_size };
		const auto* data = reinterpret_cast<const std::uint8_t*>(sizes);
		bytes.insert(bytes.end(), data, data + sizeof(sizes));
		if (compressed_size > 0 && compressed_size < 1000) bytes.insert(bytes.end(), compressed_size, 0x41);
		if (code_size > 0 && code_size < 1000) bytes.insert(bytes.end(), code_size, 0x42);
		return bytes;
	}

	void Write(const std::vector<std::uint8_t>& bytes)
	{
		std::ofstream out(gsc::override_root / "sample.gscbin", std::ios::binary | std::ios::trunc);
		Check(bool(out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())), "write fixture");
	}

	void Stock()
	{
		Check(asset.compressed_len_ == 8 && asset.len_ == 24 && asset.bytecode_len_ == 8, "native lengths changed");
		Check(compressed[0] == 'S' && bytecode[0] == 0xCC, "native payload changed");
	}

	template <class T>
	T& Context(std::size_t offset)
	{
		return *game::at<T>(game::addresses::gsc::kServerContextRva + offset);
	}

	void ClearContext()
	{
		std::memset(game::at(game::addresses::gsc::kServerContextRva), 0, 0xC000);
	}

	void NativeShutdown(bool full)
	{
		last_full_clear = full;
		if (full && shutdown_clears) ClearContext();
	}

	void NativeLink(void* context, gsc::ScriptFile* script)
	{
		Check(context == game::at(game::addresses::gsc::kServerContextRva), "wrong script context");
		++native_links;
		linked_input.assign(script->bytecode_, script->bytecode_ + script->bytecode_len_);
		// Model the real linker changing operands in the engine-owned bytecode.
		std::memset(script->bytecode_, 0xEE, script->bytecode_len_);
	}

	void NativeLoadScripts(const void*)
	{
		using namespace game::addresses::gsc;
		if (throw_during_load) throw std::runtime_error("simulated native load failure");
		Context<std::uint8_t>(kLoadingScriptsOffset) = 1;
		Context<std::uint32_t>(kVMFrameCountOffset) = 0;
		Context<std::uintptr_t>(kProgramBufferOffset) = reinterpret_cast<std::uintptr_t>(bytecode.data());
		Context<std::uint32_t>(kProgramLengthOffset) = 16;
		gsc::LinkScriptFile(game::at(kServerContextRva), registered_asset);
		Context<std::uint8_t>(kLoadingScriptsOffset) = 0;
		Context<std::uint32_t>(kVMFrameCountOffset) = 1; // map now running
	}

	void NativeRelease(void* header, unsigned type, unsigned)
	{
		if (type == game::addresses::gsc::kScriptFileAssetType)
		{
			const auto* script = static_cast<const gsc::ScriptFile*>(header);
			Check(script->buffer_ == compressed.data() && script->bytecode_ == bytecode.data(), "DB release observed plugin tail pointers");
			Check(script->compressed_len_ == 8 && script->len_ == 24 && script->bytecode_len_ == 8,
				"DB release observed replacement lengths");
		}
		++native_releases;
	}

	int NativeRestart()
	{
		using namespace game::addresses::gsc;
		++native_restarts;
		saw_pending = *game::at<std::uint8_t>(kRestartPendingRva) != 0;
		saw_reload = *game::at<std::uint8_t>(kRestartLoadScriptsRva) != 0;
		saw_savegame = *game::at<std::uint8_t>(kRestartSavegameRva) != 0;
		return saw_pending ? 2 : 0; // preserve the complete EAX result
	}

	void FullReload()
	{
		gsc::ShutdownScripts(true);
		Check(last_full_clear, "native full shutdown was not called");
		gsc::LoadScripts(nullptr);
		Check(gsc::link_epoch == 0, "load scope leaked");
	}

	void ReloadChecks()
	{
		using namespace game::addresses::gsc;
		gsc::original_load_scripts = NativeLoadScripts;
		gsc::original_shutdown = NativeShutdown;
		gsc::original_link_file = NativeLink;
		gsc::original_release_asset = NativeRelease;
		gsc::original_process_restart = NativeRestart;
		Check(VirtualAlloc(game::at(kServerContextRva & ~std::uintptr_t(0xFFF)), 0xD000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
			"commit synthetic script context");
		Check(VirtualAlloc(game::at(kRestartPendingRva & ~std::uintptr_t(0xFFF)), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
			"commit synthetic restart flags");
		namespace native = game::addresses::gsc_api;
		Check(VirtualAlloc(game::at(native::persistent_globals & ~std::uintptr_t(0xFFF)), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
			"commit synthetic persistent globals slot");
		static std::array<uint8_t, 0x80> globals{};
		*game::at<uint8_t*>(native::persistent_globals) = globals.data();
		gsc::ShutdownScripts(true);
		Check(map_shutdown_events == 1 && script_shutdown_events == 1, "gameplay teardown events missing");
		globals[native::persistent_frontend] = 1;
		gsc::ShutdownScripts(true);
		Check(map_shutdown_events == 1 && script_shutdown_events == 1, "frontend shutdown emitted gameplay teardown events");
		globals[native::persistent_frontend] = 0;
		ClearContext();
		Write(File());
		gsc::LoadScriptFile(1);
		auto registered = asset;
		registered_asset = &registered;
		asset = {}; // DB registration copies the transient streaming header
		gsc::LoadScripts(nullptr);
		asset = registered;
		registered_asset = &asset;
		Check(linked_input == std::vector<std::uint8_t>(4, 0x42), "initial native link missed override");
		Check(bytecode[0] == 0xEE, "fake linker did not relocate bytecode");
		auto changed = File(6, 7);
		changed.back() = 0x55;
		Write(changed);
		gsc::ReapplyCustomScript("sample.gsc");
		gsc::reload_script_file(&asset);
		Check(bytecode[6] == stock_code, "public reapply wrote outside old bytecode");
		const auto streamed = native_calls;
		FullReload();
		Check(native_calls == streamed, "resident test unexpectedly restreamed asset");
		Check(linked_input.size() == 7 && linked_input.back() == 0x55 && linked_input.front() == 0x42,
			"resident reload did not reread new bytes or restore original capacity");
		Check(asset.compressed_len_ == 6 && asset.bytecode_len_ == 7, "resident lengths incorrect");

		std::filesystem::remove(gsc::override_root / "sample.gscbin");
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(8, stock_code) && compressed[0] == stock_buffer,
			"deleted override did not restore pristine stock bytes");
		Check(asset.len_ == 24 && asset.compressed_len_ == 8, "stock lengths not restored");
		Write(File());
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(4, 0x42), "new resident override not discovered");
		Write(File(9, 4));
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(4, 0x42), "resident growth did not use native block-6 tail");
		Check(asset.buffer_ != compressed.data() && asset.bytecode_ != bytecode.data(),
			"resident growth did not repoint both ScriptFile payloads");

		Write(File());
		shutdown_clears = false;
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(4, 0xEE), "uncleared full shutdown authorized writes");
		shutdown_clears = true;
		gsc::ShutdownScripts(false);
		Check(!last_full_clear, "partial shutdown was forcibly promoted");
		ClearContext(); // zero fields alone must not authorize subsequent reload
		gsc::LoadScripts(nullptr);
		Check(linked_input == std::vector<std::uint8_t>(4, 0xEE), "zero fields without full teardown authorized writes");
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(4, 0x42), "refresh did not recover after full teardown");

		// A native link outside the high-level load scope may not reapply disk data.
		auto active_change = File();
		active_change.back() = 0x66;
		Write(active_change);
		gsc::LinkScriptFile(game::at(kServerContextRva), &asset);
		Check(linked_input == std::vector<std::uint8_t>(4, 0xEE), "out-of-scope linker call refreshed active storage");
		gsc::ShutdownScripts(true);
		throw_during_load = true;
		bool caught = false;
		try
		{
			gsc::LoadScripts(nullptr);
		}
		catch (const std::runtime_error&)
		{
			caught = true;
		}
		throw_during_load = false;
		Check(caught && gsc::link_epoch == 0, "exception leaked load scope");

		Write(File(121, 97));
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(97, 0x42), "large resident override did not link from native block-6 tail");
		Check(asset.buffer_ != compressed.data() && asset.bytecode_ != bytecode.data(),
			"large resident override did not publish tail pointers");

		const auto saved = gsc::saved_bytes;
		gsc::ReleaseXAsset(reinterpret_cast<void*>(1), kScriptFileAssetType + 1, 0);
		Check(gsc::saved_bytes == saved && native_releases == 1, "unrelated asset release changed snapshots");
		gsc::ReleaseXAsset(&asset, kScriptFileAssetType, 0);
		Check(gsc::saved_bytes == 0 && gsc::saved_scripts.empty() && native_releases == 2, "released DB storage retained a stale snapshot");
		std::filesystem::remove(gsc::override_root / "sample.gscbin");
		stock_buffer = 'R';
		stock_code = 0x77;
		gsc::LoadScriptFile(1); // same addresses, different zone generation
		FullReload();
		Check(linked_input == std::vector<std::uint8_t>(8, 0x77) && compressed[0] == 'R', "DB address reuse restored an obsolete snapshot");
		gsc::ReleaseXAsset(&asset, kScriptFileAssetType, 0);

		auto* pending = game::at<std::uint8_t>(kRestartPendingRva);
		auto* reload = game::at<std::uint8_t>(kRestartLoadScriptsRva);
		auto* savegame = game::at<std::uint8_t>(kRestartSavegameRva);
		*pending = 0;
		*reload = 0;
		*savegame = 0;
		Check(!gsc::ProcessMapRestart() && !saw_reload, "idle restart state was modified");
		*pending = 1;
		Check(gsc::ProcessMapRestart() == 2 && saw_pending && saw_reload && !saw_savegame,
			"normal map restart did not select native full startup");
		*savegame = 1;
		*reload = 0;
		Check(gsc::ProcessMapRestart() == 2 && saw_savegame && !saw_reload, "savegame restart arguments were changed");
		Check(native_restarts == 3 && native_links >= 9, "native lifecycle calls were suppressed");
	}
}

namespace console
{
	void Print(const char*, ...)
	{
	}
}

namespace iw8x::gsc
{
	void server_starting() noexcept
	{
	}

	void map_starting() noexcept
	{
	}

	void map_shutdown() noexcept
	{
		++map_shutdown_events;
	}

	void scripts_shutdown() noexcept
	{
		++script_shutdown_events;
	}

	void scripts_begin_load() noexcept
	{
	}

	void scripts_loaded() noexcept
	{
	}

	void map_restart() noexcept
	{
	}
}

namespace iw8x::gsc::internal
{
	bool gameplay_server() noexcept
	{
		return gameplay_fixture;
	}

	std::string_view script_function_name(uint32_t)
	{
		return "<unknown>";
	}
}

namespace game
{
	bool initialized() noexcept
	{
		return image_base != 0;
	}

	std::uintptr_t base() noexcept
	{
		return image_base;
	}

	std::uint32_t image_size() noexcept
	{
		return ::image_size;
	}

	bool validate_bytes(const void* address, const void* expected, std::size_t size) noexcept
	{
		std::size_t available = 0;
		return safemem::RegionReadable(address, available) && size <= available && std::memcmp(address, expected, size) == 0;
	}
}

#ifdef IW8X_ISLAND_PORT_PROBE
#include "island_reservation.hpp"
#endif

int main()
{
	ErrorBoundaryChecks();
	BatchRollbackChecks();

	// Exact 1141 predicate: a loaded module whose export object does not contain
	// the requested canonical token. Pending imports must remain pending, and
	// inspecting either path must not modify child values or reference counts.
	struct LinkFixture
	{
		std::array<std::uint8_t, 0x34C8> context{};
		std::array<std::uint8_t, 32 * 4> children{};
		unsigned lookups{};
		bool loaded{}, exported{};
	} link;

	const auto put = [&](std::size_t offset, std::uint32_t value)
	{
		std::memcpy(link.context.data() + offset, &value, sizeof(value));
	};
	put(0xC60, 10);
	put(0xC64, 20);
	auto* children = link.children.data();
	std::memcpy(link.context.data() + 0x34C0, &children, sizeof(children));
	const std::uint32_t export_object = 30;
	std::memcpy(link.children.data() + 64, &export_object, 4);
	link.children[64 + 12] = 1;
	const auto find = +[](void* raw, std::uint32_t object, std::uint32_t token) -> std::uint32_t
	{
		auto& fixture = *reinterpret_cast<LinkFixture*>(raw);
		++fixture.lookups;
		Check(token == (object == 30 ? 0xFA3D : 58274), "diagnostic changed lookup token");
		return object == 10 ? 1 : object == 20 ? 2 : fixture.exported ? 3 : 0;
	};
	const auto inspect = [&]
	{
		return gsc::link_diagnostics::Inspect(&link, 58274, 0xFA3D, find);
	};
	Check(inspect() == gsc::link_diagnostics::ExportState::pending && link.lookups == 1, "unloaded module falsely reported as DEV1141");
	link.children[32 + 12] = 6;
	const auto before_context = link.context;
	const auto before_children = link.children;
	Check(inspect() == gsc::link_diagnostics::ExportState::missing, "missing loaded export not diagnosed");
	link.exported = true;
	Check(inspect() == gsc::link_diagnostics::ExportState::present, "valid export reported missing");
	Check(link.context == before_context && link.children == before_children, "diagnostic modified script objects/refcounts");
	try
	{
		using namespace game::addresses::gsc;
		Check(gsc::Install(0) == hook::Status::Failed, "invalid host accepted");
		image_base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, ::image_size, MEM_RESERVE, PAGE_NOACCESS));
		Check(image_base != 0, "reserve synthetic image");
		Check(VirtualAlloc(game::at(kVarScriptFileRva & ~std::uintptr_t(0xFFF)), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr,
			"commit ScriptFile slot");
		Check(gsc::Install(image_base) == hook::Status::NotReady, "unverified hook bytes accepted");
#ifdef IW8X_ISLAND_PORT_PROBE
		IslandReservationChecks();
#endif
		gsc::installed.store(true);
		gsc::original_load_file = NativeLoad;
		gsc::original_postload_file = NativeLoad;
		gsc::get_script_program_range = FakeProgramRange;
		gsc::override_root = std::filesystem::temp_directory_path() /
			("iw8-gsc-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		Check(std::filesystem::create_directory(gsc::override_root), "create isolated fixture directory");
		gsc::LoadScriptFile(1); // missing file
		Stock();
		// Replay's preload/postload DB path must capture the same pristine
		// storage as the direct Load_ScriptFile path, after native streaming.
		Write(File());
		const auto postload_before = stream_position;
		gsc::PostloadScriptFile(1);
		Check(stream_position == postload_before + 16 && asset.bytecode_len_ == 4,
			"postload path missed override or changed stream consumption");
		const auto before = stream_position;
		gsc::LoadScriptFile(1);
		Check(stream_position == before + 16, "replacement changed native stream accounting");
		Check(asset.buffer_ == compressed.data() && asset.bytecode_ == bytecode.data(), "native ownership changed");
		Check(asset.compressed_len_ == 3 && asset.len_ == 17 && asset.bytecode_len_ == 4, "valid replacement rejected");
		Check(compressed[0] == 'A' && compressed[3] == 'S' && bytecode[0] == 0x42 && bytecode[4] == 0xCC,
			"payload copied beyond replacement bounds");
		auto changed = File();
		changed.back() = 0x43;
		Write(changed);
		gsc::ReapplyCustomScript("sample.gsc");
		gsc::reload_script_file(&asset);
		Check(bytecode[3] == 0x42, "public API overwrote active bytecode");
		gsc::LoadScriptFile(1);
		Check(bytecode[3] == 0x43, "next DB load used cached contents");
		Write(File(121, 97));
		const auto before_large = stream_position;
		gsc::LoadScriptFile(1);
		Check(stream_position == before_large + 16, "large replacement changed native stream accounting");
		Check(asset.compressed_len_ == 121 && asset.bytecode_len_ == 97, "large replacement was not accepted through native block-6 tail");
		Check(asset.buffer_ != compressed.data() && asset.bytecode_ != bytecode.data(), "large replacement did not repoint both payloads");
		Check(std::memcmp(asset.buffer_, std::vector<char>(121, 'A').data(), 121) == 0, "large compressed payload was not copied to tail");
		Check(std::all_of(asset.bytecode_, asset.bytecode_ + 97,
				  [](std::uint8_t value)
				  {
					  return value == 0x42;
				  }),
			"large bytecode payload was not copied to tail");

		program_range_enabled = false;
		Write(File(9, 9));
		gsc::LoadScriptFile(1);
		Stock();
		program_range_enabled = true;

		Write(File(200, 100));
		gsc::LoadScriptFile(1);
		Stock();

		for (auto bad : { File(-1, 4), File(0, 4), File(3, INT32_MAX) })
		{
			Write(bad);
			gsc::LoadScriptFile(1);
			Stock();
		}
		auto good = File();
		for (std::size_t n = 0; n < good.size(); ++n)
		{
			gsc::detail::ScriptData result;
			Check(!gsc::detail::Parse(std::span(good).first(n), result), "truncated file accepted");
		}
		good.push_back(0);
		gsc::detail::ScriptData result;
		Check(!gsc::detail::Parse(good, result), "trailing payload accepted");
		std::vector<std::uint8_t> unterminated(1024, 'x');
		Check(!gsc::detail::Parse(unterminated, result), "unterminated name accepted");
		std::string normalized;
		for (const char* bad : { "../x", "/x", "C:\\x", "a/../b", "a//b", "a/", "" })
			Check(!gsc::detail::NormalizeName(bad, normalized), "unsafe path accepted");
		Check(gsc::detail::NormalizeName("maps\\mp\\test.gsc", normalized) && normalized == "maps/mp/test.gsc",
			"nested native name rejected");
		Check(native_calls == 10, "native load/postload fallback invocation count");
		Check(gsc::streaming_script == nullptr, "stale ScriptFile pointer retained");
		ReloadChecks();
		std::filesystem::remove(gsc::override_root / "sample.gscbin");
		std::filesystem::remove(gsc::override_root);
		VirtualFree(reinterpret_cast<void*>(image_base), 0, MEM_RELEASE);
		image_base = 0;
		std::cout
			<< "Replay GSC load/postload, block-6 growth, resident reload, teardown gates, DB generations, restart routing and variadic error boundary passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
