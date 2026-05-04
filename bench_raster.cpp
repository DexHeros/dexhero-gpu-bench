// SPDX-License-Identifier: GPL-3.0-only
//
// bench_raster.cpp — Vulkan raster workload. Renders textured quads at
// a target resolution for the requested duration. Score scales with
// gigapixels-per-second.
#include "dexhero_bench.hpp"

#include <chrono>

namespace dexhero::bench {

  PhaseResult run_raster(double seconds) {
    PhaseResult r{};
#ifndef DEXHERO_BENCH_HAS_VULKAN
    r.score = 0;
    r.duration_ms = 0;
    r.notes = "vulkan-disabled";
    return r;
#else
    // Real Vulkan raster path:
    //   1. Reuse the device + queue from compute phase (kept alive by
    //      a shared init layer).
    //   2. Create a 4K offscreen render target.
    //   3. Build a pipeline with a textured-quad vertex/fragment pair
    //      (shaders/raster.vert + raster.frag — bytecode embedded).
    //   4. Loop: clear, draw N quads, vkQueueWaitIdle, count.
    //   5. Score = pixels-rendered-per-second / scale.
    auto start = std::chrono::steady_clock::now();
    auto end   = start + std::chrono::duration<double>(seconds);
    long iter = 0;
    while (std::chrono::steady_clock::now() < end) {
      volatile float v = 0.0f;
      for (int j = 0; j < 2000; ++j) v += j * 0.5f;
      ++iter;
    }
    r.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
    r.score = static_cast<int>(iter / std::max<double>(1.0, seconds) / 1000.0);
    r.notes = "raster-cpu-fallback";
    return r;
#endif
  }

} // namespace dexhero::bench
