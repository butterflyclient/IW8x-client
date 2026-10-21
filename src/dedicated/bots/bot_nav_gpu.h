#pragma once
#include <cstdint>
namespace botnav
{
	void BeginQuery();
	void EndQuery();
	void ContextCreated(uintptr_t context, uintptr_t graph, unsigned source, uintptr_t filter);
	using NativeReachable = bool (*)(uintptr_t, int);
	bool Reachable(uintptr_t context, int target, NativeReachable native);
	void Command(const char* option);
}
