#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace frontend_animtree128::pool
{
	// Replay's verified client animation-tree initializer creates 512 inline
	// 0x68-byte nodes, then stores the free-list head at context + 0xD000.
	inline constexpr size_t kInlineNodes = 512;
	inline constexpr size_t kExtraNodes = 128;
	inline constexpr size_t kNodeBytes = 0x68;
	inline constexpr size_t kInlineBytes = kInlineNodes * kNodeBytes;
	inline constexpr size_t kExtraBytes = kExtraNodes * kNodeBytes;
	inline constexpr size_t kCapacity = kInlineNodes + kExtraNodes;
	static_assert(kInlineBytes == 0xD000 && kCapacity == 640);

	inline bool IsInlineNode(const void* context, const void* node)
	{
		const uintptr_t base = reinterpret_cast<uintptr_t>(context);
		const uintptr_t address = reinterpret_cast<uintptr_t>(node);
		return address >= base && address < base + kInlineBytes && (address - base) % kNodeBytes == 0;
	}

	// A persistent sidecar supplies nodes through the engine's existing pointer
	// free list. The last extra node links to the original head, which may be the
	// in-structure sentinel at context + 0xD000. Nothing is moved or truncated.
	inline void* LinkExtraNodes(void* storage, void* originalHead)
	{
		auto* bytes = static_cast<uint8_t*>(storage);
		for (size_t index = 0; index < kExtraNodes; ++index)
		{
			void* next = index + 1 < kExtraNodes ? static_cast<void*>(bytes + (index + 1) * kNodeBytes) : originalHead;
			std::memcpy(bytes + index * kNodeBytes, &next, sizeof(next));
		}
		return storage;
	}
}
