#include "../../src/diagnostics/island/engine_log_budget.hpp"
#include "../../src/gsc/internal/frame_metrics.hpp"
#include "../../src/gsc/internal/native_dispatch_stats.hpp"
#include "../../src/gsc/internal/vm_profiler.hpp"
#include "../../src/gsc/compiled/island_quest_native_abi.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    using joindiag::detail::engine_log_budget;
    using joindiag::detail::island_pipeline_format_warning;
    const char* exact = "^3R_SetPipelineState: could not find matching RenderTargetFormat index in MaterialTechnique matching with currently bound render targets! Forcing index to 0.";
    assert(island_pipeline_format_warning(8, 2, exact));
    assert(!island_pipeline_format_warning(8, 3, exact)); // Error is never hidden
    assert(!island_pipeline_format_warning(15, 2, exact));
    assert(!island_pipeline_format_warning(8, 2, "R_SetPipelineState: failed to create graphics pipeline state"));
    assert(!island_pipeline_format_warning(8, 2, nullptr));

    engine_log_budget budget;
    assert(budget.accept(1000).forward);
    for (int i = 0; i < 400; ++i)
        assert(!budget.accept(1100).forward);
    const auto next = budget.accept(2100);
    assert(next.forward && next.suppressed_since_last_forward == 400);
    assert(!budget.accept(2100).forward);

    engine_log_budget parallel;
    std::atomic<int> forwarded{};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i)
        workers.emplace_back([&] { for (int j = 0; j < 1000; ++j)
            if (parallel.accept(3000).forward) forwarded.fetch_add(1); });
    for (auto& t : workers) t.join();
    assert(forwarded.load() == 1);
    const auto total = parallel.accept(4200);
    assert(total.forward && total.suppressed_since_last_forward == 7999);

    iw8x::gsc::internal::engine_frame_metrics metrics;
    assert(!metrics.add(1000, 95000, 2500).publish);
    assert(!metrics.add(1100, 97000, 3100).publish);
    const auto frame = metrics.add(2100, 110000, 3000);
    assert(frame.complete && frame.publish && frame.frames == 3 && frame.slow_frames == 3);
    assert(frame.wall_max_us == 110000 && frame.cpu_max_us == 3100);
    assert(frame.wall_total_us / frame.frames == 100666);
    assert(!metrics.add(2200, 8000, 4000).publish);
    const auto quiet = metrics.add(3300, 9000, 5000);
    assert(quiet.complete && !quiet.publish && quiet.slow_frames == 0 && quiet.frames == 2);
    // False alarm regression: healthy windows were previously silent. The
    // opt-in diagnostic path must produce them without classifying as slow.
    iw8x::gsc::internal::engine_frame_metrics heartbeat;
    assert(!heartbeat.add(5000, 7000, 3000, 40000, true).publish);
    const auto healthy = heartbeat.add(6100, 6000, 2000, 40000, true);
    assert(healthy.complete && healthy.publish && healthy.frames == 2 && healthy.slow_frames == 0);
    assert(healthy.wall_total_us == 13000 && healthy.cpu_total_us == 5000);
    assert(!heartbeat.add(6200, 20000, 6000, 40000, true).publish);
    const auto next_healthy = heartbeat.add(7300, 9000, 2500, 40000, true);
    assert(next_healthy.complete && next_healthy.publish && next_healthy.slow_frames == 0);

    iw8x::gsc::internal::native_dispatch_stats<128> native;
    native.record(false, 13, true, 19);
    native.record(false, 13, true, 13);
    native.record(true, 61, true, 20);
    native.record(true, 37, false, 43);
    const auto dispatched = native.take();
    assert(dispatched.calls == 4 && dispatched.missing == 3);
    assert(dispatched.elapsed_us == 95 && dispatched.peak_us == 43);
    assert(dispatched.missing_top_slot == 13 && !dispatched.missing_top_method && dispatched.missing_top_calls == 2);
    assert(native.take().calls == 0);
    iw8x::gsc::internal::vm_profiler vm;
    vm.record(0x1000, 14);
    vm.record(0x2000, 16);
    vm.record(0x1000, 33);
    const auto vms = vm.take();
    assert(vms.calls == 3 && vms.vm_us == 63 && vms.maximum_us == 33);
    assert(vms.top_program == 0x1000 && vms.top_program_calls == 2);
    assert(vm.take().calls == 0);

    // Never accept a merely similar script with the same seven numeric
    // values; the whole original Island compiler bytecode is fingerprinted.
    std::vector<std::uint8_t> impostor(gsc::island_quest_native_abi::source_length, 0);
    for (const auto& site : gsc::island_quest_native_abi::source_calls)
    {
        impostor[site.opcode_offset] = site.opcode;
        impostor[site.opcode_offset + 1] = static_cast<std::uint8_t>(site.source_id & 0xFF);
        impostor[site.opcode_offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
    }
    const auto saved_impostor = impostor;
    assert(!gsc::island_quest_native_abi::translate_private_compiler_copy(impostor));
    assert(impostor == saved_impostor);
    assert(!gsc::island_quest_native_abi::translate_private_compiler_copy(
        std::span<std::uint8_t>(impostor.data(), impostor.size() - 1)));
    assert(impostor == saved_impostor);

    std::cout << "PASS: targeted warning filter, 8-thread duplicate suppression, "
                 "separate engine wall/CPU reporting, quiet fast frames and opt-in healthy heartbeat\n";
}
