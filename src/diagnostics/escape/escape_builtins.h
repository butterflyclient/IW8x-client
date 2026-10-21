#pragma once
#include "diagnostics/night/night_builtin_tables.h"
namespace escape_builtins
{
	// Development diagnostics can resolve missing imports to explicit errors.
	// Those entries never report success and are not implemented game functions.
	bool Configure(uintptr_t base, night_builtins::Tables& tables, bool diagnosticErrors);
}
