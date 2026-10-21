#include "bap.h"

#include "engine/addresses.hpp"

#include "console.h"

#include "game.h"
#include "safemem.h"

#include "state.h"
#include "thirdparty/minhook/include/MinHook.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>

#include "dedicated/assets/bap/internal.hpp"
namespace bap::detail
{
	std::atomic<EntryPredicateFn> g_anyZone{ nullptr };

	std::atomic<EntryPredicateFn> g_usePending{ nullptr };

	std::atomic<UseTransitionOnceFn> g_useTransitionOnce{ nullptr };

	std::atomic<LifecycleMarkFn> g_lifecycleMark{ nullptr };

	RegisteredPredicateFn g_registeredPredicate = nullptr;

	RegisteredFollowupFn g_registeredFollowup = nullptr;

	bool ValidateBytes(uintptr_t address, const std::uint8_t* expected, std::size_t expectedSize)
	{
		std::uint8_t actual[128]{};
		if (!expected || !expectedSize || expectedSize > sizeof(actual)) return false;
		return safemem::ReadBytes(reinterpret_cast<const void*>(address), actual, expectedSize) &&
			std::memcmp(actual, expected, expectedSize) == 0;
	}

	bool BuildUseOnceTestPatch(
		std::array<std::uint8_t, game::kDBLinkUseOnceTestBlockSize>& patch, uintptr_t blockAddress, uintptr_t falseResume)
	{
		patch.fill(0x90);
		std::size_t p = 0;

		// The overwritten native block derives a stock index from RDI and only
		// clobbers volatile arithmetic registers. Preserve R10/R11 explicitly;
		// R11 is also the absolute-call scratch register used below.
		const std::uint8_t saveScratch[] = { 0x41, 0x52, 0x41, 0x53 }; // push r10; push r11
		std::memcpy(patch.data() + p, saveScratch, sizeof(saveScratch));
		p += sizeof(saveScratch);

		// Dynamically align for an inline call, provide the Win64 home area, and
		// retain the exact post-push RSP just beyond that home area.
		const std::uint8_t alignStack[] = {
			0x48, 0x89, 0xE0, // mov rax,rsp
			0x48, 0x83, 0xE4, 0xF0, // and rsp,-10h
			0x48, 0x83, 0xEC, 0x30, // sub rsp,30h
			0x48, 0x89, 0x44, 0x24, 0x20, // mov [rsp+20h],rax
			0x48, 0x89, 0xF9, // mov rcx,rdi (existing entry)
		};
		std::memcpy(patch.data() + p, alignStack, sizeof(alignStack));
		p += sizeof(alignStack);
		p += EmitAbsoluteCall(patch.data() + p, reinterpret_cast<uintptr_t>(&TestUseOnceAware));

		const std::uint8_t restoreAndTest[] = {
			0x48, 0x8B, 0x64, 0x24, 0x20, // mov rsp,[rsp+20h]
			0x41, 0x5B, // pop r11
			0x41, 0x5A, // pop r10
			0x84, 0xC0, // test al,al
			0x0F, 0x84, 0, 0, 0, 0, // jz falseResume
		};
		std::memcpy(patch.data() + p, restoreAndTest, sizeof(restoreAndTest));
		const std::size_t branchOffset = p + sizeof(restoreAndTest) - sizeof(std::int32_t);
		p += sizeof(restoreAndTest);

		const uintptr_t branchNext = blockAddress + p;
		const auto delta64 = static_cast<std::int64_t>(falseResume) - static_cast<std::int64_t>(branchNext);
		if (delta64 < INT32_MIN || delta64 > INT32_MAX || p > patch.size()) return false;
		const auto delta = static_cast<std::int32_t>(delta64);
		std::memcpy(patch.data() + branchOffset, &delta, sizeof(delta));

		// AL!=0 intentionally falls through the NOP tail to +0x11ACBCF, exactly
		// where Replay's native set-bit path continues. The zero path branches to
		// +0x11ACC0E, matching the original TEST/JZ at +0x11ACBC5/+0x11ACBCD.
		return p <= patch.size();
	}

	bool ValidateReadableWritableRange(const void* address, std::size_t size)
	{
		if (!address || !size) return false;

		auto current = reinterpret_cast<uintptr_t>(address);
		const uintptr_t end = current + size;
		if (end < current) return false;

		while (current < end)
		{
			std::size_t available = 0;
			const void* const pointer = reinterpret_cast<const void*>(current);
			if (!safemem::RegionReadable(pointer, available) || !safemem::RegionWritable(pointer) || !available) return false;

			const std::size_t remaining = static_cast<std::size_t>(end - current);
			const std::size_t advance = available < remaining ? available : remaining;
			current += advance;
		}
		return true;
	}

	bool WriteExecutable(uintptr_t address, const std::uint8_t* bytes, std::size_t size)
	{
		if (!address || !bytes || !size) return false;
		DWORD oldProtect = 0;
		void* target = reinterpret_cast<void*>(address);
		if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
		std::memcpy(target, bytes, size);
		FlushInstructionCache(GetCurrentProcess(), target, size);
		DWORD ignored = 0;
		VirtualProtect(target, size, oldProtect, &ignored);
		return true;
	}

	std::size_t EmitAbsoluteCall(std::uint8_t* out, uintptr_t target)
	{
		// mov r11, imm64 ; call r11
		out[0] = 0x49;
		out[1] = 0xBB;
		std::memcpy(out + 2, &target, sizeof(target));
		out[10] = 0x41;
		out[11] = 0xFF;
		out[12] = 0xD3;
		return 13;
	}

	std::size_t EmitAbsoluteJump(std::uint8_t* out, uintptr_t target)
	{
		// mov r11, imm64 ; jmp r11
		out[0] = 0x49;
		out[1] = 0xBB;
		std::memcpy(out + 2, &target, sizeof(target));
		out[10] = 0x41;
		out[11] = 0xFF;
		out[12] = 0xE3;
		return 13;
	}

	void* NextStashBridge()
	{
		static void* bridge = nullptr;
		if (bridge) return bridge;
		// The 27-byte stash site cannot hold a preserving call. The old leaf helper
		// did not call Win32; tracing now does. Keep its new volatile GPR clobbers out
		// of Replay, leaving only RAX as the result (RCX/R11 are set by the call site).
		// Replay inline sites can arrive at either stack parity. Align dynamically,
		// save the exact pre-alignment RSP outside the callee's 32-byte home space,
		// and forward the original return PC before entering C++.
		std::uint8_t code[80]{};
		const std::uint8_t prologue[] = {
			0x9C, 0x51, 0x52, // pushfq; push rcx; push rdx
			0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, // push r8..r11
			0x48, 0x89, 0xE0, // mov rax,rsp
			0x48, 0x83, 0xE4, 0xF0, // and rsp,-10h
			0x48, 0x83, 0xEC, 0x30, // sub rsp,30h (home + saved RSP)
			0x48, 0x89, 0x44, 0x24, 0x20, // mov [rsp+20h],rax
			0x48, 0x8B, 0x50, 0x38, // mov rdx,[rax+38h] (Replay return PC)
		};
		const std::uint8_t epilogue[] = {
			0x48, 0x8B, 0x64, 0x24, 0x20, // mov rsp,[rsp+20h]
			0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x9D, 0xC3, // pop rdx; pop rcx; popfq; ret
		};
		std::memcpy(code, prologue, sizeof(prologue));
		auto size = sizeof(prologue);
		size += EmitAbsoluteCall(code + size, reinterpret_cast<uintptr_t>(&NextStashAware));
		std::memcpy(code + size, epilogue, sizeof(epilogue));
		size += sizeof(epilogue);
		void* memory = VirtualAlloc(nullptr, sizeof(code), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!memory) return nullptr;
		std::memcpy(memory, code, size);
		DWORD oldProtect = 0;
		if (!VirtualProtect(memory, sizeof(code), PAGE_EXECUTE_READ, &oldProtect) ||
			!FlushInstructionCache(GetCurrentProcess(), memory, size))
		{
			VirtualFree(memory, 0, MEM_RELEASE);
			return nullptr;
		}
		bridge = memory; // Process lifetime, like the installed inline patches.
		return bridge;
	}

	std::size_t EmitPreservedCall(std::uint8_t* out, uintptr_t target, const std::uint8_t* argumentSetup, std::size_t argumentSetupSize)
	{
		// These calls are injected into the middle of Replay functions, not emitted
		// by the compiler.  Preserve all volatile integer state and EFLAGS, and
		// explicitly provide the Win64 32-byte home area before entering C++.
		// Inline sites are not ABI call boundaries: align dynamically for either
		// parity and restore the exact original RSP. The zone-pair block also has
		// an interior branch target; BuildZonePairPatch preserves that entry before
		// any register saves. Alignment cannot repair an unbalanced save/restore.
		std::size_t p = 0;

		out[p++] = 0x9C; // pushfq
		out[p++] = 0x50; // push rax
		out[p++] = 0x51; // push rcx
		out[p++] = 0x52; // push rdx

		const std::uint8_t pushVolatile[] = {
			0x41, 0x50, // push r8
			0x41, 0x51, // push r9
			0x41, 0x52, // push r10
			0x41, 0x53, // push r11
		};
		std::memcpy(out + p, pushVolatile, sizeof(pushVolatile));
		p += sizeof(pushVolatile);

		const std::uint8_t subShadow[] = {
			0x48, 0x89, 0xE0, // mov rax,rsp (RAX already saved)
			0x48, 0x83, 0xE4, 0xF0, // and rsp,-10h
			0x48, 0x83, 0xEC, 0x30, // sub rsp,30h (home + saved RSP)
			0x48, 0x89, 0x44, 0x24, 0x20, // mov [rsp+20h],rax
		};
		std::memcpy(out + p, subShadow, sizeof(subShadow));
		p += sizeof(subShadow);

		if (argumentSetup && argumentSetupSize)
		{
			std::memcpy(out + p, argumentSetup, argumentSetupSize);
			p += argumentSetupSize;
		}

		p += EmitAbsoluteCall(out + p, target);

		const std::uint8_t addShadow[] = { 0x48, 0x8B, 0x64, 0x24, 0x20 }; // mov rsp,[rsp+20h]
		std::memcpy(out + p, addShadow, sizeof(addShadow));
		p += sizeof(addShadow);

		const std::uint8_t popVolatile[] = {
			0x41, 0x5B, // pop r11
			0x41, 0x5A, // pop r10
			0x41, 0x59, // pop r9
			0x41, 0x58, // pop r8
		};
		std::memcpy(out + p, popVolatile, sizeof(popVolatile));
		p += sizeof(popVolatile);

		out[p++] = 0x5A; // pop rdx
		out[p++] = 0x59; // pop rcx
		out[p++] = 0x58; // pop rax
		out[p++] = 0x9D; // popfq

		return p;
	}

	bool BuildZonePairPatch(std::array<std::uint8_t, game::kDBAssetZonePairWriteBlockSize>& patch)
	{
		patch.fill(0x90);
		// Replay enters at +0x11AD57E for an existing entry, or branches from
		// +0x11AD449 to +0x11AD585 after allocating a default entry. Keep the first
		// seven-byte LEA at its original address; the allocation path already has
		// R14=StockPool and must enter at the FIRST push, never inside the save set.
		constexpr std::size_t sharedEntryOffset = 7;
		std::memcpy(patch.data(), kZonePairBytes, sharedEntryOffset); // lea r14,[rip+stock pool]
		const std::uint8_t args[] = { 0x48, 0x89, 0xF1, 0x44, 0x89, 0xEA }; // rcx=rsi; edx=r13d
		const std::uint8_t restorePool[] = { 0x49, 0x8B, 0xF6 }; // mov rsi,r14
		std::array<std::uint8_t, 128> call{};
		const auto size = EmitPreservedCall(call.data(), reinterpret_cast<uintptr_t>(&MarkZonePairAware), args, sizeof(args));
		if (sharedEntryOffset + size + sizeof(restorePool) > patch.size()) return false;
		std::memcpy(patch.data() + sharedEntryOffset, call.data(), size);
		// The stock arithmetic restores RSI to StockPool at +0x11AD5A0. The next
		// loop iteration passes RSI as FindAsset's glob, so this is a live output.
		std::memcpy(patch.data() + sharedEntryOffset + size, restorePool, sizeof(restorePool));
		return true;
	}

	template <std::size_t N>
	bool PatchFilledBlock(uintptr_t address, const std::array<std::uint8_t, N>& patch)
	{
		return WriteExecutable(address, patch.data(), patch.size());
	}

	bool InstallInlineLifecyclePatches()
	{
		const uintptr_t alternate = g_base + game::kDBAssetAlternateLifecycleWriteBlockRVA;
		const uintptr_t zonePair = g_base + game::kDBAssetZonePairWriteBlockRVA;
		const uintptr_t resolved = g_base + game::kDBAssetResolvedHeadWriteBlockRVA;
		const uintptr_t replaceClear = g_base + game::kDBAssetReplaceHeadLifecycleClearBlockRVA;
		const uintptr_t stashNext = g_base + game::kDBLinkStashNextBlockRVA;
		const uintptr_t transientHeadA = g_base + game::kDBLinkTransientHeadWriteABlockRVA;
		const uintptr_t transientHeadB = g_base + game::kDBLinkTransientHeadWriteBBlockRVA;
		const uintptr_t useOnceTest = g_base + game::kDBLinkUseOnceTestBlockRVA;
		const uintptr_t lifecycleWrite = g_base + game::kDBAssetLifecycleStockWriteRVA;

		struct Check
		{
			uintptr_t address;
			const std::uint8_t* expected;
			std::size_t expectedSize;
			const char* name;
		};

		const Check checks[] = {
			{ alternate, kAlternateLifecycleBytes, sizeof(kAlternateLifecycleBytes), "alternate lifecycle mark" },
			{ zonePair, kZonePairBytes, sizeof(kZonePairBytes), "transient zone-pair write" },
			{ g_base + kZonePairAllocatedBranchRva, kZonePairAllocatedBranchBytes, sizeof(kZonePairAllocatedBranchBytes),
				"zone-pair allocation branch" },
			{ resolved, kResolvedHeadBytes, sizeof(kResolvedHeadBytes), "resolved-head write" },
			{ replaceClear, kReplaceClearBytes, sizeof(kReplaceClearBytes), "replace-head lifecycle clear" },
			{ stashNext, kStashNextBytes, sizeof(kStashNextBytes), "DB_Link stash traversal" },
			{ transientHeadA, kTransientHeadABytes, sizeof(kTransientHeadABytes), "DB_Link transient-head write A" },
			{ transientHeadB, kTransientHeadBBytes, sizeof(kTransientHeadBBytes), "DB_Link transient-head write B" },
			{ useOnceTest, kUseOnceTestBytes, sizeof(kUseOnceTestBytes), "DB_Link UseOnce membership test" },
			{ lifecycleWrite, kLifecycleWriteBytes, sizeof(kLifecycleWriteBytes), "lifecycle stock write" },
		};
		for (const auto& check : checks)
		{
			if (!ValidateBytes(check.address, check.expected, check.expectedSize))
			{
				LOG_ERR("BAP", "%s byte guard did not match Replay 1.20.4.7623265 @%p", check.name, reinterpret_cast<void*>(check.address));
				return false;
			}
		}

		std::array<std::uint8_t, game::kDBAssetAlternateLifecycleWriteBlockSize> alternatePatch{};
		alternatePatch.fill(0x90);
		std::size_t p = EmitPreservedCall(alternatePatch.data(), reinterpret_cast<uintptr_t>(&MarkLifecycleAware));
		if (p > alternatePatch.size()) return false;

		std::array<std::uint8_t, game::kDBAssetZonePairWriteBlockSize> zonePatch{};
		if (!BuildZonePairPatch(zonePatch)) return false;

		std::array<std::uint8_t, game::kDBAssetResolvedHeadWriteBlockSize> resolvedPatch{};
		resolvedPatch.fill(0x90);
		// RCX already holds the entry at the beginning of the overwritten stock
		// index/bitset arithmetic.
		p = EmitPreservedCall(resolvedPatch.data(), reinterpret_cast<uintptr_t>(&MarkResolvedHeadAware));
		if (p + 13 > resolvedPatch.size()) return false;
		p += EmitAbsoluteJump(resolvedPatch.data() + p, g_base + game::kDBAssetResolvedHeadWriteResumeRVA);

		std::array<std::uint8_t, game::kDBAssetReplaceHeadLifecycleClearBlockSize> clearPatch{};
		clearPatch.fill(0x90);
		const std::uint8_t movRcxRsi[] = { 0x48, 0x89, 0xF1 }; // mov rcx,rsi
		const std::uint8_t movRcxRdi[] = { 0x48, 0x89, 0xF9 }; // mov rcx,rdi
		p = EmitPreservedCall(clearPatch.data(), reinterpret_cast<uintptr_t>(&ClearLifecycleAware), movRcxRsi, sizeof(movRcxRsi));
		if (p + sizeof(movRcxRdi) > clearPatch.size()) return false;
		// The reference translation leaves RCX pointing at RDI for the continuation.
		std::memcpy(clearPatch.data() + p, movRcxRdi, sizeof(movRcxRdi));
		p += sizeof(movRcxRdi);

		std::array<std::uint8_t, game::kDBLinkStashNextBlockSize> stashPatch{};
		stashPatch.fill(0x90);
		void* stashBridge = NextStashBridge();
		if (!stashBridge) return false;
		p = 0;
		std::memcpy(stashPatch.data() + p, movRcxRsi, sizeof(movRcxRsi));
		p += sizeof(movRcxRsi);
		p += EmitAbsoluteCall(stashPatch.data() + p, reinterpret_cast<uintptr_t>(stashBridge));
		const std::uint8_t stashTail[] = { 0x48, 0x89, 0xC6, 0x48, 0x85, 0xC0, 0x75, 0x00 }; // mov rsi,rax; test rax,rax; jnz rel8
		std::memcpy(stashPatch.data() + p, stashTail, sizeof(stashTail));
		const uintptr_t branchNext = stashNext + p + sizeof(stashTail);
		const intptr_t branchDelta =
			static_cast<intptr_t>(g_base + game::addresses::runtime::rva_011ac876) - static_cast<intptr_t>(branchNext);
		if (branchDelta < -128 || branchDelta > 127) return false;
		stashPatch[p + sizeof(stashTail) - 1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(branchDelta));

		std::array<std::uint8_t, game::kDBLinkTransientHeadWriteABlockSize> transientAPatch{};
		transientAPatch.fill(0x90);
		p = EmitPreservedCall(transientAPatch.data(), reinterpret_cast<uintptr_t>(&MarkTransientHeadAware));
		const std::uint8_t movRaxRsi[] = { 0x48, 0x89, 0xF0 }; // mov rax,rsi
		if (p + sizeof(movRaxRsi) > transientAPatch.size()) return false;
		std::memcpy(transientAPatch.data() + p, movRaxRsi, sizeof(movRaxRsi));
		p += sizeof(movRaxRsi);

		std::array<std::uint8_t, game::kDBLinkTransientHeadWriteBBlockSize> transientBPatch{};
		transientBPatch.fill(0x90);
		p = EmitPreservedCall(transientBPatch.data(), reinterpret_cast<uintptr_t>(&MarkTransientHeadAware));
		if (p + sizeof(movRcxRsi) > transientBPatch.size()) return false;
		std::memcpy(transientBPatch.data() + p, movRcxRsi, sizeof(movRcxRsi));
		p += sizeof(movRcxRsi);

		std::array<std::uint8_t, game::kDBLinkUseOnceTestBlockSize> useOncePatch{};
		if (!BuildUseOnceTestPatch(useOncePatch, useOnceTest, g_base + game::kDBLinkUseOnceTestFalseResumeRVA)) return false;

		std::array<std::uint8_t, sizeof(kLifecycleWriteBytes)> lifecycleWritePatch{};
		lifecycleWritePatch.fill(0x90);

		return PatchFilledBlock(alternate, alternatePatch) && PatchFilledBlock(zonePair, zonePatch) &&
			PatchFilledBlock(resolved, resolvedPatch) && PatchFilledBlock(replaceClear, clearPatch) &&
			PatchFilledBlock(stashNext, stashPatch) && PatchFilledBlock(transientHeadA, transientAPatch) &&
			PatchFilledBlock(transientHeadB, transientBPatch) && PatchFilledBlock(useOnceTest, useOncePatch) &&
			PatchFilledBlock(lifecycleWrite, lifecycleWritePatch);
	}
}

namespace bap
{
	using namespace detail;

	hook::Status InstallLifecycle(uintptr_t moduleBase)
	{
		if (!Enabled()) return hook::Status::Installed;
		if (Ready()) return hook::Status::Installed;

		std::lock_guard<std::mutex> lock(hook::detail::Mutex());
		if (!g_corePrepared || !g_state || g_base != moduleBase)
		{
			LOG_ERR("BAP", "lifecycle install requested before the core auxiliary DB hooks were prepared");
			return hook::Status::Failed;
		}

		g_registeredPredicate = reinterpret_cast<RegisteredPredicateFn>(g_base + game::kDBTransientRegisteredPredicateRVA);
		g_registeredFollowup = reinterpret_cast<RegisteredFollowupFn>(g_base + game::kDBTransientRegisteredFollowupRVA);
		if (!safemem::RegionExecutable(reinterpret_cast<void*>(g_registeredPredicate)) ||
			!safemem::RegionExecutable(reinterpret_cast<void*>(g_registeredFollowup)))
		{
			LOG_ERR("BAP", "Replay transient registered predicate/follow-up is not executable");
			return hook::Status::Failed;
		}

		// Validate every inline site before creating/enabling anything.  The long
		// blocks were first identified structurally; these prefixes only pin that
		// structural mapping to the exact Replay 1.20.4.7623265 image.
		struct InlineGuard
		{
			uintptr_t rva;
			const std::uint8_t* bytes;
			std::size_t size;
			const char* name;
		};

		const InlineGuard inlineGuards[] = {
			{ game::kDBAssetAlternateLifecycleWriteBlockRVA, kAlternateLifecycleBytes, sizeof(kAlternateLifecycleBytes),
				"alternate lifecycle mark" },
			{ game::kDBAssetZonePairWriteBlockRVA, kZonePairBytes, sizeof(kZonePairBytes), "zone-pair write" },
			{ kZonePairAllocatedBranchRva, kZonePairAllocatedBranchBytes, sizeof(kZonePairAllocatedBranchBytes),
				"zone-pair allocation branch" },
			{ game::kDBAssetResolvedHeadWriteBlockRVA, kResolvedHeadBytes, sizeof(kResolvedHeadBytes), "resolved-head write" },
			{ game::kDBAssetReplaceHeadLifecycleClearBlockRVA, kReplaceClearBytes, sizeof(kReplaceClearBytes),
				"replace-head lifecycle clear" },
			{ game::kDBLinkStashNextBlockRVA, kStashNextBytes, sizeof(kStashNextBytes), "DB_Link stash traversal" },
			{ game::kDBLinkTransientHeadWriteABlockRVA, kTransientHeadABytes, sizeof(kTransientHeadABytes), "DB_Link transient-head A" },
			{ game::kDBLinkTransientHeadWriteBBlockRVA, kTransientHeadBBytes, sizeof(kTransientHeadBBytes), "DB_Link transient-head B" },
			{ game::kDBAssetLifecycleStockWriteRVA, kLifecycleWriteBytes, sizeof(kLifecycleWriteBytes), "lifecycle stock write" },
		};
		for (const auto& guard : inlineGuards)
		{
			if (!ValidateBytes(g_base + guard.rva, guard.bytes, guard.size))
			{
				LOG_ERR(
					"BAP", "%s mapping no longer matches Replay bytes @+0x%llX", guard.name, static_cast<unsigned long long>(guard.rva));
				return hook::Status::NotReady;
			}
		}

		void* trampolines[4]{};
		HookSpec lifecycleHooks[] = {
			{ game::kDBAssetAnyZoneRVA, reinterpret_cast<void*>(&AnyZoneDetour), kAnyZoneBytes, sizeof(kAnyZoneBytes), &trampolines[0],
				"AnyZone" },
			{ game::kDBAssetUsePendingRVA, reinterpret_cast<void*>(&UsePendingDetour), kUsePendingBytes, sizeof(kUsePendingBytes),
				&trampolines[1], "UsePending" },
			{ game::kDBAssetUseTransitionOnceRVA, reinterpret_cast<void*>(&UseTransitionOnceDetour), kUseOnceBytes, sizeof(kUseOnceBytes),
				&trampolines[2], "UseTransitionOnce" },
			{ game::kDBAssetLifecycleMarkRVA, reinterpret_cast<void*>(&LifecycleMarkDetour), kLifecycleMarkBytes,
				sizeof(kLifecycleMarkBytes), &trampolines[3], "LifecycleMark" },
		};

		if (!g_lifecyclePrepared)
		{
			for (std::size_t i = 0; i < std::size(lifecycleHooks); ++i)
			{
				const uintptr_t target = g_base + lifecycleHooks[i].rva;
				const auto status = hook::detail::Prepare(reinterpret_cast<void*>(target), lifecycleHooks[i].detour,
					lifecycleHooks[i].bytes, lifecycleHooks[i].size, lifecycleHooks[i].trampoline);
				if (status != hook::Status::Installed)
				{
					LOG_ERR("BAP", "could not prepare lifecycle hook %s @+0x%llX", lifecycleHooks[i].name,
						static_cast<unsigned long long>(lifecycleHooks[i].rva));
					return status;
				}
				g_lifecycleTargets[i] = target;
			}
			g_anyZone.store(reinterpret_cast<EntryPredicateFn>(trampolines[0]), std::memory_order_release);
			g_usePending.store(reinterpret_cast<EntryPredicateFn>(trampolines[1]), std::memory_order_release);
			g_useTransitionOnce.store(reinterpret_cast<UseTransitionOnceFn>(trampolines[2]), std::memory_order_release);
			g_lifecycleMark.store(reinterpret_cast<LifecycleMarkFn>(trampolines[3]), std::memory_order_release);
			g_lifecyclePrepared = true;
		}

		// Enable the lifecycle wrappers while the core type-101 routes are still
		// disabled.  At this stage they are transparent for stock entries.  This also
		// makes it safe to NOP LifecycleMark's direct stock-plane write: its detour
		// performs that exact write for stock entries before continuing the native body.
		for (const uintptr_t target : g_lifecycleTargets)
		{
			const MH_STATUS queued = MH_QueueEnableHook(reinterpret_cast<void*>(target));
			if (queued != MH_OK)
			{
				LOG_ERR("BAP", "could not queue lifecycle hook @%p: %s", reinterpret_cast<void*>(target), MH_StatusToString(queued));
				return hook::Status::Failed;
			}
		}
		MH_STATUS applied = MH_ApplyQueued();
		if (applied != MH_OK)
		{
			LOG_ERR("BAP", "could not enable lifecycle hooks: %s", MH_StatusToString(applied));
			return hook::Status::Failed;
		}

		if (!InstallInlineLifecyclePatches())
		{
			LOG_ERR("BAP", "one or more translated Replay lifecycle/DB_Link blocks could not be installed");
			return hook::Status::Failed;
		}

		// Only now can Allocate safely return an auxiliary pointer.  Every downstream
		// stock-pool assumption reached by the reference BAP path is auxiliary-aware.
		for (const uintptr_t target : g_coreTargets)
		{
			const MH_STATUS queued = MH_QueueEnableHook(reinterpret_cast<void*>(target));
			if (queued != MH_OK)
			{
				LOG_ERR("BAP", "could not queue core DB hook @%p: %s", reinterpret_cast<void*>(target), MH_StatusToString(queued));
				return hook::Status::Failed;
			}
		}
		applied = MH_ApplyQueued();
		if (applied != MH_OK)
		{
			LOG_ERR("BAP", "could not enable core BAP DB hooks: %s", MH_StatusToString(applied));
			return hook::Status::Failed;
		}

		g_coreReady.store(true, std::memory_order_release);
		g_lifecycleReady.store(true, std::memory_order_release);
		LOG_INFO("BAP", "full type-101 DB routing active: core=8 lifecycle=4 inline=9 entries=%zu", kCapacity);
		return hook::Status::Installed;
	}

	void SetLifecycleReady(bool ready)
	{
		g_lifecycleReady.store(ready, std::memory_order_release);
	}
}
