#include "client/frontend/frontend_animtree128.h"
#include "common/console.h"
#include "client/frontend/frontend_animtree128_pool.h"
#include "client/frontend/frontend_animtree128_scene.h"
#include "network/native_network.h"
#include "engine/safemem.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
namespace frontend_animtree128
{
	namespace
	{
		// Verified Replay 1.20.4 image SHA-256 68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0.
		// RVA 0x167F5D0 constructs 512 inline 0x68-byte free-list nodes per context.
		// RVA 0x16802DA calls the parser at RVA 0xCA10F0 with max=0x200.
		// The client context table is 32 records of stride 0xD048 at RVA 0xF0798E0;
		// each context begins eight bytes into its record. Only selected 1.24/1.28
		// client_character trees are allowed a 128-node persistent sidecar.
		constexpr uintptr_t kParserRva = 0xCA10F0;
		constexpr uintptr_t kInitializerRva = 0x167F5D0;
		constexpr uintptr_t kClientTableRva = 0xF0798E0;
		constexpr size_t kClientRecordBytes = 0xD048;
		constexpr size_t kClientRecords = 32;
		constexpr size_t kFreeHeadOffset = 0xD000;
		constexpr size_t kCountOffset = 0xD028;
		constexpr char kTargetTree[] = "animtrees/client_character.atr";

		using ParserFn = void(__fastcall*)(const char*, const char*, void*, unsigned, int);
		using InitializerFn = void*(__fastcall*)(void*);
		std::atomic<ParserFn> g_parser{ nullptr };
		std::atomic<InitializerFn> g_initializer{ nullptr };
		std::mutex g_poolMutex;
		uintptr_t g_base = 0;

		struct Slot
		{
			void* context = nullptr;
			void* storage = nullptr; // VirtualAlloc'd for the lifetime of the process.
			bool attached = false;
		};

		std::array<Slot, kClientRecords> g_slots{};

		bool selected()
		{
			wchar_t path[MAX_PATH]{}, scene[32]{};
			if (!native_network::ConfigurationPath(path, MAX_PATH)) return false;
			GetPrivateProfileStringW(L"ui", L"frontend_scene", L"1.16", scene, 32, path);
			return SceneSelected(scene);
		}

		int context_index(const void* context)
		{
			const uintptr_t first = g_base + kClientTableRva + 8;
			const uintptr_t value = reinterpret_cast<uintptr_t>(context);
			if (value < first || value - first >= kClientRecords * kClientRecordBytes) return -1;
			const size_t offset = value - first;
			return offset % kClientRecordBytes == 0 ? static_cast<int>(offset / kClientRecordBytes) : -1;
		}

		// Before the first extension, the native free list must contain precisely
		// 512-count inline nodes and terminate at its in-structure sentinel. This is
		// a stronger check than trusting the asset name or the parser's max alone.
		bool native_free_list_valid(void* context, void* head, uint64_t count)
		{
			if (count > pool::kInlineNodes) return false;
			const auto sentinel = reinterpret_cast<uintptr_t>(context) + kFreeHeadOffset;
			void* current = head;
			for (size_t i = 0; i < pool::kInlineNodes - count; ++i)
			{
				if (!pool::IsInlineNode(context, current)) return false;
				void* next = nullptr;
				if (!safemem::ReadBytes(current, &next, sizeof(next))) return false;
				current = next;
			}
			return reinterpret_cast<uintptr_t>(current) == sentinel;
		}

		bool attach(Slot& slot, void* context)
		{
			auto* headAddress = static_cast<uint8_t*>(context) + kFreeHeadOffset;
			if (!safemem::RegionWritable(headAddress)) return false;
			void* head = nullptr;
			uint64_t count = 0;
			if (!safemem::ReadBytes(headAddress, &head, sizeof(head)) ||
				!safemem::ReadBytes(static_cast<uint8_t*>(context) + kCountOffset, &count, sizeof(count)) ||
				!native_free_list_valid(context, head, count))
				return false;

			if (!slot.storage)
			{
				slot.storage = VirtualAlloc(nullptr, pool::kExtraBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
				if (!slot.storage) return false;
			}
			std::memset(slot.storage, 0, pool::kExtraBytes);
			void* extendedHead = pool::LinkExtraNodes(slot.storage, head);
			if (!safemem::WriteBytes(headAddress, &extendedHead, sizeof(extendedHead))) return false;
			slot.context = context;
			slot.attached = true;
			return true;
		}

		bool prepare(void* context)
		{
			const int index = context_index(context);
			if (index < 0) return false;
			std::lock_guard<std::mutex> lock(g_poolMutex);
			Slot& slot = g_slots[static_cast<size_t>(index)];
			if (slot.attached && slot.context == context) return true;
			if (!attach(slot, context)) return false;
			LOG_INFO(
				"FrontendAnimTree128", "extended verified client_character free list to %zu nodes (context=%d)", pool::kCapacity, index);
			return true;
		}

		void* __fastcall initialize(void* record)
		{
			void* result = g_initializer.load(std::memory_order_acquire)(record);
			const int index = context_index(static_cast<uint8_t*>(record) + 8);
			if (index < 0) return result;
			std::lock_guard<std::mutex> lock(g_poolMutex);
			Slot& slot = g_slots[static_cast<size_t>(index)];
			if (slot.storage)
			{
				slot.attached = false;
				if (!attach(slot, static_cast<uint8_t*>(record) + 8))
					LOG_ERR("FrontendAnimTree128", "client_character sidecar reinitialization failed (context=%d)", index);
			}
			return result;
		}

		void __fastcall parse(const char* file, const char* tree, void* context, unsigned max, int mode)
		{
			char safeName[96]{};
			safemem::ReadString(file, safeName, sizeof(safeName));
			if (max == pool::kInlineNodes && std::strcmp(safeName, kTargetTree) == 0)
			{
				if (prepare(context))
					max = static_cast<unsigned>(pool::kCapacity);
				else
					LOG_ERR("FrontendAnimTree128", "client_character context validation failed; native limit retained");
			}
			g_parser.load(std::memory_order_acquire)(file, tree, context, max, mode);
		}
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (!selected()) return hook::Status::Installed;
		if (g_parser.load(std::memory_order_acquire) && g_initializer.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = moduleBase;
		static constexpr uint8_t kParserBytes[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x49, 0x8D, 0xAB, 0x38, 0xFF,
			0xFF, 0xFF };
		static constexpr uint8_t kInitializerBytes[] = { 0x48, 0x8D, 0x51, 0x08, 0x4C, 0x8D, 0x82, 0x00, 0xD0, 0x00, 0x00, 0x48, 0x8D, 0x82,
			0x00, 0xD0, 0x00, 0x00 };
		hook::Batch hooks;
		hooks.Add(reinterpret_cast<void*>(moduleBase + kParserRva), &parse, kParserBytes, sizeof(kParserBytes), g_parser);
		hooks.Add(reinterpret_cast<void*>(moduleBase + kInitializerRva), &initialize, kInitializerBytes, sizeof(kInitializerBytes),
			g_initializer);
		const auto result = hooks.Commit();
		if (result == hook::Status::Installed)
			LOG_INFO("FrontendAnimTree128", "verified client_character animation pool compatibility installed");
		return result;
	}

	bool Active() noexcept
	{
		return g_parser.load(std::memory_order_acquire) && g_initializer.load(std::memory_order_acquire);
	}
}
