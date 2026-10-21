#include "component_loader.hpp"

#include "common/console.h"

#include <algorithm>
#include <array>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	struct component_entry
	{
		std::unique_ptr<component_interface> component;
		std::string name;
		unsigned completed{};
	};

	struct loader_state
	{
		std::vector<component_entry> components;
		unsigned completed{};
		bool sorted{};
		bool started{};
		bool failed{};
		bool dispatching{};
		bool destroyed{};
		const char* active_component = "(none)";
	};

	loader_state& state()
	{
		// Destruction only deletes adapter objects. Shutdown is an explicit stage
		// so worker joins never happen implicitly under the Windows loader lock.
		static loader_state instance;
		return instance;
	}

	using stage_callback = hook::Status (component_interface::*)();
	constexpr std::array callbacks{
		&component_interface::post_start,
		&component_interface::post_load,
		&component_interface::post_unpack,
	};
	constexpr std::array stage_names{ "post_start", "post_load", "post_unpack" };
}

void component_loader::register_component(std::unique_ptr<component_interface>&& component, const std::string& name)
{
	auto& registry = state();
	if (registry.sorted || registry.started || registry.destroyed)
	{
		throw std::logic_error("component registration is closed");
	}
	if (!component)
	{
		throw std::invalid_argument("cannot register a null component");
	}
	registry.components.push_back({ std::move(component), name, 0 });
}

void component_loader::sort()
{
	auto& registry = state();
	if (registry.sorted) return;
	std::stable_sort(registry.components.begin(), registry.components.end(),
		[](const component_entry& first, const component_entry& second)
		{
			return first.component->priority() > second.component->priority();
		});
	registry.sorted = true;
}

hook::Status component_loader::dispatch(const unsigned stage)
{
	auto& registry = state();
	if (registry.failed || registry.destroyed) return hook::Status::Failed;
	const auto stage_bit = 1u << stage;
	if (registry.completed & stage_bit) return hook::Status::Installed;
	if (registry.dispatching || (registry.completed & (stage_bit - 1)) != stage_bit - 1)
	{
		return hook::Status::NotReady;
	}

	registry.dispatching = true;
	struct reset
	{
		bool& value;

		~reset()
		{
			value = false;
		}
	} guard{ registry.dispatching };

	try
	{
		sort();
		registry.started = true;
		for (auto& entry : registry.components)
		{
			if (entry.completed & stage_bit) continue;
			registry.active_component = entry.name.c_str();
			const auto result = (entry.component.get()->*callbacks[stage])();
			if (result == hook::Status::NotReady) return result;
			if (result != hook::Status::Installed)
			{
				registry.failed = true;
				LOG_ERR("Components", "%s failed during %s", registry.active_component, stage_names[stage]);
				return hook::Status::Failed;
			}
			entry.completed |= stage_bit;
		}
		registry.completed |= stage_bit;
		registry.active_component = "(none)";
		return hook::Status::Installed;
	}
	catch (const std::exception& error)
	{
		LOG_ERR("Components", "%s threw during %s: %s", registry.active_component, stage_names[stage], error.what());
	}
	catch (...)
	{
		LOG_ERR("Components", "%s threw during %s", registry.active_component, stage_names[stage]);
	}
	registry.failed = true;
	return hook::Status::Failed;
}

hook::Status component_loader::post_start()
{
	return dispatch(0);
}

hook::Status component_loader::post_load()
{
	return dispatch(1);
}

hook::Status component_loader::post_unpack()
{
	return dispatch(2);
}

void component_loader::pre_destroy()
{
	auto& registry = state();
	if (registry.destroyed) return;
	registry.destroyed = true;
	for (auto entry = registry.components.rbegin(); entry != registry.components.rend(); ++entry)
	{
		if (!entry->completed) continue;
		registry.active_component = entry->name.c_str();
		try
		{
			entry->component->pre_destroy();
		}
		catch (const std::exception& error)
		{
			LOG_ERR("Components", "%s threw during pre_destroy: %s", registry.active_component, error.what());
		}
		catch (...)
		{
			LOG_ERR("Components", "%s threw during pre_destroy", registry.active_component);
		}
	}
	registry.active_component = "(none)";
}

std::size_t component_loader::component_count() noexcept
{
	return state().components.size();
}

const char* component_loader::active_component() noexcept
{
	return state().active_component;
}
