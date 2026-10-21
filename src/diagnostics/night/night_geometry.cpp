#include "diagnostics/night/night_geometry.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include "engine/safemem.h"
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
namespace night_geometry
{
	namespace
	{
		// Exact Replay world streaming scheduler. D71BBD sorts the per-frame candidate
		// list; D71BC2 reloads its count before the request/unload budget loops. Exclude
		// the two known corrupt render files here, before native ownership is acquired.
		// No collision file, completion bit, registered asset or IO error is fabricated.
		constexpr uintptr_t SortRva = 0xD6E260, SortReturn = 0xD71BC2;
		constexpr uintptr_t CandidatesRva = 0x5CF620C, CountRva = 0x5CF4A04;
		constexpr uintptr_t FileIndicesRva = 0x5CE9ACA, FilesRva = 0xEFBD470;
		constexpr unsigned MaxCandidates = 1536, MaxFiles = 0x280D;
		using SortFn = void (*)(std::uint16_t*, std::uint16_t*, std::ptrdiff_t, bool);
		std::atomic<SortFn> original{ nullptr };
		uintptr_t imageBase{};
		unsigned reported{};

		bool missing(const char* name)
		{
			return name && (!std::strcmp(name, "mp_kstenod_00073_tr") || !std::strcmp(name, "mp_kstenod_00074_tr"));
		}

		template <class Name>
		unsigned filter(std::uint16_t* list, unsigned count, Name name)
		{
			unsigned kept = 0;
			for (unsigned i = 0; i < count; ++i)
			{
				const auto tile = list[i];
				if (!missing(name(tile))) list[kept++] = tile;
			}
			return kept;
		}

		void filter_candidates(uintptr_t caller, std::uint16_t* begin, std::uint16_t* end)
		{
			if (caller != imageBase + SortReturn || !night_files::MapEnabled("mp_kstenod") ||
				begin != reinterpret_cast<std::uint16_t*>(imageBase + CandidatesRva))
				return;
			auto& count = *reinterpret_cast<unsigned*>(imageBase + CountRva);
			if (count > MaxCandidates || end != begin + count) return;
			const auto* indices = reinterpret_cast<const std::uint16_t*>(imageBase + FileIndicesRva);
			const unsigned kept = filter(begin, count,
				[&](unsigned tile) -> const char*
				{
					if (tile >= MaxCandidates) return nullptr;
					const unsigned index = indices[tile];
					if (!index || index >= MaxFiles) return nullptr;
					return *reinterpret_cast<const char* const*>(imageBase + FilesRva + 32ull * index + 8);
				});
			if (kept != count && reported++ == 0)
				LOG_WARN("NightGeometry",
					"excluding original damaged render chunks 00073/00074 from streaming; valid collision remains enabled");
			count = kept;
		}

		__declspec(noinline) void sort(std::uint16_t* begin, std::uint16_t* end, std::ptrdiff_t ideal, bool flag)
		{
			const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
			original.load(std::memory_order_acquire)(begin, end, ideal, flag);
			filter_candidates(caller, begin, end);
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (!night_files::MapEnabled("mp_kstenod")) return hook::Status::Installed;
		if (original.load(std::memory_order_acquire)) return imageBase == base ? hook::Status::Installed : hook::Status::Failed;

		struct Guard
		{
			uintptr_t rva;
			std::array<std::uint8_t, 16> bytes;
		};

		const Guard guards[]{
			{ SortRva, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48 } },
			{ 0xD71BA5, { 0x48, 0x8D, 0x0D, 0x60, 0x46, 0xF8, 0x04, 0x44, 0x0F, 0xB6, 0x4C, 0x24, 0x74, 0x4A, 0x8D, 0x14 } },
			{ 0xD71BC2, { 0x44, 0x8B, 0x3D, 0x3B, 0x2E, 0xF8, 0x04, 0x45, 0x85, 0xFF, 0x0F, 0x84, 0x42, 0x03, 0x00, 0x00 } },
			{ 0xD720AF, { 0x43, 0x0F, 0xB7, 0x8C, 0x62, 0xCA, 0x9A, 0xCE, 0x05, 0x4C, 0x8D, 0x2D, 0x41, 0xDF, 0x28, 0xFF } },
			{ 0x15F8ACB, { 0x66, 0x41, 0x3B, 0xC0, 0x74, 0x1E, 0x0F, 0xB7, 0xD0, 0x48, 0x8D, 0x0D, 0x75, 0x11, 0xF0, 0x00 } },
			{ 0x15F8AC4, { 0x4C, 0x8D, 0x3D, 0xA5, 0x49, 0x9C, 0x0D, 0x66, 0x41, 0x3B, 0xC0, 0x74, 0x1E, 0x0F, 0xB7, 0xD0 } },
		};
		for (const auto& g : guards)
		{
			std::array<std::uint8_t, 16> bytes{};
			if (!safemem::RegionExecutable(reinterpret_cast<void*>(base + g.rva)) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(base + g.rva), bytes.data(), bytes.size()) || bytes != g.bytes)
			{
				LOG_ERR("NightGeometry", "native scheduler binding mismatch at RVA %llX", static_cast<unsigned long long>(g.rva));
				return hook::Status::Failed;
			}
		}
		for (const auto rva : { CountRva, CandidatesRva, CandidatesRva + MaxCandidates * 2 - 1 })
			if (!safemem::RegionWritable(reinterpret_cast<void*>(base + rva))) return hook::Status::Failed;
		imageBase = base;
		const auto status =
			hook::Install(reinterpret_cast<void*>(base + SortRva), &sort, guards[0].bytes.data(), guards[0].bytes.size(), original);
		if (status == hook::Status::Installed)
			LOG_INFO("NightGeometry", "known damaged render chunks excluded before native load requests");
		return status;
	}
}
