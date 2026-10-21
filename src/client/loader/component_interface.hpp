#pragma once

#include "engine/inline_hook.h"

enum class component_priority
{
	min = 0,
	plugins = 10,
	headless = 20,
	client_runtime = 30,
	progression = 40,
	playlists = 50,
	online_services = 60,
	content = 70,
	joins = 80,
	authentication = 90,
	dvars = 100,
	scheduler = 200,
	signatures = 210,
};

// Replay can expose a target before its bytes are ready. Unlike IW7's void
// stages, an unfinished component must retain a retry without replaying owners
// that already installed their detours.
class component_interface
{
public:
	virtual ~component_interface() = default;
	virtual hook::Status post_start() { return hook::Status::Installed; }
	virtual hook::Status post_load() { return hook::Status::Installed; }
	virtual hook::Status post_unpack() { return hook::Status::Installed; }
	virtual void pre_destroy() {}
	virtual component_priority priority() { return component_priority::min; }
};
