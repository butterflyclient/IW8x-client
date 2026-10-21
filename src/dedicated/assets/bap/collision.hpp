#pragma once
#include "dedicated/assets/bap/types.hpp"
namespace bap::detail
{
	__declspec(noinline) void __fastcall GlassPieceStateChangedDetour(
		unsigned pieceIndex, int oldState, const float* damagePos, const float* damageDir);

	__declspec(noinline) void ProcessCollisionFastfiles(bool force);
}
