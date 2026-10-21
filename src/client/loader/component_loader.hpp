#pragma once
#include "component_interface.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>

class component_loader final
{
public:
	template <typename T>
	class installer final
	{
		static_assert(std::is_base_of_v<component_interface, T>, "component has invalid base class");

	public:
		installer()
		{
			register_component(std::make_unique<T>());
		}

		explicit installer(const std::string& component_name)
		{
			const auto last = component_name.find("::component");
			register_component(std::make_unique<T>(), component_name.substr(0, last));
		}
	};

	static void register_component(std::unique_ptr<component_interface>&& component, const std::string& name = "(unnamed)");
	static void sort();

	// Replay bootstrap serializes lifecycle calls. Callbacks run without a loader
	// lock, and successful components are not called again when a stage retries.
	static hook::Status post_start();
	static hook::Status post_load();
	static hook::Status post_unpack();
	// Invoke outside DllMain, after consumers have stopped submitting new work.
	static void pre_destroy();

	static std::size_t component_count() noexcept;
	static const char* active_component() noexcept;

private:
	static hook::Status dispatch(unsigned stage);
};

#define REGISTER_COMPONENT(name) \
namespace \
{ \
	static component_loader::installer<name> registered_component{ #name }; \
}
