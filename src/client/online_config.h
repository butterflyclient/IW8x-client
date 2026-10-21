#pragma once

namespace online_config
{
	// Run once on the startup worker, before any online-service configuration reads.
	void synchronize() noexcept;
}
