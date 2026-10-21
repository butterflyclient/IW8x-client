#include "client/loader/component_loader.hpp"
#include "engine/runtime_signatures.hpp"
#include <windows.h>

namespace signatures
{
	class component final : public component_interface
	{
	public:
		hook::Status post_start() override
		{
			wchar_t path[MAX_PATH]{};
			const auto size = GetModuleFileNameW(nullptr, path, MAX_PATH);
			return size && size < MAX_PATH && runtime_signatures::initialize(path)
				? hook::Status::Installed : hook::Status::Failed;
		}
		component_priority priority() override { return component_priority::signatures; }
	};
}
REGISTER_COMPONENT(signatures::component)
