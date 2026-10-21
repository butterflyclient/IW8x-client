// Shared provider implementation; included inside its existing namespace.
std::string Check(std::string_view text)
{
	std::lock_guard guard(mutex);
	return loaded ? text_validation::Check(text, policy) : "unavailable";
}
