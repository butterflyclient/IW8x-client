#pragma once
#include "dedicated/assets/bap/types.hpp"
namespace bap::detail
{
	extern std::atomic<EntryPredicateFn> g_anyZone;

	extern std::atomic<EntryPredicateFn> g_usePending;

	extern std::atomic<UseTransitionOnceFn> g_useTransitionOnce;

	extern std::atomic<LifecycleMarkFn> g_lifecycleMark;

	extern RegisteredPredicateFn g_registeredPredicate;

	extern RegisteredFollowupFn g_registeredFollowup;

	bool ValidateBytes(uintptr_t address, const std::uint8_t* expected, std::size_t expectedSize);

	std::size_t EmitAbsoluteCall(std::uint8_t* out, uintptr_t target);

	bool BuildUseOnceTestPatch(
		std::array<std::uint8_t, game::kDBLinkUseOnceTestBlockSize>& patch, uintptr_t blockAddress, uintptr_t falseResume);

	bool ValidateReadableWritableRange(const void* address, std::size_t size);

	bool WriteExecutable(uintptr_t address, const std::uint8_t* bytes, std::size_t size);

	std::size_t EmitAbsoluteJump(std::uint8_t* out, uintptr_t target);

	void* NextStashBridge();

	std::size_t EmitPreservedCall(
		std::uint8_t* out, uintptr_t target, const std::uint8_t* argumentSetup = nullptr, std::size_t argumentSetupSize = 0);

	bool BuildZonePairPatch(std::array<std::uint8_t, game::kDBAssetZonePairWriteBlockSize>& patch);

	bool InstallInlineLifecyclePatches();
}
