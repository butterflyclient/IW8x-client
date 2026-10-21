// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view body)
{
	if (!playlist_catalog::ValidRequest(body)) return { 400, R"({"error":"invalid playlist request"})" };
	std::lock_guard guard(lock);
	if (!server || !service) return { 503, R"({"error":"dedicated playlist catalog unavailable"})" };
	LOG_INFO("PlaylistBackend", "served HTTP catalog: bytes=%zu version=%d sha256=%s", service->text.size(), service->version,
		service->sha256.c_str());
	return { 200, serviceWire };
}
