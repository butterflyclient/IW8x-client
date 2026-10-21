#pragma once
#include <string_view>
namespace auto_server
{
	// Client startup launches/reuses a local idle dedicated process. Dedicated
	// startup retains the same lifetime marker and never recursively launches.
	void StartOrRetain();
	// Called before console/log initialization by a replacement host. The parent
	// PID and creation time must match this executable before waiting for its exit.
	bool WaitForPreviousHost();
	// After native shutdown: replace this dedicated host with a fresh idle host.
	// On success this exits the current dedicated process; false leaves it alive.
	bool RestartDedicated(std::wstring_view startup = {});
}
