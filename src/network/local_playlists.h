#pragma once
#include "network/backend/playlist_catalog.h"

#include <filesystem>
namespace playlists::local
{
	playlist_catalog::Snapshot Load(uintptr_t base, const std::filesystem::path& path = {});
}
