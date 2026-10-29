#include "client/match_client_capacity.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace
{
	using client::match_client_capacity::status;
	constexpr std::uintptr_t party = 0xC73ADE0;
	constexpr std::uintptr_t slots = party + 0x4BF34;
	constexpr std::uintptr_t getter = 0x118C7D0;
	constexpr std::uintptr_t setter = 0x13E97B0;
	constexpr std::uintptr_t ui_pointer = 0xD386878;
	constexpr std::uintptr_t allocated = 0xEEB0680;
	constexpr std::uintptr_t cached = 0xEEB0CE0;
	constexpr std::uintptr_t dvar = 0x1000;
	unsigned checks{}, setter_calls{};
	bool setter_accepts{};
	std::uintptr_t setter_dvar{};
	int setter_value{};

	void expect(bool condition, const char* message)
	{
		++checks;
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			std::exit(1);
		}
	}

	struct binding
	{
		std::uintptr_t rva;
		std::array<std::uint8_t, 24> bytes;
		std::size_t size;
	};

	const binding bindings[]{
		{ getter, { 0x8B, 0x81, 0x34, 0xBF, 0x04, 0x00, 0xC3 }, 7 },
		{ setter, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0x3B, 0xAC, 0x66, 0x04, 0x48, 0x33, 0xC4 }, 16 },
		{ 0x15E8D0F, { 0x48, 0x8B, 0x05, 0x62, 0xDB, 0xD9, 0x0B, 0x44, 0x8B, 0x78, 0x28, 0x83,
			0x3D, 0x5F, 0x79, 0x8C, 0x0D, 0x00, 0x0F, 0x85, 0xE8, 0x01, 0x00, 0x00 }, 24 },
		{ 0x15E8EE5, { 0x44, 0x89, 0x35, 0x94, 0x77, 0x8C, 0x0D, 0x44, 0x89, 0x3D, 0xED,
			0x7D, 0x8C, 0x0D, 0x44, 0x89, 0x2D, 0xEA, 0x7D, 0x8C, 0x0D }, 21 }
	};

	void native_setter(std::uintptr_t pointer, int value)
	{
		++setter_calls;
		setter_dvar = pointer;
		setter_value = value;
		if (setter_accepts) std::memcpy(reinterpret_cast<void*>(pointer + 40), &value, sizeof(value));
	}

	class fixture
	{
		std::uintptr_t base_{};

		void commit(std::uintptr_t rva)
		{
			const auto page = base_ + (rva & ~std::uintptr_t(0xFFF));
			expect(VirtualAlloc(reinterpret_cast<void*>(page), 4096, MEM_COMMIT, PAGE_READWRITE) != nullptr,
				"commit sparse native fixture page");
		}

	public:
		fixture()
		{
			base_ = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, 0xEF00000, MEM_RESERVE, PAGE_NOACCESS));
			expect(base_ != 0, "reserve sparse native image");
			for (const auto& site : bindings) commit(site.rva);
			for (const auto rva : { dvar, slots, ui_pointer, allocated, cached }) commit(rva);
			reset();
		}

		~fixture()
		{
			VirtualFree(reinterpret_cast<void*>(base_), 0, MEM_RELEASE);
		}

		std::uintptr_t base() const { return base_; }

		template <typename T>
		void put(std::uintptr_t rva, T value)
		{
			std::memcpy(reinterpret_cast<void*>(base_ + rva), &value, sizeof(value));
		}

		template <typename T>
		T get(std::uintptr_t rva) const
		{
			T value{};
			std::memcpy(&value, reinterpret_cast<const void*>(base_ + rva), sizeof(value));
			return value;
		}

		void protect(std::uintptr_t rva, DWORD permission)
		{
			DWORD previous{};
			expect(VirtualProtect(reinterpret_cast<void*>(base_ + (rva & ~std::uintptr_t(0xFFF))), 4096,
				permission, &previous) != FALSE, "protect fixture page");
		}

		void code(std::uintptr_t rva, const void* bytes, std::size_t size)
		{
			protect(rva, PAGE_READWRITE);
			std::memcpy(reinterpret_cast<void*>(base_ + rva), bytes, size);
			FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base_ + rva), size);
			protect(rva, PAGE_EXECUTE_READ);
		}

		void native_guards()
		{
			for (const auto& site : bindings) code(site.rva, site.bytes.data(), site.size);
		}

		void reset(int capacity = 102)
		{
			for (const auto rva : { dvar, slots, ui_pointer, allocated }) protect(rva, PAGE_READWRITE);
			put<int>(party + 311000, -2);
			put<int>(party + 0x4BF48, 0);
			put<int>(party + 0x4BF4C, 1);
			put<int>(slots, capacity);
			put<std::uintptr_t>(ui_pointer, base_ + dvar);
			put<std::uint8_t>(dvar + 29, 5);
			put<int>(dvar + 40, 20);
			put<int>(allocated, 0);
			put<int>(cached, 0);
			setter_calls = 0;
			setter_accepts = true;
			setter_dvar = 0;
			setter_value = 0;
			code(getter, bindings[0].bytes.data(), bindings[0].size);
			// Exercise the production helper's actual native call ABI. Only the
			// engine setter is stubbed; the seven-byte getter is the native body.
			std::array<std::uint8_t, 12> jump{ 0x48, 0xB8 };
			const auto destination = reinterpret_cast<std::uintptr_t>(&native_setter);
			std::memcpy(jump.data() + 2, &destination, sizeof(destination));
			jump[10] = 0xFF;
			jump[11] = 0xE0;
			code(setter, jump.data(), jump.size());
		}

		client::match_client_capacity::result run(unsigned flags = 1, bool authenticated = true)
		{
			const int old_allocated = get<int>(allocated), old_cached = get<int>(cached), old_slots = get<int>(slots);
			const auto value = client::match_client_capacity::prepare(base_, flags, authenticated);
			expect(get<int>(allocated) == old_allocated && get<int>(cached) == old_cached && get<int>(slots) == old_slots,
				"preparation never rewrites native allocation counters or party capacity");
			return value;
		}
	};

	void test_bindings(fixture& memory)
	{
		memory.native_guards();
		expect(client::match_client_capacity::validate(memory.base()), "all four native semantic bindings accepted");
		for (const auto& site : bindings)
		{
			const auto changed = std::uint8_t(site.bytes[0] ^ 1);
			memory.code(site.rva, &changed, 1);
			expect(!client::match_client_capacity::validate(memory.base()), "each changed native binding is rejected");
			memory.code(site.rva, site.bytes.data(), site.size);
		}
		memory.protect(getter, PAGE_NOACCESS);
		expect(!client::match_client_capacity::validate(memory.base()), "unreadable native binding is rejected");
		memory.protect(getter, PAGE_EXECUTE_READ);
		expect(!client::match_client_capacity::validate(0) && !client::match_client_capacity::validate(UINTPTR_MAX),
			"null or overflowing image base is rejected");
	}

	void test_preparation(fixture& memory)
	{
		for (const int capacity : { 102, 150, 1, 200 })
		{
			memory.reset(capacity);
			const auto result = memory.run();
			expect(result.success() && result.state == status::synchronized && result.party_capacity == capacity &&
				result.ui_before == 20 && result.allocated_clients == 0 && result.cached_capacity == 0 &&
				memory.get<int>(dvar + 40) == capacity && setter_calls == 1 &&
				setter_dvar == memory.base() + dvar && setter_value == capacity,
				"fresh allocation receives the actual authoritative capacity through the native setter");
		}
		memory.reset();
		memory.put<int>(dvar + 40, 102);
		expect(memory.run().state == status::unchanged && setter_calls == 0, "matching fresh capacity needs no setter");
		memory.reset();
		expect(memory.run(2).state == status::skipped && memory.run(3).state == status::skipped && setter_calls == 0,
			"frontend allocation bit preserves its native single-client capacity");
		expect(memory.run(1, false).state == status::skipped && setter_calls == 0,
			"cancelled or unrelated assignment cannot reuse an old Island capacity");
		expect(client::match_client_capacity::prepare(0, 1, false).success() &&
			client::match_client_capacity::prepare(0, 2, true).success(),
			"excluded paths do not access native memory");
		expect(!client::match_client_capacity::prepare(0, 1, true).success(), "assigned path refuses an invalid image");
		for (const auto& field : { std::pair<std::uintptr_t, int>{ party + 0x4BF4C, 0 },
			std::pair<std::uintptr_t, int>{ party + 0x4BF48, 1 }, std::pair<std::uintptr_t, int>{ party + 311000, 0 } })
		{
			memory.reset();
			memory.put<int>(field.first, field.second);
			expect(memory.run().state == status::skipped && setter_calls == 0,
				"absent, local-host or player-host party remains native");
		}
		for (const int capacity : { -1, 0, 201 })
		{
			memory.reset(capacity);
			const auto result = memory.run();
			expect(!result.success() && result.state == status::invalid_capacity && setter_calls == 0,
				"invalid received capacity cannot reach allocation");
		}
		memory.reset();
		memory.protect(slots, PAGE_NOACCESS);
		const auto unreadable = client::match_client_capacity::prepare(memory.base(), 1, true);
		memory.protect(slots, PAGE_READWRITE);
		expect(unreadable.state == status::unreadable_state && !unreadable.success() && setter_calls == 0,
			"unreadable assigned party fails before native calls");
		for (const auto pointer : { std::uintptr_t(0), memory.base() + 0x5000, UINTPTR_MAX })
		{
			memory.reset();
			memory.put<std::uintptr_t>(ui_pointer, pointer);
			expect(memory.run().state == status::invalid_dvar && setter_calls == 0,
				"null, unreadable or overflowing dvar pointer is refused");
		}
		memory.reset();
		memory.put<std::uint8_t>(dvar + 29, 0);
		expect(memory.run().state == status::invalid_dvar && setter_calls == 0, "non-integer dvar is refused");
		memory.reset();
		setter_accepts = false;
		expect(memory.run().state == status::update_failed && setter_calls == 1 && memory.get<int>(dvar + 40) == 20,
			"setter refusal is caught by current-value readback");
		memory.reset();
		memory.protect(setter, PAGE_READWRITE);
		expect(memory.run().state == status::update_failed && setter_calls == 0, "non-executable setter is not called");
		memory.reset();
		memory.put<int>(allocated, 1);
		memory.put<int>(cached, 102);
		expect(memory.run().state == status::unchanged && setter_calls == 0 && memory.get<int>(dvar + 40) == 20,
			"matching existing allocation is preserved even if UI state changed");
		memory.put<int>(cached, 20);
		const auto existing = memory.run();
		expect(!existing.success() && existing.state == status::existing_allocation_mismatch &&
			existing.allocated_clients == 1 && existing.cached_capacity == 20 && existing.party_capacity == 102 &&
			setter_calls == 0, "existing wrong-size hunk is rejected without pretending a dvar resizes it");
		memory.reset(150);
		expect(memory.run().state == status::synchronized && setter_value == 150,
			"later assignment reads new native capacity without an old cached override");
	}

	void test_installed_image(fixture& memory, const wchar_t* path)
	{
		std::ifstream stream{ std::filesystem::path(path), std::ios::binary };
		expect(bool(stream), "open installed Replay image");
		const auto read_at = [&](std::uint64_t offset, void* destination, std::size_t size)
		{
			stream.clear();
			stream.seekg(static_cast<std::streamoff>(offset));
			stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
			return stream.gcount() == static_cast<std::streamsize>(size);
		};
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS64 nt{};
		expect(read_at(0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
			dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 &&
			read_at(dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
			nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
			nt.FileHeader.NumberOfSections > 0 && nt.FileHeader.NumberOfSections <= 96,
			"installed image has a bounded x64 PE section table");
		std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
		const auto section_table = std::uint64_t(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
		expect(read_at(section_table, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER)), "read installed PE sections");
		for (const auto& site : bindings)
		{
			std::array<std::uint8_t, 24> bytes{};
			bool found = false;
			for (const auto& section : sections)
			{
				if (site.rva < section.VirtualAddress) continue;
				const auto offset = site.rva - section.VirtualAddress;
				if (offset > section.SizeOfRawData || site.size > section.SizeOfRawData - offset) continue;
				found = read_at(std::uint64_t(section.PointerToRawData) + offset, bytes.data(), site.size);
				break;
			}
			expect(found, "native semantic binding exists in the installed image");
			memory.code(site.rva, bytes.data(), site.size);
		}
		expect(client::match_client_capacity::validate(memory.base()), "all production byte guards match the installed Replay image");
	}
}

int wmain(int argc, wchar_t** argv)
{
	expect(argc <= 2, "usage: client_match_capacity_tests.exe [Replay executable]");
	fixture memory;
	test_bindings(memory);
	test_preparation(memory);
	if (argc == 2) test_installed_image(memory, argv[1]);
	std::printf("PASS: %u client match capacity checks%s\n", checks, argc == 2 ? " including installed PE bindings" : "");
	return 0;
}
