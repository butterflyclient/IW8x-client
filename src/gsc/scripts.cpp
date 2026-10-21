#include "gsc/internal/bindings.hpp"
#include "gsc/internal/state.hpp"

#include <algorithm>
#include <set>
namespace iw8x::gsc::internal
{
	namespace
	{
		std::string normalize_script(iw8x_string input)
		{
			const auto raw = text(input);
			if (raw.empty() || raw.size() > 255 || raw.find('\0') != std::string_view::npos)
				throw std::invalid_argument("invalid script path");
			std::string result(raw);
			std::replace(result.begin(), result.end(), '\\', '/');
			for (auto& c : result)
				if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if (result.ends_with(".gsc")) result.resize(result.size() - 4);
			if (result.empty() || result.front() == '/' || result.back() == '/' || result.find("..") != std::string::npos ||
				result.find("//") != std::string::npos)
				throw std::invalid_argument("invalid script path");
			for (const auto c : result)
			{
				const auto ch = static_cast<unsigned char>(c);
				if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' ||
						ch == '-'))
					throw std::invalid_argument("invalid script path");
			}
			return result;
		}
	}

	iw8x_status register_script(
		iw8x_plugin_id id, iw8x_string script, uint32_t function_token, uint32_t result_type, iw8x_registration* output)
	{
		if (!output) return IW8X_INVALID_ARGUMENT;
		*output = 0;
		if (!function_token || result_type > IW8X_WEAPON_SNAPSHOT) return IW8X_INVALID_ARGUMENT;
		if (result_type == IW8X_WEAPON) return IW8X_UNSUPPORTED;
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		const auto normalized = normalize_script(script);
		auto& s = state();
		std::lock_guard lock(s.mutex);
		script_entry entry{ plugin, normalized, function_token, result_type };
		for (const auto& [registration, existing] : s.scripts)
		{
			if (existing.script != normalized || existing.function_token != function_token) continue;
			if (existing.plugin == plugin && existing.result_type == result_type)
			{
				*output = registration;
				return IW8X_OK;
			}
			// Exports are independent of the caller and requested return type.
			// Share both successful and missing-export results for this generation.
			if (existing.epoch == s.epoch)
			{
				entry.handle = existing.handle;
				entry.epoch = existing.epoch;
			}
		}
		if (s.scripts.size() >= 4096 || !s.next_registration) return IW8X_LIMIT;
		entry.label = normalized + "::" + std::string(script_function_name(function_token));
		const auto token = s.next_registration++;
		s.scripts.emplace(token, std::move(entry));
		*output = token;
		return IW8X_OK;
	}

	void invalidate_script_exports() noexcept
	{
		try
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			++s.script_generation;
			for (auto& [_, entry] : s.scripts)
			{
				entry.handle = 0;
				entry.epoch = 0;
			}
		}
		catch (...)
		{
		}
	}

	void resolve_script_exports(void* context) noexcept
	{
		try
		{
			const auto resolve = bindings().resolve_script;
			if (!resolve) return;
			std::set<std::pair<std::string, uint32_t>> requested;
			uint64_t epoch{}, generation{};
			{
				auto& s = state();
				std::lock_guard lock(s.mutex);
				if (!s.map_active) return;
				epoch = s.epoch;
				generation = s.script_generation;
				for (const auto& [id, entry] : s.scripts)
				{
					if (entry.epoch != epoch) requested.emplace(entry.script, entry.function_token);
				}
			}
			for (const auto& request : requested)
			{
				slow_scope timer("script.resolve", request.first, 0, request.second);
				{
					auto& s = state();
					std::lock_guard lock(s.mutex);
					if (!s.map_active || s.epoch != epoch || s.script_generation != generation) return;
				}
				// Engine lookup stays exclusively in Scr_EndLoadScripts, before its
				// compiler lookup objects are freed. Never resolve from a call.
				const auto handle = resolve(context, request.first.c_str(), request.second);
				auto& s = state();
				std::lock_guard lock(s.mutex);
				if (!s.map_active || s.epoch != epoch || s.script_generation != generation) return;
				for (auto& [id, entry] : s.scripts)
				{
					if (entry.script == request.first && entry.function_token == request.second)
					{
						entry.handle = handle;
						entry.epoch = epoch; // A zero handle is a cached NOT_FOUND.
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			report(nullptr, e.what());
		}
		catch (...)
		{
			report(nullptr, "stock script export resolution failed");
		}
	}

	iw8x_status registered_script_handle(
		iw8x_plugin_id id, iw8x_registration token, uint32_t& output, uint32_t& result_type, std::string_view* label) noexcept
	{
		output = 0;
		result_type = IW8X_UNDEFINED;
		try
		{
			const auto plugin = find_owner(id);
			if (!plugin) return IW8X_STOPPING;
			auto& s = state();
			std::lock_guard lock(s.mutex);
			const auto it = s.scripts.find(token);
			if (it == s.scripts.end() || it->second.plugin != plugin) return IW8X_NOT_FOUND;
			if (label) *label = it->second.label;
			if (!s.map_active || it->second.epoch != s.epoch) return IW8X_STALE;
			if (!it->second.handle) return IW8X_NOT_FOUND;
			output = it->second.handle;
			result_type = it->second.result_type;
			return IW8X_OK;
		}
		catch (...)
		{
			return IW8X_INTERNAL_ERROR;
		}
	}
}
