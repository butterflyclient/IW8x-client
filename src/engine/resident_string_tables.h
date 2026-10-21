#pragma once
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>
namespace resident_tables
{
	using Table = std::vector<std::vector<std::string>>;

	// Verified Replay StringTable asset (type54). Free pool entries can contain
	// freelist pointers instead of names, so validate all indirect reads.
	struct Asset
	{
		const char* name;
		int columns, rows, values, padding;
		const uint16_t* cells;
		const void* hashes;
		const char* const* strings;
	};

	static_assert(sizeof(Asset) == 48 && offsetof(Asset, strings) == 40);

	// Construct only while the caller owns a stable resident asset pool. This view
	// and its permission cache must never survive a frame or map unload. Returned
	// tables own their strings; no native asset pointers escape into the catalog.
	class Reader
	{
		std::map<uintptr_t, uintptr_t> regions;
		std::map<std::string, Asset> assets;
		size_t queries = 0;

		uintptr_t RegionEnd(uintptr_t address)
		{
			if (!address) return 0;
			auto at = regions.upper_bound(address);
			if (at != regions.begin() && address < std::prev(at)->second) return std::prev(at)->second;
			MEMORY_BASIC_INFORMATION mbi{};
			++queries;
			if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT ||
				(mbi.Protect & PAGE_GUARD))
				return 0;
			const auto p = mbi.Protect & 0xFF;
			if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
				p != PAGE_EXECUTE_WRITECOPY)
				return 0;
			const auto begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			if (begin > UINTPTR_MAX - mbi.RegionSize) return 0;
			const auto end = begin + mbi.RegionSize;
			regions.emplace(begin, end);
			return end;
		}

		bool Readable(const void* data, size_t size)
		{
			auto at = reinterpret_cast<uintptr_t>(data);
			if (!at || !size || at > UINTPTR_MAX - size) return false;
			const auto end = at + size;
			while (at < end)
			{
				const auto regionEnd = RegionEnd(at);
				if (regionEnd <= at) return false;
				at = std::min(end, regionEnd);
			}
			return true;
		}

		bool String(const char* data, size_t bound, std::string& out)
		{
			out.clear();
			auto at = reinterpret_cast<uintptr_t>(data);
			if (!at || at > UINTPTR_MAX - bound) return false;
			while (bound)
			{
				const auto end = RegionEnd(at);
				if (end <= at) return false;
				const size_t size = std::min<size_t>(bound, end - at);
				const auto* text = reinterpret_cast<const char*>(at);
				const auto* zero = static_cast<const char*>(memchr(text, 0, size));
				out.append(text, zero ? size_t(zero - text) : size);
				if (zero) return true;
				at += size;
				bound -= size;
			}
			return false;
		}

		static std::string Key(std::string name)
		{
			for (auto& c : name)
				if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			return name;
		}

	public:
		Reader(const Asset* pool, int count)
		{
			if (count < 1 || count > 8192 || !Readable(pool, size_t(count) * sizeof(Asset))) return;
			for (int i = 0; i < count; ++i)
			{
				std::string name;
				if (String(pool[i].name, 256, name) && !name.empty()) assets.emplace(Key(std::move(name)), pool[i]);
			}
		}

		size_t Indexed() const
		{
			return assets.size();
		}

		size_t Queries() const
		{
			return queries;
		}

		Table Read(const std::string& name)
		{
			const auto found = assets.find(Key(name));
			if (found == assets.end()) return {};
			const auto& a = found->second;
			if (a.columns < 1 || a.columns > 256 || a.rows < 1 || a.rows > 20000 || a.values < 1 || a.values > 65536) return {};
			const size_t count = size_t(a.rows) * a.columns;
			if (!Readable(a.cells, count * sizeof(uint16_t)) || !Readable(a.strings, size_t(a.values) * sizeof(const char*))) return {};
			// Each interned string is validated/copied once, rather than once for
			// every cell referencing it. Reject corrupt or unterminated strings.
			std::vector<std::string> strings(a.values);
			size_t bytes = 0;
			for (int i = 0; i < a.values; ++i)
			{
				if (!String(a.strings[i], 16384, strings[i])) return {};
				bytes += strings[i].size();
				if (bytes > 64 * 1024 * 1024) return {};
			}
			Table table(a.rows);
			for (int r = 0; r < a.rows; ++r)
			{
				table[r].reserve(a.columns);
				for (int c = 0; c < a.columns; ++c)
				{
					const auto index = a.cells[size_t(r) * a.columns + c];
					if (index >= a.values) return {};
					bytes += strings[index].size();
					if (bytes > 64 * 1024 * 1024) return {};
					table[r].push_back(strings[index]);
				}
			}
			return table;
		}
	};
}
