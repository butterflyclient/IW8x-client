#include "runtime.hpp"
#include "replay.hpp"

#include "engine/image.hpp"

#include <cstring>
namespace iw8x::gsc::internal
{
	bool typed_dvars_available()
	{
		return true;
	}

	iw8x_status read_typed_dvar(std::string_view name, uint32_t type, value& output)
	{
		if (!in_game_scope()) return IW8X_WRONG_THREAD;
		if (name.empty() || name.size() > 127 || name.find('\0') != std::string_view::npos) return IW8X_INVALID_ARGUMENT;
		char key[128]{};
		std::memcpy(key, name.data(), name.size());
		using namespace replay;
		const auto* dvar = reinterpret_cast<void* (*)(const char*)>(game::at(Dvar_FindVarByName.rva))(key);
		if (!dvar) return IW8X_NOT_FOUND;
		// Replay dvar type is +0x1D. Values are read only
		// through the engine's accessors, including retail value deobfuscation.
		const auto native_type = *(static_cast<const uint8_t*>(dvar) + replay::layout::dvar_type);
		switch (type)
		{
		case IW8X_BOOLEAN:
			if (native_type != 0) return IW8X_TYPE_MISMATCH;
			output = value(reinterpret_cast<bool (*)(const char*)>(game::at(Dvar_GetBoolSafe.rva))(key));
			break;
		case IW8X_INTEGER:
			if (native_type != 5 && native_type != 8) return IW8X_TYPE_MISMATCH;
			output = value(reinterpret_cast<int32_t (*)(const char*)>(game::at(Dvar_GetIntSafe.rva))(key));
			break;
		case IW8X_NUMBER:
			if (native_type != 1) return IW8X_TYPE_MISMATCH;
			output = value(reinterpret_cast<float (*)(const char*)>(game::at(Dvar_GetFloatSafe.rva))(key));
			break;
		case IW8X_STRING: {
			const auto* text = reinterpret_cast<const char* (*)(const char*)>(game::at(Dvar_GetVariantString.rva))(key);
			if (!text) return IW8X_INTERNAL_ERROR;
			const auto size = strnlen_s(text, 4096);
			if (size == 4096) return IW8X_LIMIT;
			output = value(std::string_view(text, size));
			break;
		}
		default:
			return IW8X_INVALID_ARGUMENT;
		}
		return IW8X_OK;
	}
}
