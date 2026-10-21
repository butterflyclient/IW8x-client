#pragma once
#include "network/backend/publisher_backend.h"

#include <map>
#include <optional>
#include <set>
namespace publisher
{
	// Worker-owned, single-use startup downloads. They only become native state
	// after a real TaskManager request and the stock completion/dvar application.
	class StartupCache
	{
		std::wstring origin;

		struct Entry
		{
			std::string body;
			uint64_t at;
		};

		std::map<std::string, Entry> responses;
		std::set<std::string> requested;

	public:
		void Select(const std::wstring& value)
		{
			if (origin != value)
			{
				origin = value;
				responses.clear();
				requested.clear();
			}
		}

		bool Needed(const std::wstring& value, const std::string& name)
		{
			Select(value);
			return !requested.contains(name);
		}

		bool Store(const std::wstring& value, const std::string& name, unsigned status, const std::string& body, uint64_t now)
		{
			Select(value);
			if (status != 200 || Index(name) < 0 || requested.contains(name)) return false;
			try
			{
				if (!Response(Json::parse(body), name)) return false;
				responses[name] = { body, now };
				return true;
			}
			catch (const Json::exception&)
			{
				return false;
			}
		}

		std::optional<std::string> Take(const std::wstring& value, const std::string& name, uint64_t now)
		{
			Select(value);
			// Even a cache miss retires startup prefetch for this namespace. A
			// queued prefetch must never satisfy a later live-version refresh.
			requested.insert(name);
			auto found = responses.find(name);
			if (found == responses.end()) return {};
			auto entry = std::move(found->second);
			responses.erase(found);
			if (now < entry.at || now - entry.at > 30000) return {};
			return std::move(entry.body);
		}
	};
}
