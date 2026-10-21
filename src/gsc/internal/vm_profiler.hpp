#pragma once

// Bounded, allocation-free GSC VM profiling for opt-in Island diagnostics.
// The map records function bytecode entry addresses (not VM object pointers)
// so a hot script can be resolved against the loaded ScriptFile pool later.
#include <array>
#include <cstddef>
#include <cstdint>

namespace iw8x::gsc::internal
{
    class vm_profiler
    {
    public:
        struct snapshot
        {
            std::uint64_t calls{}, vm_us{}, maximum_us{}, overflow{};
            std::uintptr_t top_program{};
            std::uint64_t top_program_calls{};
        };

        void record(const std::uintptr_t program, const std::uint64_t us) noexcept
        {
            ++calls_;
            vm_us_ += us;
            if (us > maximum_us_) maximum_us_ = us;
            // Fixed-capacity linear probing. Never allocate or block the VM.
            if (!program) return;
            auto index = ((program >> 4) ^ (program >> 17) ^ (program >> 29)) % slots_.size();
            for (std::size_t attempt = 0; attempt < slots_.size(); ++attempt)
            {
                auto& slot = slots_[index];
                if (!slot.program || slot.program == program)
                {
                    slot.program = program;
                    ++slot.calls;
                    return;
                }
                index = (index + 1) % slots_.size();
            }
            ++overflow_;
        }

        snapshot take() noexcept
        {
            snapshot result{ calls_, vm_us_, maximum_us_, overflow_ };
            for (auto& item : slots_)
            {
                if (item.calls > result.top_program_calls)
                {
                    result.top_program_calls = item.calls;
                    result.top_program = item.program;
                }
                item = {};
            }
            calls_ = vm_us_ = maximum_us_ = overflow_ = 0;
            return result;
        }

    private:
        struct slot { std::uintptr_t program{}; std::uint32_t calls{}; };
        std::array<slot, 2048> slots_{};
        std::uint64_t calls_{}, vm_us_{}, maximum_us_{}, overflow_{};
    };
}
