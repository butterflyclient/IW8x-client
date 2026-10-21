#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace command
{

	class params final
	{
	public:
		explicit params(std::string_view input);

		[[nodiscard]] std::size_t size() const noexcept
		{
			return count_;
		}

		[[nodiscard]] std::string_view get(std::size_t index) const noexcept;
		[[nodiscard]] std::string join(std::size_t index = 0) const;

		[[nodiscard]] std::string_view full() const noexcept
		{
			return input_;
		}

		[[nodiscard]] std::string_view operator[](const std::size_t index) const noexcept
		{
			return get(index);
		}

	private:
		static constexpr std::size_t max_params = 32;
		std::string_view input_{};
		std::string_view values_[max_params]{};
		std::size_t count_{};
	};

	using callback = std::function<void(const params&)>;

	void add(std::string name, callback handler);
	// Atomic non-replacing registration. Token permits safe conditional cleanup.
	[[nodiscard]] std::uint64_t try_add(std::string name, callback handler);
	void remove(std::string_view name, std::uint64_t token);
	void remove(std::string_view name);
	[[nodiscard]] bool dispatch(std::string_view input);
	// Console producers queue copied text only. The verified game-frame boundary
	// drains a bounded snapshot; commands queued by callbacks wait for another pump.
	enum class enqueue_result
	{
		unknown,
		queued,
		rejected
	};
	[[nodiscard]] enqueue_result enqueue(std::string_view input);
	void pump();
	// Snapshot for console completion; callbacks remain owned by this registry.
	std::vector<std::string> names();

} // namespace command
