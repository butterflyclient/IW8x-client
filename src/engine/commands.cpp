#include "engine/commands.h"
#include "engine/game.h"
#include "engine/safemem.h"

#include <cstddef>
#include <cstring>
namespace commands
{
	namespace
	{
		// Replay's 64 KiB command text buffer straddles image-memory regions.  In the
		// exact 1.20 process observed at runtime, the buffer begins 0x130 bytes before
		// the end of a PAGE_READWRITE region and continues into PAGE_WRITECOPY memory.
		// safemem::WriteBytes intentionally validates one VirtualQuery region at a time,
		// so append the command in region-sized chunks while leaving the native Cbuf size
		// untouched until every chunk succeeds.
		bool WriteCbufText(char* dst, const char* src, size_t len)
		{
			if (!dst || !src || !len) return false;

			while (len)
			{
				MEMORY_BASIC_INFORMATION mbi{};
				if (VirtualQuery(dst, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
					return false;

				const DWORD prot = mbi.Protect & 0xff;
				const bool writable =
					prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
				if (!writable) return false;

				const size_t regionRoom = static_cast<size_t>((static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize) - dst);
				if (!regionRoom) return false;

				const size_t chunk = len < regionRoom ? len : regionRoom;
				if (!safemem::WriteBytes(dst, src, chunk)) return false;

				dst += chunk;
				src += chunk;
				len -= chunk;
			}
			return true;
		}
	}

	const char* QueueFailureName(QueueFailure failure)
	{
		switch (failure)
		{
		case QueueFailure::None:
			return "none";
		case QueueFailure::InvalidArguments:
			return "invalid-arguments";
		case QueueFailure::DescriptorUnreadable:
			return "descriptor-unreadable";
		case QueueFailure::NullData:
			return "null-data";
		case QueueFailure::UnexpectedCapacity:
			return "unexpected-capacity";
		case QueueFailure::InvalidSize:
			return "invalid-size";
		case QueueFailure::NoRoom:
			return "no-room";
		case QueueFailure::DataWriteFailed:
			return "data-write-failed";
		case QueueFailure::SizeWriteFailed:
			return "size-write-failed";
		default:
			return "unknown";
		}
	}

	bool Queue(uintptr_t moduleBase, const char* text, QueueStatus* status)
	{
		QueueStatus result{};
		if (!moduleBase || !text || !*text)
		{
			result.failure = QueueFailure::InvalidArguments;
			if (status) *status = result;
			return false;
		}

		result.length = std::strlen(text);

		struct CommandBuffer
		{
			char* data;
			int capacity;
			int size;
		};

		static_assert(offsetof(CommandBuffer, size) == 12);
		const auto enter = reinterpret_cast<game::CriticalSection_t>(moduleBase + game::kEnterCriticalRVA);
		const auto leave = reinterpret_cast<game::CriticalSection_t>(moduleBase + game::kLeaveCriticalRVA);
		enter(35);
		CommandBuffer buffer{};
		auto* address = reinterpret_cast<void*>(moduleBase + game::kConsoleBufferRVA);
		result.descriptorRead = safemem::ReadBytes(address, &buffer, sizeof(buffer));
		if (result.descriptorRead)
		{
			result.data = buffer.data;
			result.capacity = buffer.capacity;
			result.size = buffer.size;
		}

		bool queued = false;
		if (!result.descriptorRead)
		{
			result.failure = QueueFailure::DescriptorUnreadable;
		}
		else if (!buffer.data)
		{
			result.failure = QueueFailure::NullData;
		}
		else if (buffer.capacity != 0x10000)
		{
			result.failure = QueueFailure::UnexpectedCapacity;
		}
		else if (buffer.size < 0 || buffer.size >= buffer.capacity)
		{
			result.failure = QueueFailure::InvalidSize;
		}
		else if (result.length >= static_cast<size_t>(buffer.capacity - buffer.size))
		{
			result.failure = QueueFailure::NoRoom;
		}
		else
		{
			result.dataWrite = WriteCbufText(buffer.data + buffer.size, text, result.length + 1);
			if (!result.dataWrite)
			{
				result.failure = QueueFailure::DataWriteFailed;
			}
			else
			{
				const int next = buffer.size + static_cast<int>(result.length);
				result.sizeWrite = safemem::WriteBytes(static_cast<uint8_t*>(address) + offsetof(CommandBuffer, size), &next, sizeof(next));
				if (!result.sizeWrite)
				{
					result.failure = QueueFailure::SizeWriteFailed;
				}
				else
				{
					result.failure = QueueFailure::None;
					queued = true;
				}
			}
		}
		leave(35);

		if (status) *status = result;
		return queued;
	}
}
