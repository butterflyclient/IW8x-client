#include "gsc.hpp"
#include "script_data.hpp"
#include "replay_bytecode.hpp"
#include "link_diagnostics.hpp"
#include "gsc/api.hpp"
#include "gsc/internal/runtime.hpp"

#include "common/console.h"

#include "engine/image.hpp"
#include "engine/inline_hook.h"

#include "thirdparty/minhook/include/MinHook.h"

#include "engine/safemem.h"
#ifdef IW8X_ISLAND_PORT_PROBE
#include "diagnostics/island/island_builtins.hpp"
#include "diagnostics/island/island_loot.hpp"
#include "diagnostics/island/island_operator_table.hpp"
#include "diagnostics/night/night_files.h"
#include "island_quest_native_abi.hpp"
#endif

#include <algorithm>
#include <array>

#include "gsc/internal/state.hpp"

#include <charconv>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <tuple>
#include <unordered_set>
#include <vector>
namespace gsc
{
	namespace
	{
		using namespace game::addresses::gsc;
		using LoadScriptFileFn = void (*)(int);
		using LoadScriptsFn = void (*)(const void*);
		using ShutdownScriptsFn = void (*)(bool);
		using LinkScriptFileFn = void (*)(void*, ScriptFile*);
		using ProcessMapRestartFn = int (*)(); // caller tests EAX, not AL
		using ReleaseXAssetFn = void (*)(void*, unsigned, unsigned);
		using AllocZoneMemoryFn = void (*)(const std::uint64_t*, void*, void*, void*);
		using GetScriptProgramRangeFn = std::uint8_t* (*)(std::uint32_t*);
#ifdef IW8X_ISLAND_PORT_PROBE
		constexpr std::size_t hook_count = 13;
		using fixup_preload_fn = void (*)(void*, const char*);
		using free_zone_fn = void (*)(void*);
		fixup_preload_fn original_fixup_preload{};
		free_zone_fn original_free_zone{};
		// Replay DB_FixupPreloadScriptMemory and DB_FreeXZoneMemory. The
		// preload fixup moves block 6 from temporary memory into the script heap.
		constexpr std::uint8_t fixup_preload_entry[]{ 0x4c, 0x8b, 0xdc, 0x55, 0x56, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0 };
		constexpr std::uint8_t free_zone_entry[]{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20 };
		using canonical_name_fn = std::uint32_t (*)(const char*);
		canonical_name_fn original_canonical_name{};
		std::uint32_t canonical_delta{};
		bool source_script_memory{};
			std::unordered_set<std::uintptr_t> island_quest_rebound_code{};
		using asset_exists_fn = int (*)(int, const char*);
		using find_asset_fn = void* (*)(int, const char*, int);
		asset_exists_fn original_asset_exists{};
		find_asset_fn original_find_asset{};
		constexpr std::uint8_t asset_exists_entry[]{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20 };
		constexpr std::uint8_t find_asset_entry[]{ 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
#include "diagnostics/island/island_script_memory.inc"
		struct memory_rawfile
		{
			const char* name;
			int compressed;
			int length;
			const char* buffer;
		};
		static_assert(sizeof(memory_rawfile) == 24 && sizeof(island_script_memory) < 4096);
		memory_rawfile script_memory{ "script_memory.json", sizeof(island_script_memory) - 1, island_memory_plain_length, island_script_memory };

		bool memory_asset(int type, const char* name)
		{
			return source_script_memory && type == 51 && name && std::strcmp(name, script_memory.name) == 0;
		}

		const char* island_table(int type, const char* name)
		{
			if (!canonical_delta || type != 54 || !name) return name;
			const auto* map = *game::at<const char*>(0x5cc7538);
			if (!map || std::strcmp(map, "mp_sm_island_1") != 0) return name;
			// MP lobby LUI reads *Replay 1.20.4's* original operators.csv.
			// Redirecting this DB lookup on a client to a map-specific table
			// not resident at frontend startup makes GetWalkAnimationForIndex
			// receive nil and crashes LUI Main. The 39-column private extension
			// is exclusively for original Island server GSC, not client LUI.
			if (std::strcmp(name, "operators.csv") == 0)
				return island_operator_table::resolve(name, night_files::DedicatedIslandBR());
			// Use Replay 1.20.4 native BR loot even when a legacy launcher still
			// has IW8X_ISLAND_ORIGINAL_BR_LOOT=1 in its environment. Do not mix
			// newer source loot CSVs with Replay's native table relationships.
			return island_loot::table_name(name);
		}

		int asset_exists(int type, const char* name)
		{
			if (memory_asset(type, name))
			{
				LOG_INFO("GSC", "isolated native memory lookup: original Source configuration present");
				return 1;
			}
			return original_asset_exists(type, island_table(type, name));
		}

		void* find_asset(int type, const char* name, int allow_default)
		{
			// MemoryInit precedes custom-zone loading. Supply the original Source
			// dependency early; Replay still parses/reserves/allocates its own pools.
			if (memory_asset(type, name)) return &script_memory;
			const auto* table = island_table(type, name);
			return original_find_asset(type, table, allow_default);
		}
		constexpr std::uint8_t canonical_name_entry[]{ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10 };

		std::uint32_t canonical_name(const char* name)
		{
			// Explicit isolated probe only. Native hashing, interning and errors
			// remain intact; the new prefix prevents collisions with Source IDs.
			return original_canonical_name(name) + canonical_delta;
		}
#else
		constexpr std::size_t hook_count = 8;
#endif
		std::array<void*, hook_count> targets{}, trampolines{};
		std::mutex install_mutex;
		LoadScriptFileFn original_load_file = nullptr;
		LoadScriptFileFn original_postload_file = nullptr;
		LoadScriptsFn original_load_scripts = nullptr;
		ShutdownScriptsFn original_shutdown = nullptr;
		LinkScriptFileFn original_link_file = nullptr;
		ProcessMapRestartFn original_process_restart = nullptr;
		ReleaseXAssetFn original_release_asset = nullptr;
		AllocZoneMemoryFn original_alloc_zone_memory = nullptr;
		GetScriptProgramRangeFn get_script_program_range = nullptr;
		std::filesystem::path override_root;
		std::atomic<bool> installed{ false };
		std::atomic<bool> release_installed{ false };
		std::atomic<std::uint64_t> stream_loads{ 0 }, replacements{ 0 };
		std::atomic<std::uint64_t> restored_files{ 0 }, linked_files{ 0 };
		std::atomic<unsigned> link_gate_samples{ 0 };
		thread_local ScriptFile* streaming_script = nullptr;
		std::atomic<bool> reset_ready{ false }, ever_loaded{ false };
		std::uint64_t next_epoch = 0; // server lifecycle thread only
		// Native script loading can dispatch linker work to another thread.
		// Publish the full-reset epoch for all workers while that load is active.
		std::atomic<std::uint64_t> link_epoch{ 0 };

		// These address values are identities, never pointers to dereference later.
		// The native linker/release dispatcher supplies the current live header.
		using StorageKey = std::tuple<std::uintptr_t, std::uintptr_t, std::string>;

		struct SavedScript
		{
			std::vector<std::uint8_t> compressed, bytecode;
			int len{};
			const char* native_buffer{};
			std::uint8_t* native_bytecode{};
			const char* applied_buffer_ptr{};
			std::uint8_t* applied_bytecode_ptr{};
			int applied_compressed{}, applied_len{}, applied_bytecode{};
			std::uint8_t* tail_storage{};
			std::size_t tail_capacity{};
			std::uint64_t linked_epoch{};

			std::size_t Size() const
			{
				return compressed.size() + bytecode.size();
			}

			void Applied(const ScriptFile& script)
			{
				applied_buffer_ptr = script.buffer_;
				applied_bytecode_ptr = script.bytecode_;
				applied_compressed = script.compressed_len_;
				applied_len = script.len_;
				applied_bytecode = script.bytecode_len_;
			}
		};

		std::mutex saved_mutex;
		std::map<StorageKey, SavedScript> saved_scripts;
		std::map<StorageKey, StorageKey> saved_aliases;
#ifdef IW8X_ISLAND_PORT_PROBE
		std::map<std::string, StorageKey> probe_latest_scripts;
#endif
		std::size_t saved_bytes = 0;
		constexpr std::size_t kSnapshotLimit = 256 * 1024 * 1024;

		struct BlockLayout
		{
			std::uintptr_t address{};
			std::uint64_t size{};
		};

		struct ZoneAllocation
		{
			std::uint32_t first_page{}, last_page{};
			std::uint8_t* allocation{};
			std::uint64_t size{};
		};

		static_assert(sizeof(BlockLayout) == 0x10);
		static_assert(sizeof(ZoneAllocation) == 0x18);
#ifdef IW8X_ISLAND_PORT_PROBE
		constexpr std::size_t probe_code_capacity = 4 * 1024 * 1024;
		std::uint8_t* probe_code_begin{};
		void* probe_code_owner{};
		std::uint64_t probe_code_logical_size{};
		std::size_t probe_code_used{};
		std::uint64_t probe_code_epoch{};
		std::map<std::uintptr_t, std::pair<std::uint8_t*, std::size_t>> probe_code_views;
			std::map<std::uintptr_t, std::pair<std::size_t, std::string>> probe_code_names;
#endif

		struct Block6Tail
		{
			struct FreeRange
			{
				std::uintptr_t begin{}, end{};
			};

			std::uintptr_t allocation_begin{}, logical_begin{}, logical_end{}, tail_end{};
			std::vector<FreeRange> free;
		};

		std::mutex tail_mutex;
		std::map<std::uintptr_t, Block6Tail> block6_tails;

		bool ReadIdentity(ScriptFile* script, ScriptFile& native, std::string& name)
		{
			char raw_name[detail::kNameLimit]{};
			if (!safemem::ReadBytes(script, &native, sizeof(native)) || !native.name_) return false;
			const auto length = safemem::ReadString(native.name_, raw_name, sizeof(raw_name));
			char terminator = 1;
			return length && length < sizeof(raw_name) - 1 && safemem::ReadBytes(native.name_ + length, &terminator, 1) && !terminator &&
				detail::NormalizeName(std::string_view(raw_name, length), name);
		}

		StorageKey Key(const ScriptFile& script, const std::string& name)
		{
			return { reinterpret_cast<std::uintptr_t>(script.buffer_), reinterpret_cast<std::uintptr_t>(script.bytecode_), name };
		}

		StorageKey Key(const char* buffer, const std::uint8_t* bytecode, const std::string& name)
		{
			return { reinterpret_cast<std::uintptr_t>(buffer), reinterpret_cast<std::uintptr_t>(bytecode), name };
		}

		bool Writable(void* pointer, std::size_t size);

		auto FindSavedLocked(const ScriptFile& script, const std::string& name)
		{
			const auto current = Key(script, name);
			auto entry = saved_scripts.find(current);
			if (entry != saved_scripts.end()) return entry;
			const auto alias = saved_aliases.find(current);
			return alias == saved_aliases.end() ? saved_scripts.end() : saved_scripts.find(alias->second);
		}

		void RemoveAliasesLocked(const StorageKey& stable)
		{
			for (auto it = saved_aliases.begin(); it != saved_aliases.end();)
			{
				if (it->second == stable)
					it = saved_aliases.erase(it);
				else
					++it;
			}
		}

		void ReleaseBlock6Tail(SavedScript& saved);

		void PublishAliasLocked(const StorageKey& stable, const std::string& name, const SavedScript& saved)
		{
			RemoveAliasesLocked(stable);
			if (saved.applied_buffer_ptr != saved.native_buffer || saved.applied_bytecode_ptr != saved.native_bytecode)
			{
				saved_aliases[Key(saved.applied_buffer_ptr, saved.applied_bytecode_ptr, name)] = stable;
			}
		}

		void EraseSavedLocked(const StorageKey& stable)
		{
			const auto entry = saved_scripts.find(stable);
			if (entry == saved_scripts.end()) return;
#ifdef IW8X_ISLAND_PORT_PROBE
			const auto latest = probe_latest_scripts.find(std::get<2>(stable));
			if (latest != probe_latest_scripts.end() && latest->second == stable) probe_latest_scripts.erase(latest);
#endif
			ReleaseBlock6Tail(entry->second);
			saved_bytes -= entry->second.Size();
			RemoveAliasesLocked(stable);
			saved_scripts.erase(entry);
		}

		void ForgetStorage(const ScriptFile& script)
		{ // saved_mutex held
			const auto buffer = reinterpret_cast<std::uintptr_t>(script.buffer_);
			const auto code = reinterpret_cast<std::uintptr_t>(script.bytecode_);
			std::vector<StorageKey> erase;
			for (const auto& [key, saved] : saved_scripts)
			{
				(void)saved;
				if (std::get<0>(key) == buffer && std::get<1>(key) == code) erase.push_back(key);
			}
			for (const auto& [alias, stable] : saved_aliases)
			{
				if (std::get<0>(alias) == buffer && std::get<1>(alias) == code &&
					std::find(erase.begin(), erase.end(), stable) == erase.end())
					erase.push_back(stable);
			}
			for (const auto& key : erase)
				EraseSavedLocked(key);
		}

		bool ProgramRangeContains(const std::uintptr_t begin, const std::size_t size)
		{
			if (!get_script_program_range || !begin || !size || size > UINTPTR_MAX - begin) return false;
			std::uint32_t program_size = 0;
			auto* const program = get_script_program_range(&program_size);
			const auto program_begin = reinterpret_cast<std::uintptr_t>(program);
			if (!program_begin || !program_size || program_size > UINTPTR_MAX - program_begin) return false;
			const auto end = begin + size;
			return begin >= program_begin && end <= program_begin + program_size;
		}

		void CaptureBlock6Tail(const std::uint64_t logical_size, void* allocations_ptr, void* layouts_ptr)
		{
			if (!logical_size || !allocations_ptr || !layouts_ptr) return;
			BlockLayout block{};
			if (!safemem::ReadBytes(static_cast<const std::uint8_t*>(layouts_ptr) + 6 * sizeof(BlockLayout), &block, sizeof(block)) ||
				!block.address || block.size != logical_size || block.size > UINTPTR_MAX - block.address)
				return;
			const auto logical_end = block.address + static_cast<std::uintptr_t>(block.size);

			ZoneAllocation owner{};
			bool found = false;
			for (std::size_t group = 0; group < 4; ++group)
			{
				ZoneAllocation candidate{};
				if (!safemem::ReadBytes(static_cast<const std::uint8_t*>(allocations_ptr) + group * sizeof(ZoneAllocation), &candidate,
						sizeof(candidate)) ||
					!candidate.allocation || !candidate.size)
					continue;
				const auto begin = reinterpret_cast<std::uintptr_t>(candidate.allocation);
				if (candidate.size > UINTPTR_MAX - begin) continue;
				const auto end = begin + static_cast<std::uintptr_t>(candidate.size);
				if (block.address >= begin && logical_end <= end)
				{
					owner = candidate;
					found = true;
					break;
				}
			}
			if (!found) return;
			const auto allocation_begin = reinterpret_cast<std::uintptr_t>(owner.allocation);
			const auto allocation_end = allocation_begin + static_cast<std::uintptr_t>(owner.size);
			if (logical_end >= allocation_end) return;

			std::lock_guard lock(tail_mutex);
			Block6Tail range;
			range.allocation_begin = allocation_begin;
			range.logical_begin = block.address;
			range.logical_end = logical_end;
			range.tail_end = allocation_end;
			range.free.push_back({ logical_end, allocation_end });
			block6_tails[allocation_begin] = std::move(range);
		}

		std::uint8_t* AllocateBlock6Tail(const SavedScript& saved, const std::size_t size)
		{
			if (!size || !saved.native_buffer || !saved.native_bytecode) return nullptr;
			const auto buffer = reinterpret_cast<std::uintptr_t>(saved.native_buffer);
			const auto code = reinterpret_cast<std::uintptr_t>(saved.native_bytecode);
			const auto compressed = saved.compressed.size();
			const auto bytecode = saved.bytecode.size();
			if (compressed > UINTPTR_MAX - buffer || bytecode > UINTPTR_MAX - code) return nullptr;

			std::lock_guard lock(tail_mutex);
			for (auto& [base, range] : block6_tails)
			{
				(void)base;
				if (buffer < range.logical_begin || buffer + compressed > range.logical_end || code < range.logical_begin ||
					code + bytecode > range.logical_end)
					continue;
				for (auto it = range.free.begin(); it != range.free.end(); ++it)
				{
					if (it->end < it->begin || size > it->end - it->begin) continue;
					const auto candidate = it->end - size;
					if (!ProgramRangeContains(candidate, size) || !Writable(reinterpret_cast<void*>(candidate), size)) continue;
					it->end = candidate;
					if (it->begin == it->end) range.free.erase(it);
					return reinterpret_cast<std::uint8_t*>(candidate);
				}
			}
			return nullptr;
		}

		void ReleaseBlock6Tail(SavedScript& saved)
		{
			if (!saved.tail_storage || !saved.tail_capacity) return;
			const auto begin = reinterpret_cast<std::uintptr_t>(saved.tail_storage);
			if (saved.tail_capacity > UINTPTR_MAX - begin)
			{
				saved.tail_storage = nullptr;
				saved.tail_capacity = 0;
				return;
			}
			const auto end = begin + saved.tail_capacity;
			std::lock_guard lock(tail_mutex);
			for (auto& [base, range] : block6_tails)
			{
				(void)base;
				if (begin < range.logical_end || end > range.tail_end) continue;
				range.free.push_back({ begin, end });
				std::sort(range.free.begin(), range.free.end(),
					[](const Block6Tail::FreeRange& a, const Block6Tail::FreeRange& b)
					{
						return a.begin < b.begin;
					});
				std::vector<Block6Tail::FreeRange> merged;
				for (const auto& part : range.free)
				{
					if (!merged.empty() && part.begin <= merged.back().end)
						merged.back().end = std::max(merged.back().end, part.end);
					else
						merged.push_back(part);
				}
				range.free = std::move(merged);
				break;
			}
			saved.tail_storage = nullptr;
			saved.tail_capacity = 0;
		}

#ifdef IW8X_ISLAND_PORT_PROBE
		void ResetCompactCode()
		{ // saved_mutex held; the native zone remains the allocation owner.
			probe_code_begin = nullptr;
			probe_code_used = 0;
			probe_code_epoch = 0;
			probe_code_views.clear();
				probe_code_names.clear();
				island_quest_rebound_code.clear();
		}

		bool PublishCompactCode(void* owner, void* layouts, const char* phase)
		{ // saved_mutex held
			if (owner != probe_code_owner || !probe_code_logical_size || !layouts) return false;
			BlockLayout block{};
			if (!safemem::ReadBytes(static_cast<const std::uint8_t*>(layouts) + 6 * sizeof(block), &block, sizeof(block)) ||
				block.size != probe_code_logical_size + probe_code_capacity || !block.address ||
				block.address > UINTPTR_MAX - block.size ||
				!ProgramRangeContains(block.address, static_cast<std::size_t>(block.size))) return false;
			const auto begin = block.address + probe_code_logical_size;
			if (!Writable(reinterpret_cast<void*>(begin), probe_code_capacity)) return false;
			ResetCompactCode();
			probe_code_begin = reinterpret_cast<std::uint8_t*>(begin);
			LOG_INFO("GSC", "isolated compact code reservation: base=%p capacity=%zu phase=%s", probe_code_begin,
				probe_code_capacity, phase);
			return true;
		}

		void FixupPreloadScriptMemory(void* owner, const char* name)
		{
			original_fixup_preload(owner, name);
			std::lock_guard lock(saved_mutex);
			if (owner != probe_code_owner) return;
			const std::uint8_t* temporary{};
			// Verified Replay fields: XZoneMemory::tempData +0x60, archive blocks +0x540.
			if (safemem::ReadBytes(static_cast<const std::uint8_t*>(owner) + 0x60, &temporary, sizeof(temporary)) && temporary)
				(void)PublishCompactCode(owner, const_cast<std::uint8_t*>(temporary) + 0x540, "postload");
		}

		void FreeZoneMemory(void* owner)
		{
			{
				std::lock_guard lock(saved_mutex);
				if (owner == probe_code_owner)
				{
					ResetCompactCode();
					probe_code_owner = nullptr;
					probe_code_logical_size = 0;
				}
			}
			original_free_zone(owner);
		}

		bool IslandScriptLoad()
		{
			if (!canonical_delta || !iw8x::gsc::internal::gameplay_server()) return false;
			const char* map{};
			char name[64]{};
			return safemem::ReadBytes(game::at(0x5cc7538), &map, sizeof(map)) && map &&
				safemem::ReadString(map, name, sizeof(name)) && std::strcmp(name, "mp_sm_island_1") == 0;
		}
#endif

		void AllocZoneMemory(const std::uint64_t* block_sizes, void* arg2, void* allocations, void* layouts)
		{
			std::uint64_t block6 = 0;
			if (block_sizes) safemem::ReadBytes(block_sizes + 6, &block6, sizeof(block6));
#ifdef IW8X_ISLAND_PORT_PROBE
			std::array<std::uint64_t, 8> reserved{};
			char filename[260]{}, enabled[2]{};
			const bool compact = block6 &&
				GetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", enabled, sizeof(enabled)) == 1 && enabled[0] == '1' &&
				safemem::ReadString(static_cast<const char*>(arg2), filename, sizeof(filename)) &&
				(std::strcmp(filename, "magma_island_scripts") == 0 || std::strcmp(filename, "magma_island_scripts.ff") == 0) &&
				safemem::ReadBytes(block_sizes, reserved.data(), sizeof(reserved)) &&
				block6 <= UINT64_MAX - probe_code_capacity;
			if (compact)
			{
				// Reserve an unused suffix in this native zone's script allocation.
				// Native DB owns its commit, bounds, release and reload lifetime.
				reserved[6] += probe_code_capacity;
				original_alloc_zone_memory(reserved.data(), arg2, allocations, layouts);
				{
					std::lock_guard lock(saved_mutex);
					ResetCompactCode();
					probe_code_owner = allocations;
					probe_code_logical_size = block6;
					if (!PublishCompactCode(allocations, layouts, "load"))
						LOG_INFO("GSC", "isolated compact code reservation awaits native preload promotion: zone='%s'", filename);
				}
				if (installed.load(std::memory_order_acquire)) CaptureBlock6Tail(reserved[6], allocations, layouts);
				return;
			}
#endif
			original_alloc_zone_memory(block_sizes, arg2, allocations, layouts);
			if (installed.load(std::memory_order_acquire)) CaptureBlock6Tail(block6, allocations, layouts);
		}

#ifdef IW8X_ISLAND_PORT_PROBE
		bool CompactCompilerCode(ScriptFile& script, const std::uint64_t epoch)
		{
			std::lock_guard lock(saved_mutex);
			if (!probe_code_begin || script.bytecode_len_ <= 0 ||
				!ProgramRangeContains(reinterpret_cast<std::uintptr_t>(probe_code_begin), probe_code_capacity) ||
				!Writable(probe_code_begin, probe_code_capacity)) return false;
			if (probe_code_epoch != epoch)
			{
				probe_code_epoch = epoch;
				probe_code_used = 0;
				probe_code_views.clear();
				probe_code_names.clear();
					island_quest_rebound_code.clear();
			}
			const auto identity = reinterpret_cast<std::uintptr_t>(script.bytecode_);
			const auto size = static_cast<std::size_t>(script.bytecode_len_);
			const auto previous = probe_code_views.find(identity);
			if (previous != probe_code_views.end())
			{
				if (previous->second.second != size) return false;
				script.bytecode_ = previous->second.first;
				return true;
			}
			const auto aligned = (probe_code_used + 7) & ~std::size_t{ 7 };
			if (aligned > probe_code_capacity || size > probe_code_capacity - aligned) return false;
			auto* destination = probe_code_begin + aligned;
			if (!safemem::ReadBytes(script.bytecode_, destination, size)) return false;
			probe_code_views.emplace(identity, std::pair{ destination, size });
			probe_code_used = aligned + size;
			script.bytecode_ = destination;
			return true;
		}
#endif

		bool AppliedViewMatches(const ScriptFile& current, const SavedScript& saved)
		{
			return current.buffer_ == saved.applied_buffer_ptr && current.bytecode_ == saved.applied_bytecode_ptr &&
				current.compressed_len_ == saved.applied_compressed && current.len_ == saved.applied_len &&
				current.bytecode_len_ == saved.applied_bytecode;
		}

		void RestoreNativeHeader(ScriptFile* script, const SavedScript& saved)
		{
			script->buffer_ = saved.native_buffer;
			script->bytecode_ = saved.native_bytecode;
			script->compressed_len_ = static_cast<int>(saved.compressed.size());
			script->len_ = saved.len;
			script->bytecode_len_ = static_cast<int>(saved.bytecode.size());
		}

		bool RestorePristine(ScriptFile* script, const SavedScript& saved)
		{
			if (!Writable(script, sizeof(*script)) || !Writable(const_cast<char*>(saved.native_buffer), saved.compressed.size()) ||
				!Writable(saved.native_bytecode, saved.bytecode.size()))
				return false;
			std::memcpy(const_cast<char*>(saved.native_buffer), saved.compressed.data(), saved.compressed.size());
			std::memcpy(saved.native_bytecode, saved.bytecode.data(), saved.bytecode.size());
			RestoreNativeHeader(script, saved);
			return true;
		}

		bool Writable(void* pointer, std::size_t size)
		{
			auto cursor = reinterpret_cast<std::uintptr_t>(pointer);
			if (!cursor || size > UINTPTR_MAX - cursor) return false;
			const auto end = cursor + size;
			while (cursor < end)
			{
				MEMORY_BASIC_INFORMATION region{};
				if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) || region.State != MEM_COMMIT ||
					(region.Protect & PAGE_GUARD))
					return false;
				const auto protection = region.Protect & 0xFF;
				if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READWRITE &&
					protection != PAGE_EXECUTE_WRITECOPY)
					return false;
				const auto region_end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
				if (region_end <= cursor) return false;
				cursor = region_end;
			}
			return true;
		}

		std::filesystem::path OverridePath(const std::string& name)
		{
			auto filename = name;
			if (filename.ends_with(".gsc"))
				filename += "bin";
			else if (!filename.ends_with(".gscbin"))
				filename += ".gscbin";
			auto path = override_root / filename;
			// Native compiled modules can have decimal canonical names. Prefer an
			// exact native-name override; a readable catalog path is an optional alias.
			std::uint32_t token{};
			const auto parsed = std::from_chars(name.data(), name.data() + name.size(), token);
			if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size())
			{
				const auto readable = iw8x::gsc::internal::script_function_name(token);
				std::error_code error;
				if (!std::filesystem::is_regular_file(path, error) && readable != "<unknown>")
				{
					std::string normalized;
					if (detail::NormalizeName(readable, normalized)) path = override_root / (normalized + ".gscbin");
				}
			}
			return path;
		}

#ifdef IW8X_ISLAND_PORT_PROBE
		bool DiskOverrideAllowed(std::string_view name)
		{
			// br_quest_util is shared with stock playlists, but its converted
			// 1.64 export set is required only by the Island map.
			return name != "scripts/mp/gametypes/br_quest_util.gsc" || IslandScriptLoad();
		}
#endif

		bool ApplyDiskOverride(ScriptFile* script, const std::string& name, const StorageKey& stable, SavedScript& saved)
		{
#ifdef IW8X_ISLAND_PORT_PROBE
			// The Island companion has a newer br_quest_util than Replay's stock
			// module. Never replace the shared stock script while loading another
			// map or during frontend/DB preload; only the Island server linker may
			// select this explicit, disk-backed compatibility module.
			if (!DiskOverrideAllowed(name)) return false;
#endif
			ScriptFile native{};
			if (!safemem::ReadBytes(script, &native, sizeof(native)) || native.buffer_ != saved.native_buffer ||
				native.bytecode_ != saved.native_bytecode)
				return false;
			const auto path = OverridePath(name);
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file.is_open()) return false;
			const auto size = file.tellg();
			if (size <= 0 || size > static_cast<std::streamoff>(detail::kFileLimit))
			{
				LOG_WARN("GSC", "invalid file size: %ls; keeping native script", path.c_str());
				return false;
			}
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
			{
				LOG_WARN("GSC", "incomplete read: %ls; keeping native script", path.c_str());
				return false;
			}
			detail::ScriptData custom;
			if (!detail::Parse(bytes, custom))
			{
				LOG_WARN("GSC", "invalid gscbin header/payload: %ls; keeping native script", path.c_str());
				return false;
			}
			// Only gsc-tool's serialized marker denotes its unpacked IW8 format.
			// Native asset snapshots carry their asset name and are already packed.
			if (bytes.size() >= 4 && std::memcmp(bytes.data(), "GSC\0", 4) == 0)
			{
				const auto code_offset = static_cast<std::size_t>(custom.bytecode.data() - bytes.data());
				std::size_t converted{};
				if (!detail::PackToolOffsets(std::span(bytes).subspan(code_offset), converted))
				{
					LOG_WARN("GSC", "unsupported or invalid gsc-tool bytecode: %ls; keeping native script", path.c_str());
					return false;
				}
				LOG_INFO("GSC", "packed %zu gsc-tool relative operands for Replay: %s", converted, name.c_str());
			}

			if (!Writable(script, sizeof(*script)))
			{
				LOG_WARN("GSC", "non-writable ScriptFile header: %s; keeping native script", name.c_str());
				return false;
			}

			bool tail = false;
			if (detail::Fits(native, custom))
			{
				if (!Writable(const_cast<char*>(native.buffer_), custom.compressed.size()) ||
					!Writable(native.bytecode_, custom.bytecode.size()))
				{
					LOG_WARN("GSC", "non-writable native ScriptFile storage: %s; keeping native script", name.c_str());
					return false;
				}
				detail::Apply(*script, custom);
			}
			else
			{
				const auto compressed = custom.compressed.size();
				const auto bytecode = custom.bytecode.size();
				if (compressed > SIZE_MAX - bytecode) return false;
				const auto total = compressed + bytecode;
				auto* storage = saved.tail_storage;
				if (!storage || saved.tail_capacity < total || !ProgramRangeContains(reinterpret_cast<std::uintptr_t>(storage), total) ||
					!Writable(storage, total))
				{
					storage = AllocateBlock6Tail(saved, total);
					if (storage)
					{
						saved.tail_storage = storage;
						saved.tail_capacity = total;
					}
				}
				if (!storage)
				{
					LOG_WARN("GSC",
						"replacement exceeds native payload and verified block-6 tail is unavailable: %ls compressed=%d/%zu bytecode=%d/%zu; keeping native script",
						path.c_str(), custom.compressed_len, saved.compressed.size(), custom.bytecode_len, saved.bytecode.size());
					return false;
				}
				std::memcpy(storage, custom.compressed.data(), compressed);
				std::memcpy(storage + compressed, custom.bytecode.data(), bytecode);
				script->buffer_ = reinterpret_cast<const char*>(storage);
				script->bytecode_ = storage + compressed;
				script->compressed_len_ = custom.compressed_len;
				script->len_ = custom.len;
				script->bytecode_len_ = custom.bytecode_len;
				tail = true;
			}

			saved.Applied(*script);
			PublishAliasLocked(stable, name, saved);
			replacements.fetch_add(1, std::memory_order_relaxed);
			LOG_INFO("GSC", "injected before native link: %s compressed=%d bytecode=%d storage=%s", name.c_str(), custom.compressed_len,
				custom.bytecode_len, tail ? "native-block6-tail" : "native-payload");
			return true;
		}

#ifdef IW8X_ISLAND_PORT_PROBE
		void capture_just_streamed(const ScriptFile& native, const std::string& name, const SavedScript& saved,
			const bool linking = false) noexcept
		{
			struct error_scope
			{
				DWORD previous{ GetLastError() };
				~error_scope()
				{
					SetLastError(previous);
				}
			} restore_error;
			char enabled[2]{};
			if (GetEnvironmentVariableA(linking ? "IW8X_ISLAND_CAPTURE_LINKS" : "IW8X_ISLAND_CAPTURE_SCRIPTS", enabled,
				sizeof(enabled)) != 1 || enabled[0] != '1') return;
			try
			{
				// Both observers hold saved_mutex; each stream/link gets its own file.
				static std::uint64_t sequence{};
				static std::size_t captures{};
				constexpr std::size_t max_files = 4096;
				if (captures >= max_files) return;
				char raw_name[detail::kNameLimit]{};
				const auto name_length = safemem::ReadString(native.name_, raw_name, sizeof(raw_name));
				char terminator = 1;
				if (!name_length || name_length >= sizeof(raw_name) - 1 ||
					!safemem::ReadBytes(native.name_ + name_length, &terminator, 1) || terminator)
				{
					LOG_WARN("GSC", "native script capture could not read name: %s", name.c_str());
					return;
				}
				const std::int32_t lengths[]{ native.compressed_len_, native.len_, native.bytecode_len_ };
				if (name_length + 1 + sizeof(lengths) + saved.Size() > detail::kFileLimit)
				{
					LOG_WARN("GSC", "native script capture exceeds file limit: %s", name.c_str());
					return;
				}
				const auto directory = std::filesystem::current_path() / L"magma" /
					(linking ? L"island-probe-links" : L"island-probe-scripts") /
					std::to_string(GetCurrentProcessId());
				std::filesystem::create_directories(directory);
				auto filename = name;
				std::replace(filename.begin(), filename.end(), '/', '_');
				std::filesystem::path path;
				HANDLE file = INVALID_HANDLE_VALUE;
				do
				{
					path = directory / (std::to_string(++sequence) + "_" + filename + ".gscbin");
					file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
					{
						LOG_WARN("GSC", "native script capture could not create file: %ls error=%lu", path.c_str(), GetLastError());
						return;
					}
				} while (file == INVALID_HANDLE_VALUE);
				++captures;
				const auto write = [file](const void* data, const std::size_t size)
				{
					DWORD written{};
					return WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
				};
				// Serialize the original name and lengths, followed by the untouched snapshot vectors.
				const bool complete = write(raw_name, name_length + 1) && write(lengths, sizeof(lengths)) &&
					write(saved.compressed.data(), saved.compressed.size()) && write(saved.bytecode.data(), saved.bytecode.size());
				const bool closed = CloseHandle(file) != 0;
				if (!complete || !closed)
				{
					if (closed) (void)DeleteFileW(path.c_str());
					LOG_WARN("GSC", "native script capture incomplete: %ls", path.c_str());
				}
			}
			catch (const std::exception& error)
			{
				LOG_WARN("GSC", "native script capture failed: %s; keeping native behavior", error.what());
			}
			catch (...)
			{
				LOG_WARN("GSC", "native script capture failed; keeping native behavior");
			}
		}

		void capture_link_script(ScriptFile* script)
		{
			char enabled[2]{};
			if (GetEnvironmentVariableA("IW8X_ISLAND_CAPTURE_LINKS", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return;
			ScriptFile native{};
			std::string name;
			if (!ReadIdentity(script, native, name) || native.compressed_len_ <= 0 || native.len_ <= 0 || native.bytecode_len_ <= 0 ||
				static_cast<std::size_t>(native.compressed_len_) + native.bytecode_len_ > detail::kFileLimit) return;
			SavedScript saved;
			saved.compressed.resize(native.compressed_len_);
			saved.bytecode.resize(native.bytecode_len_);
			if (!safemem::ReadBytes(native.buffer_, saved.compressed.data(), saved.compressed.size()) ||
				!safemem::ReadBytes(native.bytecode_, saved.bytecode.data(), saved.bytecode.size())) return;
			std::lock_guard lock(saved_mutex);
			capture_just_streamed(native, name, saved, true);
		}
#endif

		void ReplaceJustStreamed(ScriptFile* script)
		{
			ScriptFile native{};
			std::string name;
			if (!ReadIdentity(script, native, name)) return;
			std::lock_guard lock(saved_mutex);
			ForgetStorage(native); // address reuse starts a new DB generation
			if (native.compressed_len_ <= 0 || native.len_ <= 0 || native.bytecode_len_ <= 0 || native.len_ > detail::kFileLimit) return;
			const auto compressed = static_cast<std::size_t>(native.compressed_len_);
			const auto code = static_cast<std::size_t>(native.bytecode_len_);
			if (compressed > detail::kFileLimit || code > detail::kFileLimit || compressed + code > detail::kFileLimit ||
				compressed + code > kSnapshotLimit - saved_bytes || !Writable(script, sizeof(*script)) ||
				!Writable(const_cast<char*>(native.buffer_), compressed) || !Writable(native.bytecode_, code))
			{
				LOG_WARN("GSC", "cannot retain pristine DB storage: %s; keeping native script", name.c_str());
				return;
			}
			SavedScript saved;
			saved.compressed.resize(compressed);
			saved.bytecode.resize(code);
			if (!safemem::ReadBytes(native.buffer_, saved.compressed.data(), compressed) ||
				!safemem::ReadBytes(native.bytecode_, saved.bytecode.data(), code))
				return;
			saved.len = native.len_;
			saved.native_buffer = native.buffer_;
			saved.native_bytecode = native.bytecode_;
			saved.Applied(native);
#ifdef IW8X_ISLAND_PORT_PROBE
			if (script == streaming_script) capture_just_streamed(native, name, saved);
#endif
			const auto stable = Key(native, name);
			auto [entry, inserted] = saved_scripts.emplace(stable, std::move(saved));
			if (!inserted) return;
#ifdef IW8X_ISLAND_PORT_PROBE
			if (canonical_delta && script == streaming_script) probe_latest_scripts[name] = stable;
#endif
			saved_bytes += compressed + code;
			(void)ApplyDiskOverride(script, name, stable, entry->second);
		}

		void RefreshForLink(ScriptFile* script, const std::uint64_t epoch)
		{
			ScriptFile current{};
			std::string name;
			if (!ReadIdentity(script, current, name)) return;
			std::unique_lock lock(saved_mutex);
			auto entry = FindSavedLocked(current, name);
			if (entry == saved_scripts.end())
			{
				// DB can register a different payload allocation from the temporary
				// streaming view. The native linker supplies the authoritative asset.
				// Capture it only at a proven full load with no executing VM frames.
				std::error_code error;
				if (!std::filesystem::is_regular_file(OverridePath(name), error)) return;
				LOG_INFO("GSC", "capturing registered script for override: %s header=%p bytecode=%p bytes=%d",
					name.c_str(), script, current.bytecode_, current.bytecode_len_);
				lock.unlock();
				ReplaceJustStreamed(script);
				lock.lock();
				if (!ReadIdentity(script, current, name)) return;
				entry = FindSavedLocked(current, name);
			}
			if (entry == saved_scripts.end()) return; // no proven allocation/snapshot
			auto& saved = entry->second;
			if (saved.linked_epoch == epoch) return;
			// DB registration may have copied the original header before the
			// streaming hook redirected an oversized replacement into block 6.
			// The linker supplies the registered header, so accept either proven
			// view and publish the replacement into that header before linking.
			const bool native_view = current.buffer_ == saved.native_buffer && current.bytecode_ == saved.native_bytecode &&
				current.compressed_len_ == static_cast<int>(saved.compressed.size()) && current.len_ == saved.len &&
				current.bytecode_len_ == static_cast<int>(saved.bytecode.size());
			if ((!AppliedViewMatches(current, saved) && !native_view) || !Writable(script, sizeof(*script)))
			{
				LOG_WARN("GSC", "resident storage changed outside native GSC hooks: %s; refresh skipped", name.c_str());
				return;
			}
			// Full native VM/program teardown has completed. Restore all original
			// operands before letting the engine link this currently registered asset.
			const auto stable = Key(saved.native_buffer, saved.native_bytecode, name);
			if (!RestorePristine(script, saved))
			{
				LOG_WARN("GSC", "native ScriptFile storage is no longer writable: %s; refresh skipped", name.c_str());
				return;
			}
			RemoveAliasesLocked(stable);
			ReleaseBlock6Tail(saved);
			saved.Applied(*script);
			saved.linked_epoch = epoch;
			restored_files.fetch_add(1, std::memory_order_relaxed);
			if (ApplyDiskOverride(script, name, stable, saved)) // reread disk, including additions/removals
				LOG_INFO("GSC", "registered override selected for native link: %s header=%p bytecode=%p bytes=%d tid=%lu",
					name.c_str(), script, script->bytecode_, script->bytecode_len_, GetCurrentThreadId());
		}

		void LinkScriptFile(void* context, ScriptFile* script)
		{
			if (!installed.load(std::memory_order_acquire))
			{
				original_link_file(context, script);
				return;
			}
			std::uint8_t loading{};
			std::uint32_t frames{};
			const auto epoch = link_epoch.load(std::memory_order_acquire);
			const auto* server = game::at<std::uint8_t>(kServerContextRva);
			if (epoch && link_gate_samples.fetch_add(1, std::memory_order_relaxed) < 3)
			{
				(void)safemem::ReadBytes(server + kLoadingScriptsOffset, &loading, sizeof(loading));
				(void)safemem::ReadBytes(server + kVMFrameCountOffset, &frames, sizeof(frames));
				LOG_INFO("GSC", "native link boundary: context=%p server=%p epoch=%llu loading=%u frames=%u tid=%lu",
					context, server, static_cast<unsigned long long>(epoch), unsigned(loading), frames, GetCurrentThreadId());
			}
			if (epoch && context == server && safemem::ReadBytes(server + kLoadingScriptsOffset, &loading, sizeof(loading)) &&
				loading == 1 && safemem::ReadBytes(server + kVMFrameCountOffset, &frames, sizeof(frames)) && !frames)
			{
				try
				{
					RefreshForLink(script, epoch);
				}
				catch (const std::exception& error)
				{
					LOG_WARN("GSC", "script refresh failed: %s", error.what());
				}
			}
			ScriptFile* compiler_script = script;
#ifdef IW8X_ISLAND_PORT_PROBE
			ScriptFile probe_view{};
			// Native DB priority can retain a stock ScriptFile despite loading a
			// newer same-name companion. In this explicit isolated profile, supply
			// the last streamed native block-6 payload to the compiler only. Keep
			// registered DB headers and their stock lifetime/release paths intact.
			const bool island_load = epoch && context == server && loading == 1 && !frames && IslandScriptLoad();
			if (island_load)
			{
				std::string name;
				if (ReadIdentity(script, probe_view, name))
				{
					std::lock_guard lock(saved_mutex);
					const auto latest = probe_latest_scripts.find(name);
					if (latest != probe_latest_scripts.end() && latest->second != Key(probe_view, name))
					{
						const auto selected = saved_scripts.find(latest->second);
						if (selected != saved_scripts.end())
						{
							auto& saved = selected->second;
							if (ProgramRangeContains(reinterpret_cast<std::uintptr_t>(saved.native_buffer), saved.compressed.size()) &&
								ProgramRangeContains(reinterpret_cast<std::uintptr_t>(saved.native_bytecode), saved.bytecode.size()))
							{
								RestoreNativeHeader(&probe_view, saved);
								if (saved.linked_epoch == epoch || RestorePristine(&probe_view, saved))
								{
									saved.linked_epoch = epoch;
									compiler_script = &probe_view;
									LOG_INFO("GSC", "isolated companion compiler view: %s bytecode=%p bytes=%d", name.c_str(),
										probe_view.bytecode_, probe_view.bytecode_len_);
								}
							}
						}
					}
				}
			}
			// Record the actual selected compiler input, including a private view.
			if (island_load)
			{
				probe_view = *compiler_script;
				if (!CompactCompilerCode(probe_view, epoch))
				{
					reinterpret_cast<void (*)(int, const char*, ...)>(game::base() + kComErrorRva)(1,
						"Isolated script bytecode exceeds or lacks its native signed24 code reservation");
					return;
				}
					// Keep a read-only map from ACTUAL compacted runtime PCs back
					// to their selected ScriptFile, including stock-only modules.
					// This has no effect on bytecode, linker, or game callbacks.
					ScriptFile original_identity{};
					std::string selected_name;
					if (ReadIdentity(compiler_script, original_identity, selected_name))
					{
						const auto first = reinterpret_cast<std::uintptr_t>(probe_view.bytecode_);
						const auto size = static_cast<std::size_t>(probe_view.bytecode_len_);
						// The registered stock BR quest header is only 15,661 bytes,
						// whereas the genuine Island companion supplies 20,617 bytes.
						// Translate ONLY its verified private compiler copy, never the
						// registered stock asset, source archive, or shared script.
						if (selected_name == "scripts/mp/gametypes/br_quest_util.gsc")
						{
							char enabled[2]{};
							if (GetEnvironmentVariableA("IW8X_ISLAND_QUEST_NATIVE_REBIND", enabled, sizeof(enabled)) == 1 &&
								enabled[0] == '1')
							{
									bool incompatible = false;
									bool newly_translated = false;
									{
										std::lock_guard lock(saved_mutex);
										if (!island_quest_rebound_code.contains(first))
								{
									const auto copy = std::span(probe_view.bytecode_, size);
									if (!island_quest_native_abi::translate_private_compiler_copy(copy))
											incompatible = true;
										else
										{
											island_quest_rebound_code.insert(first);
											newly_translated = true;
										}
										}
									}
									// Native Com_Error can unwind/longjmp engine script loading;
									// never call it while our GSC mutex is held.
									if (incompatible)
									{
										reinterpret_cast<void (*)(int, const char*, ...)>(game::base() + kComErrorRva)(1,
											"Island quest native ABI: actual companion bytecode differs from original verified seven-callsite source");
										return;
								}
									if (newly_translated)
										LOG_INFO("GSC", "Island quest native ABI: translated 7 original selected compiler-view calls at %p, original assets untouched", probe_view.bytecode_);
							}
						}
						const std::lock_guard lock(saved_mutex);
						probe_code_names.try_emplace(first, size, std::move(selected_name));
					}
				compiler_script = &probe_view;
			}
			capture_link_script(compiler_script);
#endif
			const auto previous_source = link_diagnostics::Begin(compiler_script);
			original_link_file(context, compiler_script);
			link_diagnostics::End(previous_source);
			linked_files.fetch_add(1, std::memory_order_relaxed);
		}

		void ReleaseXAsset(void* header, unsigned type, unsigned release_type)
		{
			if (installed.load(std::memory_order_acquire) && type == kScriptFileAssetType)
			{
				ScriptFile current{};
				if (safemem::ReadBytes(header, &current, sizeof(current)))
				{
					std::lock_guard lock(saved_mutex);
					std::string name;
					ScriptFile live{};
					if (ReadIdentity(static_cast<ScriptFile*>(header), live, name))
					{
						const auto entry = FindSavedLocked(live, name);
						if (entry != saved_scripts.end())
						{
							const auto stable = Key(entry->second.native_buffer, entry->second.native_bytecode, name);
							const bool restored = AppliedViewMatches(live, entry->second) && Writable(header, sizeof(ScriptFile));
							if (restored)
								RestoreNativeHeader(static_cast<ScriptFile*>(header), entry->second);
							else if (entry->second.tail_storage)
							{
								// The live header is not in the view we published. Do not recycle
								// its tail slice into another script while an unknown pointer may
								// still reference it; zone teardown will reclaim the native pages.
								entry->second.tail_storage = nullptr;
								entry->second.tail_capacity = 0;
							}
							EraseSavedLocked(stable);
						}
						else
						{
							ForgetStorage(current);
						}
					}
					else
					{
						ForgetStorage(current);
					}
				}
			}
			original_release_asset(header, type, release_type);
		}

		int ProcessMapRestart()
		{
			std::uint8_t pending{}, savegame{};
			if (installed.load(std::memory_order_acquire) && safemem::ReadBytes(game::at(kRestartPendingRva), &pending, 1) && pending &&
				safemem::ReadBytes(game::at(kRestartSavegameRva), &savegame, 1) && !savegame &&
				Writable(game::at(kRestartLoadScriptsRva), 1))
			{
				*game::at<std::uint8_t>(kRestartLoadScriptsRva) = 1;
				LOG_INFO("GSC", "map restart selected native full server/script reload");
				iw8x::gsc::map_restart();
			}
			return original_process_restart();
		}

		void LoadScriptFile(int stream_start)
		{
			// Preserve the complete native streaming sequence and byte consumption.
			// Never reread XFile or advance stream positions by replacement sizes.
			original_load_file(stream_start);
			if (!installed.load(std::memory_order_acquire)) return;
			stream_loads.fetch_add(1, std::memory_order_relaxed);
			ScriptFile* script = nullptr;
			if (!safemem::ReadBytes(game::at(kVarScriptFileRva), &script, sizeof(script)) || !script) return;
			const auto previous = streaming_script;
			streaming_script = script;
			reload_script_file(script);
			streaming_script = previous;
		}

		void PostloadScriptFile(int stream_start)
		{
			// Preserve the complete native streaming sequence and byte consumption.
			// Never reread XFile or advance stream positions by replacement sizes.
			original_postload_file(stream_start);
			if (!installed.load(std::memory_order_acquire)) return;
			stream_loads.fetch_add(1, std::memory_order_relaxed);
			ScriptFile* script = nullptr;
			if (!safemem::ReadBytes(game::at(kVarScriptFileRva), &script, sizeof(script)) || !script) return;
			const auto previous = streaming_script;
			streaming_script = script;
			reload_script_file(script);
			streaming_script = previous;
		}

		struct ResetState
		{
			std::uint32_t objects[3]{};
			std::uint32_t program_length{}, program_checksum{}, vm_frames{};
			std::uintptr_t program{};
			std::uint8_t loading{};
		};

		bool ReadResetState(ResetState& state)
		{
			const auto* context = game::at<std::uint8_t>(kServerContextRva);
			return safemem::ReadBytes(context + kCompileObjectsOffset, state.objects, sizeof(state.objects)) &&
				safemem::ReadBytes(context + kProgramLengthOffset, &state.program_length, sizeof(state.program_length)) &&
				safemem::ReadBytes(context + kProgramChecksumOffset, &state.program_checksum, sizeof(state.program_checksum)) &&
				safemem::ReadBytes(context + kProgramBufferOffset, &state.program, sizeof(state.program)) &&
				safemem::ReadBytes(context + kVMFrameCountOffset, &state.vm_frames, sizeof(state.vm_frames)) &&
				safemem::ReadBytes(context + kLoadingScriptsOffset, &state.loading, sizeof(state.loading));
		}

		bool Empty(const ResetState& state)
		{
			return !state.objects[0] && !state.objects[1] && !state.objects[2] && !state.program_length && !state.program_checksum &&
				!state.program && !state.vm_frames && !state.loading;
		}

		void ShutdownScripts(bool full_clear)
		{
			if (!installed.load(std::memory_order_acquire))
			{
				original_shutdown(full_clear);
				return;
			}
			// This entry is G_MainMP_ShutdownGame, the real MP game teardown owner;
			// the MAP_SHUTDOWN event is independent of the script events below.
			const uint8_t* persistent{};
			uint8_t frontend = 1;
			if (safemem::ReadBytes(game::at(game::addresses::gsc_api::persistent_globals), &persistent, sizeof(persistent)) && persistent &&
				safemem::ReadBytes(persistent + game::addresses::gsc_api::persistent_frontend, &frontend, sizeof(frontend)) && !frontend)
			{
				iw8x::gsc::map_shutdown();
				iw8x::gsc::scripts_shutdown();
			}
			link_epoch = 0;
			reset_ready.store(false, std::memory_order_release);
			original_shutdown(full_clear);
			ResetState state;
			const bool readable = ReadResetState(state);
			LOG_INFO("GSC", "native shutdown returned: full_clear=%d observed_program_vm_empty=%d readable=%d", int(full_clear),
				int(readable && Empty(state)), int(readable));
			// Require both the complete native full-clear path and its postconditions.
			// Individual zero fields never authorize an isolated low-level reset.
			reset_ready.store(full_clear && readable && Empty(state), std::memory_order_release);
		}

		void LoadScripts(const void* settings)
		{
			if (!installed.load(std::memory_order_acquire))
			{
				original_load_scripts(settings);
				return;
			}
			ResetState state;
			const bool readable = ReadResetState(state);
			const auto loads = stream_loads.exchange(0, std::memory_order_relaxed);
			const auto injected = replacements.exchange(0, std::memory_order_relaxed);
			const bool cleared = reset_ready.exchange(false, std::memory_order_acq_rel);
			const bool refresh = readable && Empty(state) && (cleared || !ever_loaded.load(std::memory_order_acquire));

			struct Scope
			{
				std::uint64_t previous = link_epoch.load(std::memory_order_acquire);

				~Scope()
				{
					link_epoch.store(previous, std::memory_order_release);
				}
			} scope;

			link_epoch.store(refresh ? ++next_epoch : 0, std::memory_order_release);
			link_gate_samples.store(0, std::memory_order_relaxed);
			ever_loaded.store(true, std::memory_order_release);
			const auto restored_before = restored_files.load(std::memory_order_relaxed);
			const auto linked_before = linked_files.load(std::memory_order_relaxed);
			LOG_INFO("GSC", "native script load: db_script_loads=%llu replacements=%llu observed_program_vm_empty=%d refresh=%d",
				static_cast<unsigned long long>(loads), static_cast<unsigned long long>(injected), int(readable && Empty(state)),
				int(refresh));
			const bool gameplay = iw8x::gsc::internal::gameplay_server();
			if (gameplay)
			{
				iw8x::gsc::server_starting();
				iw8x::gsc::map_starting();
				iw8x::gsc::scripts_begin_load();
			}
			original_load_scripts(settings);
			if (gameplay) iw8x::gsc::scripts_loaded();
			LOG_INFO("GSC", "native script load returned: pristine_restores=%llu native_links=%llu",
				static_cast<unsigned long long>(restored_files.load(std::memory_order_relaxed) - restored_before),
				static_cast<unsigned long long>(linked_files.load(std::memory_order_relaxed) - linked_before));
#ifdef IW8X_ISLAND_PORT_PROBE
			if (canonical_delta && refresh)
				LOG_INFO("GSC", "isolated compact code linked: scripts=%zu bytes=%zu capacity=%zu", probe_code_views.size(),
					probe_code_used, probe_code_capacity);
#endif
		}

	} // namespace

#ifdef IW8X_ISLAND_PORT_PROBE
		bool DescribeIslandCodeSite(std::uintptr_t pc, std::string& script, std::size_t& byte_offset)
		{
			const std::lock_guard lock(saved_mutex);
			if (!probe_code_begin || probe_code_names.empty()) return false;
			const auto next = probe_code_names.upper_bound(pc);
			if (next == probe_code_names.begin()) return false;
			const auto entry = std::prev(next);
			const auto first = entry->first;
			const auto& [length, name] = entry->second;
			if (pc < first || pc - first >= length) return false;
			script = name;
			byte_offset = pc - first;
			return true;
		}
#endif

	void reload_script_file(ScriptFile* script_file)
	{
		if (!script_file || script_file != streaming_script)
		{
			LOG_WARN("GSC", "reload requires native DB streaming; active/resident ScriptFile was not modified");
			return;
		}
		try
		{
			ReplaceJustStreamed(script_file);
		}
		catch (const std::exception& error)
		{
			LOG_WARN("GSC", "custom script read failed: %s; keeping native script", error.what());
		}
	}

	void ReapplyCustomScript(const std::string& name)
	{
		LOG_INFO("GSC", "'%s' will be reread during the next native DB load or full map/script reload", name.c_str());
	}

	hook::Status Install(std::uintptr_t module_base)
	{
		std::lock_guard lock(install_mutex);
		if (installed.load(std::memory_order_acquire)) return hook::Status::Installed;
		if (!game::initialized() || !module_base || module_base != game::base()) return hook::Status::Failed;
		HMODULE self{};
		wchar_t path[MAX_PATH]{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&Install), &self))
			return hook::Status::Failed;
		const auto length = GetModuleFileNameW(self, path, MAX_PATH);
		if (!length || length >= MAX_PATH) return hook::Status::Failed;
		override_root = std::filesystem::path(path).parent_path() / L"IW8x" / L".gsc";

		struct Binding
		{
			uintptr_t rva;
			std::span<const uint8_t> bytes;
			void* callback;
		};
#define SITE(rva, name, callback) Binding{rva, name##_entry, reinterpret_cast<void*>(&callback)}
		const Binding entries[]{ SITE(kLoadScriptFileRva, Load_ScriptFile, LoadScriptFile),
			SITE(kLoadScriptsRva, GScr_MainMP_LoadScripts, LoadScripts), SITE(kShutdownScriptsRva, G_MainMP_ShutdownGame, ShutdownScripts),
			SITE(kLinkScriptFileRva, LinkScriptFile, LinkScriptFile), SITE(kProcessMapRestartRva, ProcessMapRestart, ProcessMapRestart),
			SITE(kAllocZoneMemoryRva, AllocZoneMemory, AllocZoneMemory),
			SITE(kPostloadScriptFileRva, Postload_ScriptFile, PostloadScriptFile), SITE(kReleaseXAssetRva, ReleaseXAsset, ReleaseXAsset)
#ifdef IW8X_ISLAND_PORT_PROBE
			, Binding{ 0x1316bd0, canonical_name_entry, reinterpret_cast<void*>(&canonical_name) }
			, Binding{ 0x11b2220, asset_exists_entry, reinterpret_cast<void*>(&asset_exists) }
			, Binding{ 0x11aa890, find_asset_entry, reinterpret_cast<void*>(&find_asset) }
			, Binding{ 0x11a9160, fixup_preload_entry, reinterpret_cast<void*>(&FixupPreloadScriptMemory) }
			, Binding{ 0x11a9240, free_zone_entry, reinterpret_cast<void*>(&FreeZoneMemory) }
#endif
		};
#undef SITE
		std::lock_guard hook_lock(hook::detail::Mutex());
		for (size_t i = 0; i < std::size(entries); ++i)
		{
			if (trampolines[i]) continue;
			const auto& entry = entries[i];
			if (entry.rva >= game::image_size() || entry.bytes.size() > game::image_size() - entry.rva) return hook::Status::Failed;
			targets[i] = game::at(entry.rva);
			const auto status = hook::detail::Prepare(targets[i], entry.callback, entry.bytes.data(), entry.bytes.size(), &trampolines[i]);
			if (status != hook::Status::Installed) return status;
		}
		original_load_file = reinterpret_cast<LoadScriptFileFn>(trampolines[0]);
		original_load_scripts = reinterpret_cast<LoadScriptsFn>(trampolines[1]);
		original_shutdown = reinterpret_cast<ShutdownScriptsFn>(trampolines[2]);
		original_link_file = reinterpret_cast<LinkScriptFileFn>(trampolines[3]);
		original_process_restart = reinterpret_cast<ProcessMapRestartFn>(trampolines[4]);
		original_alloc_zone_memory = reinterpret_cast<AllocZoneMemoryFn>(trampolines[5]);
		original_postload_file = reinterpret_cast<LoadScriptFileFn>(trampolines[6]);
		original_release_asset = reinterpret_cast<ReleaseXAssetFn>(trampolines[7]);
#ifdef IW8X_ISLAND_PORT_PROBE
		original_canonical_name = reinterpret_cast<canonical_name_fn>(trampolines[8]);
		original_asset_exists = reinterpret_cast<asset_exists_fn>(trampolines[9]);
		original_find_asset = reinterpret_cast<find_asset_fn>(trampolines[10]);
		original_fixup_preload = reinterpret_cast<fixup_preload_fn>(trampolines[11]);
		original_free_zone = reinterpret_cast<free_zone_fn>(trampolines[12]);
		char numeric_namespace[2]{};
		canonical_delta = GetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", numeric_namespace, sizeof(numeric_namespace)) == 1 &&
			numeric_namespace[0] == '1' ? 0x1472f - 0xe2c0 : 0;
		if (canonical_delta) LOG_INFO("GSC", "isolated numeric namespace probe: original fixed IDs retained; dynamic base=0x1472F");
		if (canonical_delta) island_builtins::install();
		char memory_dependency[2]{};
		source_script_memory = canonical_delta && GetEnvironmentVariableA("IW8X_ISLAND_SOURCE_SCRIPT_MEMORY",
			memory_dependency, sizeof(memory_dependency)) == 1 && memory_dependency[0] == '1';
		if (source_script_memory) LOG_INFO("GSC", "isolated original script_memory.json dependency available before native memory initialization");
#endif
		get_script_program_range = reinterpret_cast<GetScriptProgramRangeFn>(game::at(kGetScriptProgramRangeRva));
		if (!hook::detail::EnableBatch(targets)) return hook::Status::Failed;
		installed.store(true, std::memory_order_release);
		release_installed.store(true, std::memory_order_release);
		LOG_INFO("GSC", "Replay native DB load/postload, linker, release and full reload installed: %ls", override_root.c_str());
		return hook::Status::Installed;
	}

	hook::Status InstallReleaseHook(std::uintptr_t module_base)
	{
		// This dispatcher is distinct from BAP's free-entry hooks. Installation
		// includes it atomically; bootstrap still calls this compatibility gate.
		return module_base == game::base() && release_installed.load(std::memory_order_acquire) ? hook::Status::Installed
			: hook::Status::Failed;
	}

	void Component_Stage_Unpack()
	{
		(void)Install(game::base());
	}

	void Component_Load()
	{
	}
} // namespace gsc
