#include "auth/profile/challenge_transport.h"

#include "engine/addresses.hpp"

#include "auth/offline_auth.h"

#include "engine/safemem.h"

#include "common/console.h"

#include <atomic>
#include <array>
#include <charconv>
#include <ctime>

#include <bcrypt.h>

#include <deque>
namespace challenge_transport
{
	namespace
	{
		uintptr_t g_base = 0;
		uint64_t g_session = 0, g_sequence = 0;
		Receiver g_receiver = nullptr;
		bool g_enabled = false, g_dedicated = false;
		using FinishFn = int64_t (*)();
		using DispatchFn = void (*)(int);
		std::atomic<FinishFn> g_finish{ nullptr };
		std::atomic<DispatchFn> g_dispatch{ nullptr };

		struct Capture
		{
			int slot = -1;
			std::optional<challenges::Event> event;
		};

		thread_local Capture g_capture;
		challenges::Assembly g_assembly;

		struct Pending
		{
			challenges::Event event;
			std::string bytes;
		};

		std::array<std::deque<Pending>, 200> g_pending;

		template <class F>
		F Native(uintptr_t rva)
		{
			return reinterpret_cast<F>(g_base + rva);
		}

		template <class T>
		T Read(uintptr_t rva)
		{
			return *reinterpret_cast<const T*>(g_base + rva);
		}

		bool Human(int slot)
		{
			if (slot < 0 || slot >= 200 || slot >= Read<int>(0xe17fcd0)) return false;
			const auto client = Read<uintptr_t>(0xe17f690 + 8ull * slot);
			return client && *reinterpret_cast<const int*>(client + 214756) != 2 && Native<bool (*)(uintptr_t)>(0x13761e0)(client);
		}

		template <class T>
		bool Parse(std::string_view text, T& value)
		{
			const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
			return !text.empty() && r.ec == std::errc{} && r.ptr == text.data() + text.size();
		}

		void CaptureRecord()
		{
			if (!Human(g_capture.slot)) return;
			const auto* ring = Read<const uint8_t*>(0xc4ea830);
			if (!ring) return;
			const auto* cursors = reinterpret_cast<const uint32_t*>(ring);
			const uint32_t start = cursors[32772], end = cursors[32770], count = cursors[32768];
			const uint32_t size = end - start;
			if (size < 14 || size > 8192 || count > 24) return;
			// Snapshot BEFORE the native release/publish in10ADEA0. Its consumer can
			// clear a published record on another thread, so reading after Report is a race.
			std::string bytes(size - 12, '\0');
			for (uint32_t i = 12; i < size; ++i)
				bytes[i - 12] = char(ring[(start + i) & 0x1ffff]);
			size_t cursor = 0;
			challenges::Event event;
			for (unsigned i = 0; i < count + 2; ++i)
			{
				const auto stop = bytes.find('\0', cursor);
				if (stop == bytes.npos) return;
				const auto text = std::string_view(bytes).substr(cursor, stop - cursor);
				if (i == 0)
				{
					if (!Parse(text, event.xuid) || !event.xuid) return;
				}
				else if (i == 1)
					event.name = std::string(text);
				else
					event.parameters.emplace_back(text);
				cursor = stop + 1;
			}
			if (cursor != bytes.size()) return;
			event.session = g_session;
			event.sequence = ++g_sequence;
			event.match = Read<uint32_t>(0xe17fdc8);
			const auto server = Read<uintptr_t>(0xe17fe10);
			event.milliseconds = server ? *reinterpret_cast<const uint32_t*>(server + 0x1c) : 0;
			event.utc = uint32_t(std::time(nullptr));
			g_capture.event = std::move(event);
		}

		int64_t Finish()
		{
			CaptureRecord();
			return g_finish.load(std::memory_order_acquire)();
		}

		void Dispatch(int client)
		{
			const auto argv = Native<const char* (*)(int)>(0x1296960);
			if (!challenges::IsEventCommand(argv(0)))
			{
				g_dispatch.load(std::memory_order_acquire)(client);
				return;
			}
			if (client != 0 || Native<int (*)()>(0x12968b0)() != 6) return;
			uint64_t session = 0, sequence = 0;
			unsigned part = 0, count = 0;
			if (!Parse(std::string_view(argv(1)), session) || !Parse(std::string_view(argv(2)), sequence) ||
				!Parse(std::string_view(argv(3)), part) || !Parse(std::string_view(argv(4)), count))
				return;
			const auto event = g_assembly.Add(session, sequence, part, count, argv(5));
			if (event && event->xuid == offlineauth::GetIdentity().xuid && g_receiver) g_receiver(*event);
		}
	}

	hook::Status Install(uintptr_t base, bool dedicated, Receiver receiver)
	{
		g_base = base;
		g_receiver = receiver;
		g_dedicated = dedicated;
		if (dedicated)
		{
			if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&g_session), sizeof(g_session), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
				!g_session)
				return hook::Status::Failed;
			constexpr uint8_t sender[] = { 0x48, 0x83, 0xec, 0x38, 0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x1f, 0x44, 0x00, 0x00 };
			uint8_t actual[sizeof(sender)]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x137c670), actual, sizeof(actual)) ||
				memcmp(actual, sender, sizeof(sender)))
				return hook::Status::Failed;
			constexpr uint8_t finish[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56 };
			const auto status = hook::Install(reinterpret_cast<void*>(base + 0x10adea0), &Finish, finish, sizeof(finish), g_finish);
			g_enabled = status == hook::Status::Installed;
			return status;
		}
		constexpr uint8_t argv[] = { 0x48, 0x63, 0x05, 0x69, 0x62, 0xf7, 0x0b, 0x48, 0x8d, 0x15, 0x62, 0x62, 0xf7, 0x0b };
		constexpr uint8_t argc[] = { 0x48, 0x63, 0x05, 0x19, 0x63, 0xf7, 0x0b, 0x48, 0x8d, 0x0d, 0x56, 0x63, 0xf7, 0x0b };
		if (memcmp(reinterpret_cast<void*>(base + 0x1296960), argv, sizeof(argv)) ||
			memcmp(reinterpret_cast<void*>(base + 0x12968b0), argc, sizeof(argc)))
			return hook::Status::Failed;
		constexpr uint8_t dispatch[] = { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24 };
		const auto status = hook::Install(reinterpret_cast<void*>(base + 0x15a26a0), &Dispatch, dispatch, sizeof(dispatch), g_dispatch);
		g_enabled = status == hook::Status::Installed;
		return status;
	}

	void BeginReport(uint64_t ref)
	{
		g_capture = {};
		if (g_enabled && g_dedicated && ((ref >> 32) & 0xff) == 0 && uint32_t(ref) < 200) g_capture.slot = int(uint32_t(ref));
	}

	void EndReport()
	{
		const auto capture = std::move(g_capture);
		g_capture = {};
		if (!capture.event || !Human(capture.slot)) return;
		if (g_receiver)
		{
			g_receiver(*capture.event);
			return;
		}
		const auto bytes = challenges::Pack(*capture.event);
		if (bytes.empty()) return;
		auto& queue = g_pending[capture.slot];
		if (!queue.empty() && (queue.front().event.xuid != capture.event->xuid || queue.front().event.match != capture.event->match))
			queue.clear();
		// Stay on the verified server script thread. Queue transient congestion and
		// retry on subsequent reports; never send from the frontend/main-frame hook.
		if (queue.size() >= 256)
		{
			LOG_ERR("Challenges", "persistent reliable congestion | slot=%d; newest event rejected", capture.slot);
			return;
		}
		queue.push_back({ *capture.event, bytes });
		const auto client = Read<uintptr_t>(0xe17f690 + 8ull * capture.slot);
		while (!queue.empty())
		{
			const auto& pendingEvent = queue.front();
			const unsigned count = unsigned((pendingEvent.bytes.size() + 399) / 400);
			const uint32_t pending =
				*reinterpret_cast<const uint32_t*>(client + 179584) - *reinterpret_cast<const uint32_t*>(client + 179588);
			if (pending + count >= 240) return; // leave capacity for stock gameplay commands
			for (unsigned i = 0; i < count; ++i)
			{
				const auto command = challenges::EventCommand(pendingEvent.event.session, pendingEvent.event.sequence, i, count,
					challenges::Hex(std::string_view(pendingEvent.bytes).substr(i * 400, 400)));
				Native<void (*)(uintptr_t, int, const char*)>(0x137c670)(client, 2, command.c_str());
			}
			queue.pop_front();
		}
	}
}
