#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
namespace night_physics
{
	enum class GateKind
	{
		Collision,
		Created,
		Released
	};

	// Interior engine hook: preserve all volatile GPRs, flags and XMM0..5.
	// The callback follows the Windows x64 ABI and preserves nonvolatile registers.
	// Trampoline lives in a separate RW atomic slot, published by hook::Batch
	// before activation. Code is made RX once and is never patched while live.
	inline std::vector<std::uint8_t> MakeStub(
		GateKind kind, uintptr_t address, uintptr_t callback, uintptr_t originalSlot, uintptr_t reject = 0)
	{
		std::vector<std::uint8_t> code;
		auto bytes = [&](std::initializer_list<std::uint8_t> b)
		{
			code.insert(code.end(), b);
		};
		auto dword = [&](std::uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
				code.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
		};
		auto qword = [&](uintptr_t v)
		{
			for (int i = 0; i < 8; ++i)
				code.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
		};
		bytes({ 0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 });
		bytes({ 0x48, 0x89, 0xE0, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x81, 0xEC });
		dword(0x90);
		bytes({ 0x48, 0x89, 0x44, 0x24, 0x20 });
		for (int i = 0; i < 6; ++i)
		{ // disp32 also encodes the +80h slot without sign extension.
			bytes({ 0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x84 + i * 8), 0x24 });
			dword(0x30 + i * 16);
		}
		if (kind == GateKind::Collision)
		{
			bytes({ 0x4C, 0x89, 0xE9, 0x4C, 0x89, 0xF2 }); // RCX=R13 pipeline, RDX=R14 shape
			bytes({ 0x44, 0x8B, 0x80 });
			dword(0xB0); // particle = original RSP+70h
			bytes({ 0x44, 0x8B, 0x88 });
			dword(0xA0); // world = original RSP+60h
		}
		else if (kind == GateKind::Created)
		{
			bytes({ 0x48, 0x89, 0xF1, 0x4C, 0x89, 0xCA, 0x41, 0x89, 0xF8 }); // RSI,R9,EDI
		}
		else
			bytes({ 0x48, 0x89, 0xF9 }); // RDI pipeline, after collider destruction
		bytes({ 0x48, 0xB8 });
		qword(callback);
		bytes({ 0xFF, 0xD0 });
		size_t branch{};
		if (kind == GateKind::Collision)
		{
			bytes({ 0x84, 0xC0, 0x0F, 0x84 });
			branch = code.size();
			dword(0);
		}
		auto restore = [&]
		{
			for (int i = 0; i < 6; ++i)
			{
				bytes({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x84 + i * 8), 0x24 });
				dword(0x30 + i * 16);
			}
			bytes({ 0x48, 0x8B, 0x64, 0x24, 0x20, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x9D });
		};
		restore();
		bytes({ 0xFF, 0x25 });
		const auto delta = static_cast<std::int64_t>(originalSlot) - static_cast<std::int64_t>(address + code.size() + 4);
		if (delta < INT32_MIN || delta > INT32_MAX) return {};
		dword(static_cast<std::uint32_t>(delta));
		if (kind == GateKind::Collision)
		{
			const auto offset = static_cast<std::uint32_t>(code.size() - branch - 4);
			std::memcpy(code.data() + branch, &offset, 4);
			restore();
			bytes({ 0xFF, 0x25, 0, 0, 0, 0 });
			qword(reject);
		}
		return code;
	}
}
