#pragma once
#include "plugin/sdk/abi.h"

#include <filesystem>
#include <string>
#include <vector>
namespace iw8x::plugins
{
	struct manifest
	{
		std::string name, version;
		uint32_t api{};
		std::filesystem::path entry;
	};

	manifest read_manifest(const std::filesystem::path& path);

	struct loaded_plugin
	{
		iw8x_plugin_id id{};
		std::string name;
		std::filesystem::path path;
	};

	void discover(const std::filesystem::path& root);
	iw8x_status unload(iw8x_plugin_id id);
	void unload_all();
	std::vector<loaded_plugin> loaded();
}
