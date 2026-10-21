#pragma once
#include "auth/offline_identity.h"

#include <filesystem>
#include <string_view>
namespace offlineauth
{
	// A generated identity is local save data, not an INI configuration dependency.
	// Existing INIs are imported once; corrupt saves never generate a different XUID.
	class IdentityStore
	{
	public:
		bool Load(const std::filesystem::path& root, bool server, Identity& identity);
		bool Rename(Identity& identity, std::string_view name);
		static bool ValidName(std::string_view name);

	private:
		std::filesystem::path path_;
		bool server_ = false;
	};
}
