#pragma once
// Private module interfaces; never part of the plugin or exported ABI.
#include "dedicated/assets/bap/hooks.hpp"
#include "dedicated/assets/bap/asset_pool.hpp"
#include "dedicated/assets/bap/lifecycle.hpp"
#include "dedicated/assets/bap/diagnostics.hpp"
#include "dedicated/assets/bap/collision.hpp"
namespace bap::detail
{
	extern std::atomic<int> g_nativePoolsPrepared;
}
