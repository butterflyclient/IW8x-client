#pragma once
#include "network/profile/text_validation.h"
namespace name_validation
{
	bool Load();
	std::string Check(std::string_view text);
	unsigned Request(const text_validation::Json& request, text_validation::Json& response);
	void Reset();
	bool Submit(const std::string& text);

	struct View
	{
		bool pending = false, complete = false, allowed = false;
		std::string reason;
	};

	View Status();
	bool Command(std::string_view command, bool server);
}
