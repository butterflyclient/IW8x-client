#pragma once

#include <algorithm>
#include <cstdint>

namespace iw8x::gsc::internal
{
    struct engine_frame_summary
    {
        std::uint64_t frames{}, slow_frames{}, wall_total_us{}, wall_max_us{}, cpu_total_us{}, cpu_max_us{};
        bool complete{};
        bool publish{};
    };

    class engine_frame_metrics
    {
    public:
        engine_frame_summary add(const std::uint64_t now_ms, const std::uint64_t wall_us,
                                 const std::uint64_t cpu_us, const std::uint64_t threshold_us = 40000,
                                 const bool publish_healthy_window = false) noexcept
        {
            if (!window_start_ms_) window_start_ms_ = now_ms;
            ++current_.frames;
            current_.wall_total_us += wall_us;
            current_.wall_max_us = (std::max)(current_.wall_max_us, wall_us);
            current_.cpu_total_us += cpu_us;
            current_.cpu_max_us = (std::max)(current_.cpu_max_us, cpu_us);
            if (wall_us >= threshold_us) ++current_.slow_frames;
            if (now_ms - window_start_ms_ < 1000) return {};
            engine_frame_summary result = current_;
            result.complete = true;
            // Normal production operation only publishes slow frames. A
            // strictly opt-in Island diagnostic heartbeat can also publish
            // *healthy* windows; without it, silence is NOT a stalled-frame
            // signal and must not be interpreted as such.
            result.publish = current_.slow_frames != 0 || publish_healthy_window;
            current_ = {};
            window_start_ms_ = now_ms;
            return result;
        }

    private:
        engine_frame_summary current_{};
        std::uint64_t window_start_ms_{};
    };
}
