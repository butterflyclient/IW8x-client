#pragma once
#include <string>
#include <vector>

#include "network/commerce/store_catalog.h"
namespace store_backend
{
	struct Row
	{
		std::string id, name, bundles;
	};

	struct View
	{
		bool ready = false;
		uint32_t revision = 0;
		std::vector<Row> rows;
	};

	bool Load();
	bool Reply(store_catalog::Json& response);
	bool Published(uint32_t bundle);
	void Reset();
	void Pump();
	View Get();
	bool Command(std::string_view command, bool server);
}
