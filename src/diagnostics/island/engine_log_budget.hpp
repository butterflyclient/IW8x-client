#pragma once

// Limit the I/O cost of one known, repeated renderer diagnostic. This does
// NOT fix a missing RenderTargetFormat variant: a representative warning
// and an exact suppressed count remain visible once per interval.
#include <atomic>
#include <cstdint>
#include <cstring>

namespace joindiag::detail
{
    inline bool island_pipeline_format_warning(const unsigned channel, const int flags, const char* text) noexcept
    {
        if (channel != 8 || flags != 2 || !text) return false;
        // Preserve all other engine warnings, even on the Island map.
        constexpr char message[] =
            "R_SetPipelineState: could not find matching RenderTargetFormat index in MaterialTechnique";
        if (text[0] == '^' && text[1] == '3') text += 2;
        return std::strncmp(text, message, sizeof(message) - 1) == 0;
    }

    struct warning_decision
    {
        bool forward{};
        std::uint64_t suppressed_since_last_forward{};
    };

    class engine_log_budget
    {
    public:
        // Thread-safe for warnings produced by concurrent game/render
        // workers. No mutex or allocation on the hot duplicate path.
        warning_decision accept(const std::uint64_t now_ms, const std::uint64_t interval_ms = 1000) noexcept
        {
            auto next = next_forward_ms_.load(std::memory_order_relaxed);
            for (;;)
            {
                if (now_ms < next)
                {
                    hidden_.fetch_add(1, std::memory_order_relaxed);
                    return { false, 0 };
                }
                const auto deadline = now_ms + interval_ms;
                if (next_forward_ms_.compare_exchange_weak(next, deadline, std::memory_order_acq_rel,
                                                            std::memory_order_relaxed))
                    return { true, hidden_.exchange(0, std::memory_order_acq_rel) };
            }
        }

    private:
        std::atomic<std::uint64_t> next_forward_ms_{ 0 };
        std::atomic<std::uint64_t> hidden_{ 0 };
    };
}
