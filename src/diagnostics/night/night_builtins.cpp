#include "diagnostics/night/night_builtins.h"
#include "diagnostics/night/night_files.h"
#include "gsc/internal/port_tables.hpp"
#include "diagnostics/night/night_builtin_bindings.h"
#include "diagnostics/night/night_playlist_bindings.h"
#include "diagnostics/night/night_entity_bindings.h"
#include "diagnostics/night/night_high_lod_bindings.h"
#include "diagnostics/night/night_high_lod.h"
#include "diagnostics/night/night_music.h"
#include "diagnostics/night/night_music_bindings.h"
#include "diagnostics/night/night_builtin_tables.h"
#include "diagnostics/escape/escape_builtins.h"
#include "diagnostics/night/night_movers.h"
#include "common/console.h"
#include "network/native_network.h"
#include "engine/safemem.h"
#include <windows.h>
#include <cstdarg>
#include <mutex>
#include <cstdlib>
#include <cstdio>
namespace night_builtins
{
	namespace
	{
		namespace binding = night_builtin_binding;
		constexpr uintptr_t NativeFunctions = 0xD5B7350, NativeMethods = 0xD5B9410;
		uintptr_t g_base{};
		Tables g_tables;
		std::mutex g_lock;
		bool g_configured = false, g_installed = false;
		using InitializeFn = void (*)(void*);
		using RegisterFn = std::uint64_t (*)(void*, uintptr_t, uintptr_t, const Descriptor*);
		std::atomic<InitializeFn> g_initialize{};
		std::atomic<RegisterFn> g_register{}, g_alternate{};
		using CollectFn = void (*)();
		std::atomic<CollectFn> g_collectHighLod{};
		using CommandFn = bool (*)(int, char);
		std::atomic<CommandFn> g_singleCommand{};

		auto* native_function_table()
		{
			return reinterpret_cast<uintptr_t*>(g_base + NativeFunctions);
		}

		auto* native_method_table()
		{
			return reinterpret_cast<uintptr_t*>(g_base + NativeMethods);
		}

		void fatal(const char* message)
		{
			LOG_ERR("NightBuiltins", "%s", message);
			const auto dispatch = [](const char* format, ...)
			{
				va_list args;
				va_start(args, format);
				reinterpret_cast<void (*)(int, const char*, va_list)>(g_base + binding::Fatal.rva)(0, format, args);
				va_end(args);
			};
			dispatch("%s", message);
		}

		void verify_bunker_code(void* context)
		{
			// Original 1.28 208A940 -> 224CF10 pushes integer zero, unconditionally.
			// This preserves that original implementation; it is not a generic stub.
			reinterpret_cast<void (*)(void*, int)>(g_base + binding::AddBool.rva)(context, 0);
		}

		void alt_bunker_index(void* context)
		{
			// Original 1.28 208A950 -> 224D0F0 pushes integer minus one.
			reinterpret_cast<void (*)(void*, int)>(g_base + binding::AddInt.rva)(context, -1);
		}

		void validate_loadout_ownership(void* context, std::uint64_t reference)
		{
			// The complete original 1.28 202E4C0..202E500 implementation validates
			// the player handle and returns. It does not read the supplied loadout,
			// change inventory, or push a result. Preserve that actual behavior.
			const auto objectError = [context](unsigned code, const char* message)
			{
				reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + binding::ObjectError.rva)(code, context, message);
			};
			if ((reference >> 32) & 0xFF)
			{
				objectError(0xE61, "ValidateLoadoutOwnership must be called on an entity");
				return;
			}
			const auto entity = reinterpret_cast<uintptr_t (*)(std::uint64_t)>(g_base + binding::GetEntity.rva)(reference);
			uintptr_t client{};
			if (!entity || !safemem::ReadBytes(reinterpret_cast<const void*>(entity + 0x150), &client, sizeof(client)))
			{
				fatal("Night could not read the native player entity during loadout validation");
				return;
			}
			if (!client) objectError(0xE60, "ValidateLoadoutOwnership must be called on a player");
		}

		struct NativePlayerMusic
		{
			void* context;

			void ObjectError(unsigned code)
			{
				reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + binding::ObjectError.rva)(
					code, context, "StopPlayerMusicState requires a valid player or agent");
			}

			void ScriptError(unsigned code)
			{
				reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + binding::ScriptError.rva)(
					code, context, "StopPlayerMusicState requires one music-state name and a valid client index");
			}

			bool PlayerOrAgent(std::uint64_t reference, int& number)
			{
				const auto entity = reinterpret_cast<uintptr_t (*)(std::uint64_t)>(g_base + binding::GetEntity.rva)(reference);
				uintptr_t client{}, agent{};
				std::int16_t index{};
				if (!entity || !safemem::ReadBytes(reinterpret_cast<void*>(entity + 0x150), &client, sizeof(client)) ||
					!safemem::ReadBytes(reinterpret_cast<void*>(entity + 0x168), &agent, sizeof(agent)) ||
					!safemem::ReadBytes(reinterpret_cast<void*>(entity), &index, sizeof(index)))
				{
					fatal("Night music could not read the native entity");
					return false;
				}
				number = index;
				return client || agent;
			}

			bool AgentMode()
			{
				return reinterpret_cast<int (*)()>(g_base + binding::AgentMode.rva)() != 0;
			}

			bool AgentClient(int& number)
			{
				return reinterpret_cast<bool (*)(int, int*)>(g_base + binding::AgentClient.rva)(number, &number);
			}

			int ClientCount()
			{
				int count{};
				if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xBC216C0), &count, sizeof(count)) || count < 0 || count > 200)
					fatal("Night music encountered an invalid native client count");
				return count;
			}

			unsigned NumParams()
			{
				return reinterpret_cast<unsigned (*)(void*)>(g_base + binding::NumParams.rva)(context);
			}

			std::uint32_t ArgumentHash()
			{
				const auto name = reinterpret_cast<const char* (*)(void*, unsigned)>(g_base + binding::GetString.rva)(context, 0);
				return reinterpret_cast<std::uint32_t (*)(const char*)>(g_base + binding::SoundHash.rva)(name);
			}

			void SendStop(int number, std::uint32_t hash)
			{
				char command[32]{};
				std::snprintf(command, sizeof(command), "p %x 1", hash);
				if (number == -1)
				{
					reinterpret_cast<void (*)(int, const char*)>(g_base + binding::BroadcastCommand.rva)(2, command);
					return;
				}
				uintptr_t client{}, vtable{}, send{};
				if (number < 0 || number >= 200 ||
					!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xE17F690 + number * 8ull), &client, sizeof(client)) || !client ||
					!safemem::ReadBytes(reinterpret_cast<void*>(client), &vtable, sizeof(vtable)) || !vtable ||
					!safemem::ReadBytes(reinterpret_cast<void*>(vtable + 8), &send, sizeof(send)) ||
					!safemem::RegionExecutable(reinterpret_cast<void*>(send)))
				{
					fatal("Night music could not resolve the native reliable-command recipient");
					return;
				}
				reinterpret_cast<void (*)(uintptr_t, int, const char*)>(send)(client, 2, command);
			}
		};

		void stop_player_music_state(void* context, std::uint64_t reference)
		{
			NativePlayerMusic service{ context };
			StopPlayerMusic(service, reference);
		}

		struct NativeMusicMixer
		{
			void Lock()
			{
				reinterpret_cast<void (*)(unsigned)>(g_base + binding::EnterCritical.rva)(0x32);
			}

			void Unlock()
			{
				reinterpret_cast<void (*)(unsigned)>(g_base + binding::LeaveCritical.rva)(0x32);
			}

			uintptr_t Find(std::uint32_t hash)
			{
				return reinterpret_cast<uintptr_t (*)(std::uint32_t)>(g_base + binding::FindMusicState.rva)(hash);
			}

			uintptr_t Requested()
			{
				return *reinterpret_cast<uintptr_t*>(g_base + 0x12B9DB58);
			}

			void Clear()
			{
				*reinterpret_cast<uintptr_t*>(g_base + 0x12B9DB58) = 0;
			}

			void Update()
			{
				reinterpret_cast<void (*)()>(g_base + binding::UpdateMusic.rva)();
			}
		};

		bool single_character_command(int localClient, char command)
		{
			if (command == 'p' && reinterpret_cast<int (*)()>(g_base + binding::CommandArgc.rva)() == 3)
			{
				const auto argv = reinterpret_cast<const char* (*)(int)>(g_base + binding::CommandArgv.rva);
				const auto hash = static_cast<std::uint32_t>(std::strtoul(argv(1), nullptr, 16));
				if (std::strtoul(argv(2), nullptr, 16) != 0)
				{
					NativeMusicMixer mixer;
					StopRequestedMusic(mixer, hash);
					return true;
				}
			}
			return g_singleCommand.load(std::memory_order_acquire)(localClient, command);
		}

		void force_netfield_high_lod(void* context, std::uint64_t reference)
		{
			const auto entity = reinterpret_cast<uintptr_t (*)(std::uint64_t)>(g_base + binding::GetEntity.rva)(reference);
			const auto value = reinterpret_cast<int (*)(void*, unsigned)>(g_base + binding::GetInt.rva)(context, 0);
			std::uint32_t flags{};
			auto* field = reinterpret_cast<void*>(entity + 0x1A4);
			if (!entity || !safemem::ReadBytes(field, &flags, sizeof(flags)))
			{
				fatal("Night could not read the entity flags for ForceNetfieldHighLod");
				return;
			}
			flags = value ? flags | 0x10u : flags & ~0x10u;
			if (!safemem::WriteBytes(field, &flags, sizeof(flags)))
				fatal("Night could not write the entity flags for ForceNetfieldHighLod");
		}

		bool readable_span(uintptr_t address, size_t size)
		{
			if (!address || address > UINTPTR_MAX - size) return false;
			while (size)
			{
				size_t room{};
				if (!safemem::RegionReadable(reinterpret_cast<const void*>(address), room) || !room) return false;
				const size_t n = room < size ? room : size;
				address += n;
				size -= n;
			}
			return true;
		}

		void collect_high_lod()
		{
			g_collectHighLod.load(std::memory_order_acquire)();
			// Source 1.28 adds entity+1A4 bit 4 to this exact native collector.
			// Both engines use the same visibility predicate. Preserve Replay's
			// existing selections and merge only the new explicitly forced entries.
			uintptr_t server{}, entities{};
			unsigned entityCount{}, nativeCount{};
			auto* nativeList = reinterpret_cast<std::uint16_t*>(g_base + 0xD5BD150);
			auto* countAddress = reinterpret_cast<void*>(g_base + 0xD5BE150);
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + 0xE17FE00), &server, sizeof(server)) || !server ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(server + 0x18), &entities, sizeof(entities)) ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(server + 0x20), &entityCount, sizeof(entityCount)) ||
				!safemem::ReadBytes(countAddress, &nativeCount, sizeof(nativeCount)) || entityCount > 2048 || nativeCount > 2048 ||
				(entityCount && !readable_span(entities, entityCount * 0x5A0ull)))
			{
				fatal("Night high-detail entity collector encountered an unverified native layout");
				return;
			}
			std::array<std::uint16_t, 2048> native{}, forced{}, merged{};
			unsigned forcedCount{}, mergedCount{};
			if (nativeCount && !safemem::ReadBytes(nativeList, native.data(), nativeCount * sizeof(native[0])))
			{
				fatal("Night could not read the native high-detail entity list");
				return;
			}
			for (unsigned i = 0; i < entityCount; ++i)
			{
				const auto entity = entities + i * 0x5A0ull;
				std::uint32_t flags{};
				std::memcpy(&flags, reinterpret_cast<const void*>(entity + 0x1A4), sizeof(flags));
				if (!(flags & 0x10u) || !reinterpret_cast<bool (*)(uintptr_t)>(g_base + binding::SnapshotEligible.rva)(entity)) continue;
				std::uint16_t number{};
				std::memcpy(&number, reinterpret_cast<const void*>(entity), sizeof(number));
				forced[forcedCount++] = number;
			}
			if (!forcedCount) return;
			if (!MergeHighLod({ native.data(), nativeCount }, { forced.data(), forcedCount }, entityCount, merged, mergedCount) ||
				!safemem::WriteBytes(nativeList, merged.data(), mergedCount * sizeof(merged[0])) ||
				!safemem::WriteBytes(countAddress, &mergedCount, sizeof(mergedCount)))
				fatal("Night rejected an invalid high-detail entity list");
		}

		void playlist_error(void* context, const char* message)
		{
			// The original 1.28 GetPlaylistName uses Scr_Error code 0x1987 when
			// invoked on a frontend server. Replay additionally takes the text.
			reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + binding::ScriptError.rva)(0x1987, context, message);
		}

		void get_playlist_name(void* context)
		{
			// Same catalog as Replay's original GetPlaylistId (0x1261B60), with the
			// original name reader at 0x10CD4C0. No backend-side duplicate catalog.
			uintptr_t server{}, ordinalDvar{};
			uint8_t frontend{};
			int ordinal{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + 0xE17FE10), &server, sizeof(server)) || !server ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(server + 0x72), &frontend, 1))
			{
				playlist_error(context, "GetPlaylistName: server state is unavailable");
				return;
			}
			if (frontend)
			{
				playlist_error(context, "GetPlaylistName not allowed on front end server");
				return;
			}
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + 0xD386948), &ordinalDvar, sizeof(ordinalDvar)) || !ordinalDvar ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(ordinalDvar + 0x28), &ordinal, sizeof(ordinal)))
			{
				playlist_error(context, "GetPlaylistName: playlist selection is unavailable");
				return;
			}
			const int id = reinterpret_cast<int (*)(int)>(g_base + binding::PlaylistId.rva)(ordinal);
			const char* name = reinterpret_cast<const char* (*)(int)>(g_base + binding::PlaylistName.rva)(id);
			if (!name)
			{
				playlist_error(context, "GetPlaylistName: playlist catalog is not loaded");
				return;
			}
			reinterpret_cast<void (*)(void*, const char*)>(g_base + binding::AddString.rva)(context, name);
		}

		void initialize(void* context)
		{
			g_initialize.load(std::memory_order_acquire)(context);
			night_movers::ResetServerContext(context);
			bool valid;
			{
				std::lock_guard lock(g_lock);
				valid = g_tables.Adopt(context, native_function_table(), native_method_table(), true);
			}
			if (!valid) fatal("Night script table initialization differs from the verified Replay layout");
		}

		std::uint64_t register_through(RegisterFn original, void* context, uintptr_t second, uintptr_t third, const Descriptor* descriptor)
		{
			bool valid;
			std::uint64_t result{};
			{
				std::lock_guard lock(g_lock);
				valid = g_tables.ValidDescriptor(descriptor) && g_tables.Adopt(context, native_function_table(), native_method_table());
				if (valid)
				{
					result = original(context, second, third, descriptor);
					g_tables.Mirror(native_function_table(), native_method_table());
				}
			}
			if (!valid) fatal("Night rejected an invalid or conflicting script registration before writing its tables");
			return result;
		}

		std::uint64_t register_impl(void* c, uintptr_t b, uintptr_t d, const Descriptor* r)
		{
			return register_through(g_register.load(std::memory_order_acquire), c, b, d, r);
		}

		std::uint64_t alternate(void* c, uintptr_t b, uintptr_t d, const Descriptor* r)
		{
			return register_through(g_alternate.load(std::memory_order_acquire), c, b, d, r);
		}

		bool matches(const binding::Entry& entry)
		{
			std::array<uint8_t, 16> bytes{};
			const auto* at = reinterpret_cast<const void*>(g_base + entry.rva);
			return safemem::RegionExecutable(at) && safemem::ReadBytes(at, bytes.data(), bytes.size()) && bytes == entry.bytes;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed) return hook::Status::Installed;
		wchar_t config[MAX_PATH]{};
		if (!native_network::ConfigurationPath(config, MAX_PATH)) return hook::Status::Failed;
			if (!night_files::MapEnabled("mp_escape2") && !night_files::MapEnabled("mp_kstenod"))
			return hook::Status::Installed;
		g_base = base;
		for (const auto& at : { binding::Initialize, binding::Register, binding::Alternate, binding::AddBool, binding::AddInt,
				 binding::Fatal, binding::PlaylistId, binding::PlaylistName, binding::AddString, binding::ScriptError, binding::GetEntity,
				 binding::ObjectError, binding::GetInt, binding::SnapshotEligible, binding::CollectHighLod, binding::AgentMode,
				 binding::AgentClient, binding::NumParams, binding::GetString, binding::SoundHash, binding::BroadcastCommand,
				 binding::SingleCharacterCommand, binding::CommandArgc, binding::CommandArgv, binding::EnterCritical,
				 binding::LeaveCritical, binding::FindMusicState, binding::UpdateMusic })
			if (!matches(at))
			{
				LOG_ERR("NightBuiltins", "native binding mismatch at RVA %llX", static_cast<unsigned long long>(at.rva));
				return hook::Status::Failed;
			}
		if (!g_configured)
		{
			if (night_movers::Install(base) != hook::Status::Installed) return hook::Status::Failed;
			if (!g_tables.Add(0x41A, reinterpret_cast<uintptr_t>(&verify_bunker_code)) ||
				!g_tables.Add(0x41B, reinterpret_cast<uintptr_t>(&alt_bunker_index)) ||
				!g_tables.Add(0x41C, reinterpret_cast<uintptr_t>(&get_playlist_name)) ||
				!g_tables.Add(0x879F, reinterpret_cast<uintptr_t>(&validate_loadout_ownership)) ||
				!g_tables.Add(0x8795, reinterpret_cast<uintptr_t>(&stop_player_music_state)) ||
				!g_tables.Add(0x879C, reinterpret_cast<uintptr_t>(&force_netfield_high_lod)) ||
				!g_tables.Add(0x879D, reinterpret_cast<uintptr_t>(&night_movers::MarkKeyframed)) ||
				!g_tables.Add(0x8794, reinterpret_cast<uintptr_t>(&night_movers::SetOptimized)) ||
				!g_tables.Add(0x87A1, reinterpret_cast<uintptr_t>(&night_movers::SetAntilagged)))
				return hook::Status::Failed;
			if (night_files::MapEnabled("mp_escape2") &&
					!escape_builtins::Configure(base, g_tables, true))
				return hook::Status::Failed;
			g_tables.Seal();
			g_configured = true;
		}
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + binding::Initialize.rva), &initialize, binding::Initialize.bytes.data(),
			binding::Initialize.bytes.size(), g_initialize);
		batch.Add(reinterpret_cast<void*>(base + binding::Register.rva), &register_impl, binding::Register.bytes.data(),
			binding::Register.bytes.size(), g_register);
		batch.Add(reinterpret_cast<void*>(base + binding::Alternate.rva), &alternate, binding::Alternate.bytes.data(),
			binding::Alternate.bytes.size(), g_alternate);
		batch.Add(reinterpret_cast<void*>(base + binding::CollectHighLod.rva), &collect_high_lod, binding::CollectHighLod.bytes.data(),
			binding::CollectHighLod.bytes.size(), g_collectHighLod);
		batch.Add(reinterpret_cast<void*>(base + binding::SingleCharacterCommand.rva), &single_character_command,
			binding::SingleCharacterCommand.bytes.data(), binding::SingleCharacterCommand.bytes.size(), g_singleCommand);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed)
		{
			using namespace iw8x::gsc::internal;
			port_tables::inspect = [](const void* context, port_tables::view& view)
			{
				std::lock_guard lock(g_lock);
				if (!g_tables.Owns(context)) return false;
				view.functions = reinterpret_cast<const uintptr_t*>(Tables::Read<uintptr_t>(context, 0xC90));
				view.methods = reinterpret_cast<const uintptr_t*>(Tables::Read<uintptr_t>(context, 0xC98));
				view.function_count = Tables::Read<uint32_t>(context, 0xC7C);
				view.method_count = Tables::Read<uint32_t>(context, 0xC88);
				return true;
			};
			port_tables::reserved = [](bool method, uint32_t slot)
			{
				return g_tables.Reserved(method, slot);
			};
			g_installed = true;
			LOG_INFO("NightBuiltins",
				"bunker 041A/041B, playlist-name 041C, stop music 8795, high-detail snapshots 879C and player-validation 879F registered; map dependency closure remains incomplete");
		}
		return status;
	}
}
