#pragma once
#include <cstddef>
#include <cstdint>
namespace commands
{
	enum class QueueFailure : std::uint8_t
	{
		None,
		InvalidArguments,
		DescriptorUnreadable,
		NullData,
		UnexpectedCapacity,
		InvalidSize,
		NoRoom,
		DataWriteFailed,
		SizeWriteFailed
	};

	struct QueueStatus
	{
		QueueFailure failure = QueueFailure::None;
		bool descriptorRead = false;
		bool dataWrite = false;
		bool sizeWrite = false;
		char* data = nullptr;
		int capacity = 0;
		int size = 0;
		std::size_t length = 0;
	};

	// Append to local client 0's native Cbuf under CRITSECT_CBUF (35).
	// Caller validates the critical-section helpers before using this function.
	bool Queue(uintptr_t moduleBase, const char* text, QueueStatus* status = nullptr);
	const char* QueueFailureName(QueueFailure failure);
}
