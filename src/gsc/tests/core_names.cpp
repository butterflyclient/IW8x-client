// Test-only name boundary. The core registry tests do not exercise the native
// Replay canonical catalog; that catalog has a separate IDA evidence gate.
#include "gsc/internal/state.hpp"
namespace iw8x::gsc::internal
{
	uint32_t script_function_token(iw8x_string)
	{
		return 0;
	}

	uint32_t script_module_token(std::string_view)
	{
		return 0;
	}

	std::string_view script_function_name(uint32_t)
	{
		return "<fixture>";
	}
}
