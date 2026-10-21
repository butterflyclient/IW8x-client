#include "loader.hpp"
#include "thirdparty/json.hpp"

#include <fstream>
#include <set>
#include <stdexcept>
namespace iw8x::plugins
{
	manifest read_manifest(const std::filesystem::path& path)
	{
		if (std::filesystem::file_size(path) > 64 * 1024) throw std::runtime_error("plugin manifest exceeds 64 KiB");
		std::ifstream stream(path, std::ios::binary);
		if (!stream) throw std::runtime_error("cannot open plugin manifest");
		std::set<std::string> keys;
		const auto j = nlohmann::json::parse(stream,
			[&](int depth, auto event, auto& parsed)
			{
				if (event == nlohmann::json::parse_event_t::key && depth == 1 && !keys.insert(parsed.template get<std::string>()).second)
					throw std::runtime_error("duplicate manifest key");
				if (depth > 4) throw std::runtime_error("manifest nesting is not supported");
				return true;
			});
		if (!j.is_object() || j.size() != 4 || !j.contains("name") || !j.contains("version") || !j.contains("api") ||
			!j.contains("entry") || !j["name"].is_string() || !j["version"].is_string() || !j["entry"].is_string() ||
			!j["api"].is_number_unsigned())
			throw std::runtime_error("manifest requires name, version, api, entry");
		manifest out{ j["name"].get<std::string>(), j["version"].get<std::string>(), 0, {} };
		const auto version = j["api"].get<uint64_t>();
		if (version != IW8X_PLUGIN_API_VERSION) throw std::runtime_error("unsupported plugin API version");
		out.api = static_cast<uint32_t>(version);
		const auto entry = j["entry"].get<std::string>();
		if (out.name.empty() || out.name.size() > 128 || out.version.empty() || out.version.size() > 64 ||
			out.name.find_first_of("\r\n\t[]") != std::string::npos || out.name.find('\0') != std::string::npos || entry.empty() ||
			entry.size() > 255 || entry.find_first_of("/\\:") != std::string::npos || entry.find('\0') != std::string::npos)
			throw std::runtime_error("invalid manifest name/version/entry");
		const auto filename = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(entry.data()), entry.size()));
		if (filename.extension() != L".dll" || filename != filename.filename())
			throw std::runtime_error("entry must be a DLL filename within the plugin folder");
		const auto directory = std::filesystem::canonical(path.parent_path());
		out.entry = std::filesystem::canonical(directory / filename);
		if (out.entry.parent_path() != directory || !std::filesystem::is_regular_file(out.entry))
			throw std::runtime_error("plugin entry resolves outside its directory");
		return out;
	}
}
