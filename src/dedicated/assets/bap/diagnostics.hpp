#pragma once
#include "dedicated/assets/bap/types.hpp"
namespace bap::detail
{
	extern std::atomic<uintptr_t> g_traceAuxBase;

	bool SnapshotIndex(uintptr_t address, uintptr_t base, std::size_t count, std::uint32_t& index);

	EntrySnapshot SnapshotEntry(const Entry* entry);

	__declspec(noinline) void RecordEventImpl(const char* name, uintptr_t caller, const Entry* entry, const char* phase,
		const Entry* related, std::uint64_t arg0, std::uint64_t arg1);

	__declspec(noinline) void RecordEvent(const char* name, uintptr_t caller, const Entry* entry, const char* phase = "enter",
		const Entry* related = nullptr, std::uint64_t arg0 = 0, std::uint64_t arg1 = 0);

	void FormatRva(char* text, std::size_t size, uintptr_t pc, uintptr_t base, std::size_t imageSize);

	void DumpEntry(const EntrySnapshot& snapshot);

	void DumpCrash(const EXCEPTION_POINTERS* pointers);

	LONG CALLBACK BapExceptionObserver(EXCEPTION_POINTERS* pointers);
}
