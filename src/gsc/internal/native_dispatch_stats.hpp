#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace iw8x::gsc::internal
{
    // Server simulation thread only. No atomics/locks/allocations per VM call.
    template <std::size_t Slots>
    class native_dispatch_stats
    {
    public:
        struct snapshot
        {
            uint64_t calls{};
            uint64_t missing{};
            uint64_t elapsed_us{};
            uint64_t peak_us{};
            uint64_t missing_top_calls{};
            uint32_t missing_top_slot{};
            bool missing_top_method{};
            uint64_t missing_second_calls{};
            uint32_t missing_second_slot{};
            bool missing_second_method{};
        };

        void record(const bool method, const uint32_t slot, const bool missing, const uint64_t elapsed_us) noexcept
        {
            ++calls_;
            elapsed_us_ += elapsed_us;
            peak_us_ = (std::max)(peak_us_, elapsed_us);
            if (missing)
            {
                ++missing_;
                const auto index = static_cast<std::size_t>(slot) + (method ? Slots : 0);
                if (index < missing_hits_.size()) ++missing_hits_[index];
            }
        }

        snapshot take() noexcept
        {
            snapshot result{ calls_, missing_, elapsed_us_, peak_us_ };
            for (std::size_t index = 0; index < missing_hits_.size(); ++index)
            {
                if (missing_hits_[index] > result.missing_top_calls)
                {
                    result.missing_second_calls = result.missing_top_calls;
                    result.missing_second_slot = result.missing_top_slot;
                    result.missing_second_method = result.missing_top_method;
                    result.missing_top_calls = missing_hits_[index];
                    result.missing_top_slot = static_cast<uint32_t>(index % Slots);
                    result.missing_top_method = index >= Slots;
                }
                else if (missing_hits_[index] > result.missing_second_calls)
                {
                    result.missing_second_calls = missing_hits_[index];
                    result.missing_second_slot = static_cast<uint32_t>(index % Slots);
                    result.missing_second_method = index >= Slots;
                }
            }
            calls_ = missing_ = elapsed_us_ = peak_us_ = 0;
            missing_hits_.fill(0);
            return result;
        }

    private:
        std::array<uint32_t, Slots * 2> missing_hits_{};
        uint64_t calls_{}, missing_{}, elapsed_us_{}, peak_us_{};
    };
}
