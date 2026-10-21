#pragma once
#include "api.hpp"
namespace iw8x::gsc::internal
{
	iw8x_status argument_count(uint64_t, uint32_t*);
	iw8x_status argument(uint64_t, uint32_t, iw8x_value_callback, void*);
	iw8x_status return_value(uint64_t, const iw8x_value*);
	iw8x_status error(uint64_t, iw8x_string);
}
