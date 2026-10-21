#include "auth/profile/xp_tokens.h"
#include "client/weapons/weapon_lowammo_policy.h"
#include "client/weapons/weapon_perk_pair_policy.h"
#include "diagnostics/night/night_builtin_tables.h"

#include "engine/inline_hook.h"

#include "gsc/internal/port_tables.hpp"
#include "network/backend/backend_latency.h"
#include "network/backend/playlist_catalog.h"
#include "network/profile/settings_profile.h"
#include "network/profile/text_validation.h"
#include "thirdparty/minhook/include/MinHook.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace console
{
	void Print(const char*, ...)
	{
	}
}

namespace
{
	unsigned checks{};
	using function = int (*)();
	std::atomic<function> first_original{}, second_original{};

	void require(bool condition, const char* message)
	{
		++checks;
		if (!condition) throw std::runtime_error(message);
	}

	int first_detour()
	{
		return first_original.load()() + 10;
	}

	int second_detour()
	{
		return second_original.load()() + 20;
	}

	void hook_transactions()
	{
		auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		require(memory != nullptr, "executable fixture allocation");
		const std::array<unsigned char, 16> first{ 0xB8, 1, 0, 0, 0, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
		auto second = first;
		second[1] = 2;
		std::memcpy(memory, first.data(), first.size());
		std::memcpy(memory + 64, second.data(), second.size());
		FlushInstructionCache(GetCurrentProcess(), memory, 4096);
		auto first_fn = reinterpret_cast<function>(memory), second_fn = reinterpret_cast<function>(memory + 64);
		hook::Batch duplicate;
		duplicate.Add(memory, &first_detour, first.data(), first.size(), first_original);
		duplicate.Add(memory, &second_detour, first.data(), first.size(), second_original);
		require(duplicate.Commit() == hook::Status::Failed, "duplicate target rejected");
		require(!first_original && !second_original && first_fn() == 1, "duplicate batch leaves fixture untouched");
		hook::Batch invalid;
		invalid.Add(memory, &first_detour, first.data(), first.size(), first_original);
		invalid.Add(memory + 64, &second_detour, first.data(), first.size(), second_original);
		require(invalid.Commit() == hook::Status::NotReady, "fingerprint mismatch rejected");
		require(!first_original && !second_original && first_fn() == 1 && second_fn() == 2, "failed preparation is rolled back");
		hook::Batch valid;
		valid.Add(memory, &first_detour, first.data(), first.size(), first_original);
		valid.Add(memory + 64, &second_detour, second.data(), second.size(), second_original);
		require(valid.Commit() == hook::Status::Installed, "validated batch activates");
		require(first_fn() == 11 && second_fn() == 22, "both detours have published originals");
		require(valid.Commit() == hook::Status::Failed, "commit is single use");
		MH_DisableHook(memory);
		MH_DisableHook(memory + 64);
		MH_RemoveHook(memory);
		MH_RemoveHook(memory + 64);
		VirtualFree(memory, 0, MEM_RELEASE);
	}

	void port_registration()
	{
		using tables = night_builtins::Tables;
		tables provider;
		require(provider.Add(0x41A, 0x1234), "port function accepted");
		require(provider.Add(0x879F, 0x5678), "port method accepted");
		require(!provider.Add(0x41A, 0x9999), "duplicate port function rejected");
		provider.Seal();
		require(provider.Reserved(false, 1) && !provider.Reserved(false, 0), "plugin reservation respects exact port IDs");
		require(provider.Reserved(true, 0x879F - tables::NativeMethodEnd), "method slot reserved");
		std::array<unsigned char, 0xD00> context{};
		std::array<uintptr_t, tables::NativeCount> functions{};
		std::array<uintptr_t, tables::NativeMethodCount> methods{};
		tables::Write(context.data(), 0xC74, tables::First);
		tables::Write(context.data(), 0xC78, tables::NativeEnd);
		tables::Write(context.data(), 0xC7C, uint32_t(functions.size()));
		tables::Write(context.data(), 0xC80, tables::MethodFirst);
		tables::Write(context.data(), 0xC84, tables::NativeMethodEnd);
		tables::Write(context.data(), 0xC88, uint32_t(methods.size()));
		tables::Write(context.data(), 0xC90, reinterpret_cast<uintptr_t>(functions.data()));
		tables::Write(context.data(), 0xC98, reinterpret_cast<uintptr_t>(methods.data()));
		require(provider.Adopt(context.data(), functions.data(), methods.data(), true), "stock table adopted");
		require(provider.Owns(context.data()), "port ownership validated");
		const auto* extended = reinterpret_cast<const uintptr_t*>(tables::Read<uintptr_t>(context.data(), 0xC90));
		require(extended[0x41A - tables::First] == 0x1234, "port provider survives publication");
		tables::Write(context.data(), 0xC78, uint32_t(0));
		require(!provider.Owns(context.data()), "inconsistent owner metadata rejected");
	}

	void feature_models()
	{
		for (const auto& token : xp_tokens::Definitions)
		{
			require(xp_tokens::Find(token.item) == &token && xp_tokens::Achievement(token.achievement) == &token, "token catalog lookup");
			require(xp_tokens::Counted(token.item) && token.seconds > 0, "token grant metadata");
		}
		require(xp_tokens::Counted(xp_tokens::TrialTicket) && !xp_tokens::Counted(-1), "trial and unknown token policy");
		matchmaking::BackendLatency latency;
		latency.Report(200, 37, 100);
		require(latency.Read(5100) == 37 && latency.Read(5101) == -1, "backend latency expiration boundary");
		latency.Report(500, 1, 100);
		require(latency.Read(100) == -1, "failed HTTP clears latency");
		int override_value = 4;
		const std::array<const int32_t*, 1> overrides{ &override_value };
		require(weapon_lowammo::Threshold(2, 0.25f, 20, overrides) == 4, "low-ammo override precedence");
		require(weapon_lowammo::Threshold(0, 0.25f, 20, {}, false) == 5, "low-ammo native fraction");
		require(weapon_lowammo::Threshold(0, std::numeric_limits<float>::quiet_NaN(), 20, {}, false) == INT32_MIN, "native NaN conversion");
		for (unsigned first = 0; first < 8; ++first)
			for (unsigned kit = 1; kit < 8; ++kit)
			{
				weapon_perk_pair::Plan plan{ first, kit };
				for (unsigned value = 0; value <= plan.Count(); ++value)
				{
					unsigned a{}, b{};
					uint8_t encoded{};
					require(plan.Decode(value, a, b) && plan.Encode(a, b, 0, 0, encoded) && encoded == value, "perk pair round trip");
				}
			}
		settings120::Account settings;
		require(settings.Handle(0, { { "action", "fetch" } }, 0).status == 401, "settings require owner");
		nlohmann::json values = nlohmann::json::object();
		for (const auto& field : settings120::fields)
			values[field.name] = field.type == 6 ? nlohmann::json("") : nlohmann::json(0);
		require(settings120::Valid(values), "complete settings profile validates");
		auto request =
			nlohmann::json{ { "action", "save" }, { "revision", uint64_t(0) }, { "token", std::string(32, 'a') }, { "values", values } };
		require(settings.Handle(1, request, 0).changed, "settings save commits");
		require(!settings.Handle(1, request, 0).changed, "settings retry is idempotent");
		request["token"] = std::string(32, 'b');
		require(settings.Handle(1, request, 0).status == 409, "stale settings update conflicts");
		settings120::Account restored;
		require(restored.Restore(1, settings.Save()), "settings persistence restores");
		require(text_validation::Check("Loadout One", text_validation::Defaults()) == "allowed", "custom name accepted");
		require(text_validation::Check(std::string("\xC0\xAF", 2), text_validation::Defaults()) == "characters", "invalid UTF-8 rejected");
	}

	void catalogs()
	{
		for (const auto id : { 6000, 6001 })
		{
			const auto module = GetModuleHandleW(nullptr);
			const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
			const auto loaded = resource ? LoadResource(module, resource) : nullptr;
			require(loaded != nullptr, "catalog fixture exists");
			std::string text(static_cast<const char*>(LockResource(loaded)), SizeofResource(module, resource));
			const auto catalog = playlist_catalog::Validate(text);
			const auto wire = playlist_catalog::Encode(catalog);
			require(playlist_catalog::Decode(wire).sha256 == catalog.sha256, "catalog transfer round trip");
			auto corrupt = nlohmann::json::parse(wire);
			corrupt["sha256"] = std::string(64, '0');
			bool rejected = false;
			try
			{
				(void)playlist_catalog::Decode(corrupt.dump());
			}
			catch (...)
			{
				rejected = true;
			}
			require(rejected, "catalog digest mismatch rejected");
		}
	}
}

int main()
{
	try
	{
		hook_transactions();
		port_registration();
		feature_models();
		catalogs();
		std::printf("Merge feature checks passed: %u\n", checks);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "Merge feature check failed: %s\n", error.what());
		return 1;
	}
}
