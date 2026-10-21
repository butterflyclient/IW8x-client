#include "diagnostics/escape/escape_builtins.h"
#include "diagnostics/escape/escape_builtins_state.h"
#include "diagnostics/escape/escape_builtin_bindings.h"
#include "diagnostics/night/night_entity_bindings.h"
#include "diagnostics/night/night_playlist_bindings.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <array>
#include <exception>
#include <cstdio>
#include <windows.h>
namespace escape_builtins
{
	namespace
	{
		uintptr_t g_base{};
		namespace native = night_builtin_binding;

		[[noreturn]] void invalid(const char* text)
		{
			LOG_ERR("EscapeBuiltins", "%s", text);
			RaiseFailFastException(nullptr, nullptr, 0);
			std::terminate();
		}

		template <class T>
		T read(uintptr_t address)
		{
			T result{};
			if (!address || !safemem::ReadBytes(reinterpret_cast<const void*>(address), &result, sizeof(result)))
				invalid("unreadable native object in original Escape script callback");
			return result;
		}

		struct Service
		{
			void* context;

			void ScriptError(unsigned code)
			{
				reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + native::ScriptError.rva)(
					code, context, "IsSilenced requires a weapon object");
			}

			void ObjectError(unsigned code)
			{
				reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + native::ObjectError.rva)(
					code, context, "Breath timer query requires a player or agent entity");
			}

			void AddBool(bool value)
			{
				reinterpret_cast<void (*)(void*, int)>(g_base + native::AddBool.rva)(context, value);
			}

			uintptr_t WeaponPool()
			{
				const auto pool = reinterpret_cast<uintptr_t (*)()>(g_base + binding::WeaponPool.rva)();
				if (!pool) invalid("native script weapon pool is unavailable");
				return pool;
			}

			uintptr_t DefaultWeapon()
			{
				return g_base + binding::DefaultWeapon;
			}

			bool Alternate(uintptr_t weapon)
			{
				return read<std::uint8_t>(weapon + 0x3c) != 0;
			}

			bool Silenced(uintptr_t weapon, bool alternate)
			{
				return reinterpret_cast<bool (*)(uintptr_t, bool)>(g_base + binding::Silenced.rva)(weapon, alternate);
			}

			uintptr_t PlayerOrAgent(std::uint32_t number)
			{
				const auto entity = reinterpret_cast<uintptr_t (*)(std::uint64_t)>(g_base + native::GetEntity.rva)(number);
				if (!entity) invalid("native entity resolver returned null");
				const auto client = read<uintptr_t>(entity + 0x150);
				return client ? client : read<uintptr_t>(entity + 0x168);
			}

			bool BreathTimerActive(uintptr_t state)
			{
				return reinterpret_cast<bool (*)(uintptr_t)>(g_base + binding::BreathTimerActive.rva)(state);
			}
		};

		void silenced(void* context, std::uint64_t reference)
		{
			Service service{ context };
			IsSilenced(service, reference);
		}

		void breath(void* context, std::uint64_t reference)
		{
			Service service{ context };
			IsBreathTimerActive(service, reference);
		}

		void encrypted_cinematic(void*, std::uint64_t)
		{
			// Original 1.30 callback 245DD10 is exactly C2 00 00 (ret 0), including
			// invalid handles. This preserves that callback, not a generic fallback.
		}

		template <unsigned Id>
		void unported(void* context, std::uint64_t reference)
		{
			// This is deliberately a runtime error, never an implementation or a
			// successful empty return. It lets the diagnostic map identify its first
			// executed dependency instead of stopping at an unused import's link.
			char message[160]{};
			std::snprintf(message, sizeof(message), "Escape diagnostic: builtin 0x%04X is not ported", Id);
			LOG_ERR("EscapeBuiltins", "UNPORTED_CALL id=0x%04X context=%p reference=%llX", Id, context,
				static_cast<unsigned long long>(reference));
			reinterpret_cast<void (*)(unsigned, void*, const char*)>(g_base + native::ScriptError.rva)(0, context, message);
			invalid("native script error unexpectedly returned from an unported Escape callback");
		}
	}

	bool Configure(uintptr_t base, night_builtins::Tables& tables, bool diagnosticErrors)
	{
		for (const auto& at : { binding::WeaponPool, binding::Silenced, binding::BreathTimerActive, native::GetEntity, native::ObjectError,
				 native::ScriptError, native::AddBool })
		{
			std::array<std::uint8_t, 16> actual{};
			if (!safemem::RegionExecutable(reinterpret_cast<void*>(base + at.rva)) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(base + at.rva), actual.data(), actual.size()) || actual != at.bytes)
			{
				LOG_ERR("EscapeBuiltins", "native binding mismatch at RVA %llX", static_cast<unsigned long long>(at.rva));
				return false;
			}
		}
		g_base = base;
		if (diagnosticErrors)
		{
			if (!tables.Add(0x41F, reinterpret_cast<uintptr_t>(&unported<0x41F>)) ||
				!tables.Add(0x420, reinterpret_cast<uintptr_t>(&unported<0x420>)) ||
				!tables.Add(0x423, reinterpret_cast<uintptr_t>(&unported<0x423>)) ||
				!tables.Add(0x8788, reinterpret_cast<uintptr_t>(&unported<0x8788>)) ||
				!tables.Add(0x878A, reinterpret_cast<uintptr_t>(&unported<0x878A>)) ||
				!tables.Add(0x878C, reinterpret_cast<uintptr_t>(&unported<0x878C>)) ||
				!tables.Add(0x87A8, reinterpret_cast<uintptr_t>(&unported<0x87A8>)))
				return false;
			LOG_INFO("EscapeBuiltins", "DEVELOPMENT ONLY: seven unported callbacks resolve to explicit script errors");
		}
		return tables.Add(0x87B6, reinterpret_cast<uintptr_t>(&silenced)) && tables.Add(0x87B8, reinterpret_cast<uintptr_t>(&breath)) &&
			tables.Add(0x878E, reinterpret_cast<uintptr_t>(&encrypted_cinematic));
	}
}
