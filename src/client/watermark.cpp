#include "client/watermark.h"
#include "engine/engine.hpp"

namespace watermark
{
	void draw()
	{
		engine::draw_text_top_right("IW8x", 0.375f, 0.25f);
	}
}
