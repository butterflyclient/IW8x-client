#include "runtime.hpp"
#include "state.hpp"
#include "port_tables.hpp"
#include "../api.hpp"
#include "../ownership.hpp"
#include "thirdparty/json.hpp"

#include "engine/image.hpp"
#include "engine/safemem.h"
#ifdef IW8X_ISLAND_PORT_PROBE
#include "gsc/compiled/gsc.hpp"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
namespace iw8x::gsc::internal
{
	namespace
	{
		namespace native = game::addresses::gsc_api;
		constexpr size_t capacity = native::plugin_capacity;
		constexpr size_t stock_functions = native::stock_function_count, stock_methods = native::stock_method_count;
		constexpr std::string_view database_target = "replay-1.20.4";

		struct binding
		{
			bool method{};
			uint32_t slot{};
			std::string plugin, name;
		};

			std::mutex table_mutex;
			// Slot identity never changes for the lifetime of this process.
			// Publish a fixed-index immutable name once, after its fully
			// validated database entry has been installed. Hot native GSC
			// dispatch must not take table_mutex or scan the disk registry.
			std::array<std::string, capacity * 2> published_names;
			std::array<std::atomic<bool>, capacity * 2> published_slots{};
			// Unknown port builtin IDs can be invoked every frame. Preserve the
			// diagnostic once per slot without flooding disk logs or the render thread.
			std::array<std::atomic_flag, capacity * 2> unassigned_reported{};
	#ifdef IW8X_ISLAND_PORT_PROBE
			// Script bytecode execution does not necessarily re-enter the public
			// VM_Execute entry hook. Sample the live engine-side function frame at
			// the actual missing-builtin boundary, where the caller is known.
			// This is a read-only diagnostic, not a handler for the missing ID.
			void sample_missing_island_builtin(void* ctx, bool method, uint32_t slot) noexcept
			{
					// Source 1.64.5 player ground-surface getter method 0x87CB
					// (Replay extended method slot 68) is still unimplemented: record
					// its exact VM frame without inventing a compatible player-field
					// offset. Also retain the historical function-slot30 profiler.
					const bool source_ground_surface = method && slot == 68;
					if (!source_ground_surface && (method || slot != 30)) return;
				static const bool enabled = []() noexcept
				{
					char flag[4]{};
					return GetEnvironmentVariableA("IW8X_GSC_VM_PROFILE", flag, sizeof(flag)) == 1 && flag[0] == '1';
				}();
				if (!enabled) return;
					thread_local uint32_t calls_ground{}, calls_legacy{};
					auto& calls = source_ground_surface ? calls_ground : calls_legacy;
					// The actual Island getter fires around once per frame. One
					// sample per 4096 calls captures provenance in about a minute;
					// the historical high-volume function keeps its 1/65536 gate.
					const auto frequency_mask = source_ground_surface ? 0xFFFu : 0xFFFFu;
					if ((++calls & frequency_mask) != 0) return;
				void* active_frame{};
				const uint8_t* candidate_pos{};
				uint32_t frame_count{};
				if (!safemem::ReadBytes(static_cast<uint8_t*>(ctx) + native::layout::frame_pointer,
					&active_frame, sizeof(active_frame))) return;
				if (active_frame)
					safemem::ReadBytes(active_frame, &candidate_pos, sizeof(candidate_pos));
				safemem::ReadBytes(static_cast<uint8_t*>(ctx) + native::layout::frame_count,
					&frame_count, sizeof(frame_count));
				void* stack[5]{};
				const auto depth = CaptureStackBackTrace(1, 5, stack, nullptr);
					std::string source_script;
					std::size_t source_offset{};
					const bool source_attributed = candidate_pos &&
						::gsc::DescribeIslandCodeSite(reinterpret_cast<std::uintptr_t>(candidate_pos), source_script, source_offset);
					char message[640]{};
				std::snprintf(message, sizeof(message),
						"[IslandMissingBuiltinSample] method=%u slot=%u id=0x%X calls=%u thread=%lu frame_count=%u frame=%p possible_pos=%p owner=%s offset=%zu stack=%p,%p,%p,%p,%p depth=%u",
						unsigned(method), slot, source_ground_surface ? 0x87CBu : 0x437u,
						calls, static_cast<unsigned long>(GetCurrentThreadId()), frame_count,
						active_frame, candidate_pos, source_attributed ? source_script.c_str() : "<unresolved>",
						source_offset, stack[0], stack[1], stack[2], stack[3], stack[4],
					static_cast<unsigned>(depth));
				report(nullptr, message);
			}
	#endif
			std::vector<binding> entries;
			thread_local native_dispatch_stats<capacity> dispatch_stats;
			void publish_names_locked()
			{
				for (const auto& b : entries)
				{
					const auto index = static_cast<size_t>(b.slot) + (b.method ? capacity : 0);
					if (index >= published_names.size()) throw std::runtime_error("invalid native binding slot");
					if (published_slots[index].load(std::memory_order_acquire))
					{
						if (published_names[index] != b.name) throw std::runtime_error("native binding slot changed after publication");
						continue;
					}
					published_names[index] = b.name;
					published_slots[index].store(true, std::memory_order_release);
				}
			}
			std::string_view native_name_view(const bool method, const uint32_t slot) noexcept
			{
				if (slot >= capacity) return {};
				const auto index = static_cast<size_t>(slot) + (method ? capacity : 0);
				return published_slots[index].load(std::memory_order_acquire) ? std::string_view(published_names[index]) : std::string_view{};
			}
		std::filesystem::path database;
		bool tables_initialized{};

		class database_lock
		{
		public:
			database_lock()
			{
				auto path = database;
				path += L".lock";
				handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open native binding lock");
				if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped_))
				{
					CloseHandle(handle_);
					throw std::runtime_error("cannot lock native binding database");
				}
			}

			~database_lock()
			{
				UnlockFileEx(handle_, 0, 1, 0, &overlapped_);
				CloseHandle(handle_);
			}

		private:
			HANDLE handle_{ INVALID_HANDLE_VALUE };
			OVERLAPPED overlapped_{};
		};

		using function = void (*)(void*);
		using method = void (*)(void*, uint64_t);
		// These arrays and thunks live in the host for its entire process lifetime.
		// One process-lifetime host entry runs stock builtins inside VM_Execute's
		// native error boundary. Existing plugin slots/IDs remain unchanged.
		std::array<function, stock_functions + capacity + 1> functions{};
		std::array<method, stock_methods + capacity> methods{};
		port_tables::view imported_tables{};

		template <size_t Slot>
		void function_thunk(void* ctx)
		{
			dispatch_native(ctx, false, Slot, 0);
		}

		template <size_t Slot>
		void method_thunk(void* ctx, uint64_t self)
		{
			dispatch_native(ctx, true, Slot, self);
		}

		template <size_t... Slots>
		constexpr auto function_thunks(std::index_sequence<Slots...>)
		{
			return std::array<function, sizeof...(Slots)>{ &function_thunk<Slots>... };
		}

		template <size_t... Slots>
		constexpr auto method_thunks(std::index_sequence<Slots...>)
		{
			return std::array<method, sizeof...(Slots)>{ &method_thunk<Slots>... };
		}

		constexpr auto native_functions = function_thunks(std::make_index_sequence<capacity>{});
		constexpr auto native_methods = method_thunks(std::make_index_sequence<capacity>{});

		void atomic_write(const std::filesystem::path& destination, std::string_view content)
		{
			auto temporary = destination;
			temporary += L".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				if (!out || !out.write(content.data(), content.size()) || !out.flush())
					throw std::runtime_error("cannot write native binding file");
			}
			if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				throw std::runtime_error("cannot commit native binding file");
		}

		void save(const std::vector<binding>& bindings)
		{
			nlohmann::json json = { { "api", IW8X_PLUGIN_API_VERSION }, { "target", database_target },
				{ "bindings", nlohmann::json::array() } };
			std::ostringstream header;
			header << "// Generated by IW8x. Replay 1.20.4 only. Keep native-bindings.json with compiled scripts.\n";
			for (const auto& b : bindings)
			{
				json["bindings"].push_back({ { "method", b.method }, { "slot", b.slot }, { "plugin", b.plugin }, { "name", b.name } });
				header << "#define " << b.name << (b.method ? " _meth_" : " _func_") << std::hex << std::setfill('0') << std::setw(4)
					   << (b.slot + (b.method ? native::stock_method_base + stock_methods : native::stock_function_base + stock_functions))
					   << std::dec << '\n';
			}
			// Commit durable identities first; a failed header write never reuses IDs.
			atomic_write(database, json.dump(2));
			atomic_write(database.parent_path() / "iw8x_native_bindings.gsh", header.str());
		}

		template <class T>
		T read(void* ctx, size_t offset)
		{
			T out{};
			if (!safemem::ReadBytes(static_cast<uint8_t*>(ctx) + offset, &out, sizeof(out)))
				throw std::runtime_error("unreadable native dispatch metadata");
			return out;
		}

		template <class T>
		void put(void* ctx, size_t offset, T value)
		{
			// Entire metadata range has been checked writable before any mutation.
			std::memcpy(static_cast<uint8_t*>(ctx) + offset, &value, sizeof(value));
		}

		void validate_writable_metadata(void* ctx)
		{
			MEMORY_BASIC_INFORMATION memory{};
			const auto start = reinterpret_cast<uintptr_t>(ctx) + native::layout::function_base;
			if (!VirtualQuery(reinterpret_cast<void*>(start), &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
				(memory.Protect & PAGE_GUARD) ||
				!(memory.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) ||
				start + native::layout::methods + sizeof(void*) - native::layout::function_base >
					reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize)
				throw std::runtime_error("native dispatch metadata is not writable");
		}

		void refresh_database()
		{
			if (std::filesystem::exists(database))
			{
				if (std::filesystem::file_size(database) > 256 * 1024) throw std::runtime_error("native binding database too large");
				std::ifstream in(database, std::ios::binary);
				const auto json = nlohmann::json::parse(in);
				if (json.value("target", std::string{}) != database_target)
					throw std::runtime_error("native binding database is not for Replay 1.20.4");
				if (json.at("api") != IW8X_PLUGIN_API_VERSION || !json.at("bindings").is_array())
					throw std::runtime_error("invalid native binding database version");
				std::vector<binding> loaded;
				for (const auto& item : json.at("bindings"))
				{
					if (!item.is_object() || item.size() != 4 || !item.at("method").is_boolean() || !item.at("slot").is_number_unsigned() ||
						item.at("slot").get<uint64_t>() >= capacity)
						throw std::runtime_error("invalid native binding shape/slot");
					binding b{ item.at("method").get<bool>(), item.at("slot").get<uint32_t>(), item.at("plugin").get<std::string>(),
						item.at("name").get<std::string>() };
					if (b.slot >= capacity || b.plugin.empty() || b.plugin.size() > 128 || checked_name(string_view(b.name)) != b.name)
						throw std::runtime_error("invalid native binding entry");
					for (const auto& previous : loaded)
						if (previous.name == b.name || (previous.method == b.method && previous.slot == b.slot))
							throw std::runtime_error("duplicate persistent native binding");
					loaded.push_back(std::move(b));
				}
				for (const auto& old : entries)
				{
					const auto same = std::find_if(loaded.begin(), loaded.end(),
						[&](const binding& b)
						{
							return old.method == b.method && old.slot == b.slot && old.name == b.name && old.plugin == b.plugin;
						});
					if (same == loaded.end()) throw std::runtime_error("native binding identities changed on disk");
				}
				entries = std::move(loaded);
			}
			else if (!entries.empty() || std::filesystem::exists(database.parent_path() / "iw8x_native_bindings.gsh"))
			{
				throw std::runtime_error("native binding database missing beside existing compiler bindings");
			}
		}
	}

	void initialize_tables(const std::filesystem::path& root)
	{
		std::lock_guard lock(table_mutex);
		if (tables_initialized) return;
		std::filesystem::create_directories(root);
		database = root / "native-bindings.json";
		database_lock interprocess;
		refresh_database();
		for (const auto& entry : entries)
			if (port_tables::is_reserved(entry.method, entry.slot))
				throw std::runtime_error(
					"existing plugin builtin ID conflicts with a selected port builtin: " + entry.name + "; binding database retained");
		save(entries);
			publish_names_locked();
		tables_initialized = true;
	}

	iw8x_status reserve_native(bool is_method, std::string_view plugin, std::string_view name)
	{
		std::lock_guard lock(table_mutex);
		if (!tables_initialized) return IW8X_UNSUPPORTED;
		database_lock interprocess;
		refresh_database();
			publish_names_locked();
		uint32_t slot{};
		for (const auto& b : entries)
		{
			if (b.name == name)
			{
				if (b.method != is_method || b.plugin != plugin) return IW8X_DUPLICATE;
				save(entries);
				return IW8X_OK;
			}
			if (b.method == is_method) slot = (std::max)(slot, b.slot + 1);
		}
		while (slot < capacity && port_tables::is_reserved(is_method, slot))
			++slot;
		if (slot >= capacity) return IW8X_LIMIT;
		auto proposed = entries;
		proposed.push_back({ is_method, slot, std::string(plugin), std::string(name) });
		save(proposed);
		entries.swap(proposed);
			publish_names_locked();
		return IW8X_OK;
	}

	std::string native_name(bool is_method, uint32_t slot)
	{
			return std::string(native_name_view(is_method, slot));
	}

		native_dispatch_snapshot take_native_dispatch_stats() noexcept
		{
			return dispatch_stats.take();
		}

	void restore_port_tables(void* ctx)
	{
		if (ctx != server_context() || !port_tables::inspect || !imported_tables.functions || !imported_tables.methods) return;
		if (read<function*>(ctx, native::layout::functions) != functions.data() ||
			read<method*>(ctx, native::layout::methods) != methods.data()) return;
		if (read<uint32_t>(ctx, native::layout::frame_count) ||
			read<uint32_t>(ctx, native::layout::function_base) != native::stock_function_base ||
			read<uint32_t>(ctx, native::layout::method_base) != native::stock_method_base ||
			read<uint32_t>(ctx, native::layout::function_count) != functions.size() ||
			read<uint32_t>(ctx, native::layout::method_count) != methods.size() ||
			read<uint32_t>(ctx, native::layout::function_end) != native::stock_function_base + functions.size() ||
			read<uint32_t>(ctx, native::layout::method_end) != native::stock_method_base + methods.size())
			throw std::runtime_error("unexpected merged table owner/layout before script loading");
		validate_writable_metadata(ctx);
		// BeginLoadScripts clears and registers through the current table owner.
		// Return the verified port tables before that phase; merge after it finishes.
		put(ctx, native::layout::functions, imported_tables.functions);
		put(ctx, native::layout::methods, imported_tables.methods);
		put<uint32_t>(ctx, native::layout::function_count, static_cast<uint32_t>(imported_tables.function_count));
		put<uint32_t>(ctx, native::layout::method_count, static_cast<uint32_t>(imported_tables.method_count));
		put<uint32_t>(ctx, native::layout::function_end,
			static_cast<uint32_t>(native::stock_function_base + imported_tables.function_count));
		put<uint32_t>(ctx, native::layout::method_end,
			static_cast<uint32_t>(native::stock_method_base + imported_tables.method_count));
	}

	void publish_tables(void* ctx)
	{
		if (ctx != server_context()) return;
		if (read<uint32_t>(ctx, native::layout::frame_count)) throw std::runtime_error("refusing to replace active VM tables");
		auto* old_functions = read<function*>(ctx, native::layout::functions);
		auto* old_methods = read<method*>(ctx, native::layout::methods);
		const bool stock =
			old_functions == game::at<function>(native::native_functions) && old_methods == game::at<method>(native::native_methods);
		const bool ours = old_functions == functions.data() && old_methods == methods.data();
		port_tables::view port;
		const bool imported = port_tables::inspect && port_tables::inspect(ctx, port);
		if (imported &&
			(port.function_count < stock_functions || port.function_count > functions.size() || port.method_count < stock_methods ||
				port.method_count > methods.size()))
			throw std::runtime_error("port builtin table exceeds the merged dispatch capacity");
		if ((!stock && !ours && !imported) || read<uint32_t>(ctx, native::layout::function_base) != native::stock_function_base ||
			read<uint32_t>(ctx, native::layout::method_base) != native::stock_method_base ||
			read<uint32_t>(ctx, native::layout::function_count) !=
				(imported ? port.function_count : stock_functions + (ours ? capacity + 1 : 0)) ||
			read<uint32_t>(ctx, native::layout::method_count) != (imported ? port.method_count : stock_methods + (ours ? capacity : 0)) ||
			read<uint32_t>(ctx, native::layout::function_end) !=
				native::stock_function_base + (imported ? port.function_count : stock_functions + (ours ? capacity + 1 : 0)) ||
			read<uint32_t>(ctx, native::layout::method_end) !=
				native::stock_method_base + (imported ? port.method_count : stock_methods + (ours ? capacity : 0)))
			throw std::runtime_error("unexpected native table owner/layout");
		validate_writable_metadata(ctx);
		if (stock || imported)
		{
			if (!safemem::ReadBytes(
					old_functions, functions.data(), (imported ? port.function_count : stock_functions) * sizeof(function)) ||
				!safemem::ReadBytes(old_methods, methods.data(), (imported ? port.method_count : stock_methods) * sizeof(method)))
				throw std::runtime_error("cannot preserve native dispatch tables");
		}
		if (imported) imported_tables = port;
		else if (stock) imported_tables = {};
		for (size_t i = 0; i < capacity; ++i)
		{
			const auto provider = port_tables::function_at_slot ? port_tables::function_at_slot(static_cast<uint32_t>(i)) : 0;
			if (provider)
			{
				if (!native_name(false, static_cast<uint32_t>(i)).empty())
					throw std::runtime_error("port builtin conflicts with a registered plugin function");
				functions[stock_functions + i] = reinterpret_cast<function>(provider);
			}
			else if (!port_tables::is_reserved(false, static_cast<uint32_t>(i))) functions[stock_functions + i] = native_functions[i];
		}
		static_assert(invocation_builtin_id == stock_functions + capacity + 1);
		functions.back() = &invocation_trampoline;
			for (size_t i = 0; i < capacity; ++i)
			{
				const auto provider = port_tables::method_at_slot ? port_tables::method_at_slot(static_cast<uint32_t>(i)) : 0;
				if (provider)
				{
					if (!native_name(true, static_cast<uint32_t>(i)).empty())
						throw std::runtime_error("port builtin conflicts with a registered plugin method");
					methods[stock_methods + i] = reinterpret_cast<method>(provider);
				}
				else if (!port_tables::is_reserved(true, static_cast<uint32_t>(i))) methods[stock_methods + i] = native_methods[i];
			}
		put(ctx, native::layout::functions, functions.data());
		put(ctx, native::layout::methods, methods.data());
		put<uint32_t>(ctx, native::layout::function_count, static_cast<uint32_t>(functions.size()));
		put<uint32_t>(ctx, native::layout::function_end, static_cast<uint32_t>(native::stock_function_base + functions.size()));
		put<uint32_t>(ctx, native::layout::method_count, static_cast<uint32_t>(methods.size()));
		put<uint32_t>(ctx, native::layout::method_end, static_cast<uint32_t>(native::stock_method_base + methods.size()));
	}

	void dispatch_native(void* ctx, bool is_method, uint32_t slot, uint64_t packed_self) noexcept
	{
		if (ctx != server_context()) return;
			const auto started = std::chrono::steady_clock::now();
			bool missing = false;
			struct measure
			{
				bool method, &missing;
				uint32_t slot;
				std::chrono::steady_clock::time_point started;
				~measure() noexcept
				{
					const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - started).count();
					dispatch_stats.record(method, slot, missing, static_cast<std::uint64_t>((std::max)(duration, 0ll)));
				}
			} meter{ is_method, missing, slot, started };
		try
		{
			game_scope scope;
			try
			{
				value result;
					const auto name = native_name_view(is_method, slot);
					if (name.empty())
					{
							missing = true;
	#ifdef IW8X_ISLAND_PORT_PROBE
								sample_missing_island_builtin(ctx, is_method, slot);
	#endif
						const auto index = (is_method ? capacity : 0) + slot;
						if (index >= unassigned_reported.size() ||
							!unassigned_reported[index].test_and_set(std::memory_order_relaxed))
						{
#ifdef IW8X_ISLAND_PORT_PROBE
						std::ostringstream diagnostic;
						diagnostic << "unassigned native builtin ID: method=" << is_method << " slot=" << slot << " id=0x" << std::hex
							<< (is_method ? native::stock_method_base + stock_methods + slot : native::stock_function_base + stock_functions + slot);
							// First sighting only: identify the real native GSC program owner
							// before treating any new source-version ID as a port target.
							// Never insert a fake stub or replace another engine builtin
							// merely to silence this diagnostic.
							void* frame{};
							const std::uint8_t* position{};
							if (safemem::ReadBytes(static_cast<const std::uint8_t*>(ctx) + native::layout::frame_pointer,
								&frame, sizeof(frame)) && frame && safemem::ReadBytes(frame, &position, sizeof(position)))
							{
								std::string script;
								std::size_t offset{};
								if (position && ::gsc::DescribeIslandCodeSite(reinterpret_cast<std::uintptr_t>(position), script, offset))
									diagnostic << " source=" << script << " bytecode_offset=" << std::dec << offset;
								else diagnostic << " source=<unresolved>";
							}
						report(nullptr, diagnostic.str().c_str());
#else
						report(nullptr, "unassigned native builtin ID");
#endif
						}
					}
				else
				{
					// Copy input before notify or return can consume native stack values.
					const auto args = read_arguments(ctx);
					const auto invoked = invoke(name, is_method, is_method ? capture_entity(packed_self) : entity_ref{}, args);
					if (invoked.status == IW8X_OK)
						result = invoked.result;
					else
						report(nullptr, invoked.error.c_str());
				}
				prepare_return(ctx, result);
				write_return(ctx, result);
			}
			catch (const std::exception& e)
			{
				report(nullptr, e.what());
				// Keep the server scope while publishing the failed call's result.
				value empty;
				prepare_return(ctx, empty);
				write_return(ctx, empty);
			}
		}
		catch (const std::exception& e)
		{
			report(nullptr, e.what());
		}
		catch (...)
		{
			report(nullptr, "native dispatch failed");
		}
	}
}
