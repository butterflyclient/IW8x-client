// Shared provider implementation; included inside its existing namespace.
unsigned Request(const Json& request, Json& response)
{
	try
	{
		if (request.at("kind") != "custom_mod_name") return 400;
		const auto result = Check(request.at("text").get<std::string>());
		if (result == "unavailable") return 503;
		response["text_validation"] = { { "kind", "custom_mod_name" }, { "allowed", result == "allowed" }, { "reason", result } };
		return 200;
	}
	catch (...)
	{
		return 400;
	}
}
