#include "command.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <mutex>
#include <unordered_map>
namespace command
{
	namespace
	{

		std::mutex g_mutex;

		struct entry
		{
			callback handler;
			std::uint64_t token;
		};

		std::unordered_map<std::string, entry> g_handlers;
		std::uint64_t g_next_token = 1;

		struct pending_command
		{
			std::string text;
			std::uint64_t token;
		};

		std::deque<pending_command> g_pending;

		std::string normalized(std::string_view value)
		{
			std::string result(value);
			std::transform(result.begin(), result.end(), result.begin(),
				[](const unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return result;
		}

		bool whitespace(const char c) noexcept
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n';
		}

	} // namespace

	params::params(const std::string_view input) : input_(input)
	{
		std::size_t cursor = 0;
		while (cursor < input.size() && count_ < max_params)
		{
			while (cursor < input.size() && whitespace(input[cursor]))
				++cursor;
			if (cursor >= input.size()) break;
			const auto start = cursor;
			while (cursor < input.size() && !whitespace(input[cursor]))
				++cursor;
			values_[count_++] = input.substr(start, cursor - start);
		}
	}

	std::string_view params::get(const std::size_t index) const noexcept
	{
		return index < count_ ? values_[index] : std::string_view{};
	}

	std::string params::join(const std::size_t index) const
	{
		if (index >= count_) return {};
		std::string result;
		for (std::size_t i = index; i < count_; ++i)
		{
			if (!result.empty()) result.push_back(' ');
			result.append(values_[i]);
		}
		return result;
	}

	void add(std::string name, callback handler)
	{
		if (name.empty() || !handler) return;
		std::lock_guard lock(g_mutex);
		if (!g_next_token) return;
		g_handlers[normalized(name)] = { std::move(handler), g_next_token++ };
	}

	std::uint64_t try_add(std::string name, callback handler)
	{
		if (name.empty() || !handler) return 0;
		std::lock_guard lock(g_mutex);
		if (!g_next_token) return 0;
		const auto token = g_next_token++;
		return g_handlers.emplace(normalized(name), entry{ std::move(handler), token }).second ? token : 0;
	}

	void remove(const std::string_view name, const std::uint64_t token)
	{
		std::lock_guard lock(g_mutex);
		const auto it = g_handlers.find(normalized(name));
		if (it != g_handlers.end() && it->second.token == token) g_handlers.erase(it);
	}

	void remove(const std::string_view name)
	{
		std::lock_guard lock(g_mutex);
		g_handlers.erase(normalized(name));
	}

	std::vector<std::string> names()
	{
		std::lock_guard lock(g_mutex);
		std::vector<std::string> result;
		result.reserve(g_handlers.size());
		for (const auto& item : g_handlers) result.push_back(item.first);
		return result;
	}

	bool dispatch(const std::string_view input)
	{
		const params arguments(input);
		if (!arguments.size()) return false;

		callback handler;
		{
			std::lock_guard lock(g_mutex);
			const auto it = g_handlers.find(normalized(arguments[0]));
			if (it == g_handlers.end()) return false;
			handler = it->second.handler;
		}

		handler(arguments);
		return true;
	}

	enqueue_result enqueue(const std::string_view input)
	{
		const params arguments(input);
		if (!arguments.size()) return enqueue_result::unknown;
		std::lock_guard lock(g_mutex);
		const auto it = g_handlers.find(normalized(arguments[0]));
		if (it == g_handlers.end()) return enqueue_result::unknown;
		// Consume known commands even when saturated; never forward a rejected
		// plugin command to an unrelated stock engine command with the same name.
		if (input.size() > 1023 || g_pending.size() >= 256) return enqueue_result::rejected;
		g_pending.push_back({ std::string(input), it->second.token });
		return enqueue_result::queued;
	}

	void pump()
	{
		std::deque<pending_command> batch;
		{
			std::lock_guard lock(g_mutex);
			for (std::size_t i = 0; i < 32 && !g_pending.empty(); ++i)
			{
				batch.push_back(std::move(g_pending.front()));
				g_pending.pop_front();
			}
		}
		for (const auto& item : batch)
		{
			const params arguments(item.text);
			callback handler;
			{
				std::lock_guard lock(g_mutex);
				const auto it = g_handlers.find(normalized(arguments[0]));
				// Unload/re-registration must not deliver old input to a new DLL.
				if (it == g_handlers.end() || it->second.token != item.token) continue;
				handler = it->second.handler;
			}
			handler(arguments);
		}
	}

} // namespace command
