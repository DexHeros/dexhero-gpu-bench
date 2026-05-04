// SPDX-License-Identifier: GPL-3.0-only
//
// bench.cpp — entry point for dexhero-gpu-bench.
//
// Usage:
//   dexhero-gpu-bench --json [--sign-with PATH] [--nonce HEX]
//                     [--quick] [--vendor-hint=auto|nvidia|amd|intel|apple]
//                     [--cpu-fallback] [--version] [--help]
//
// See tools/gpu-bench/bench.spec.md for the full CLI + JSON contract.
#include "dexhero_bench.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace dexhero::bench {

  static std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return std::string{buf};
  }

  static std::string get_arg(int argc, char **argv, std::string_view flag,
                              std::string_view default_v = "") {
    for (int i = 1; i < argc; ++i) {
      std::string_view a{argv[i]};
      if (a == flag && i + 1 < argc) return std::string{argv[i + 1]};
      if (a.size() > flag.size() + 1 && a.substr(0, flag.size()) == flag && a[flag.size()] == '=') {
        return std::string{a.substr(flag.size() + 1)};
      }
    }
    return std::string{default_v};
  }
  static bool has_flag(int argc, char **argv, std::string_view flag) {
    for (int i = 1; i < argc; ++i) if (std::string_view{argv[i]} == flag) return true;
    return false;
  }

  int run(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
      std::fputs(
        "Usage: dexhero-gpu-bench --json [--sign-with PATH] [--nonce HEX]\n"
        "                                [--quick] [--vendor-hint=auto|nvidia|amd|intel|apple]\n"
        "                                [--cpu-fallback] [--version] [--help]\n", stdout);
      return 0;
    }
    if (has_flag(argc, argv, "--version")) {
      std::fputs("dexhero-gpu-bench 1.0.0\n", stdout);
      return 0;
    }
    bool quick        = has_flag(argc, argv, "--quick");
    bool cpu_fallback = has_flag(argc, argv, "--cpu-fallback");
    std::string nonce = get_arg(argc, argv, "--nonce");
    std::string sign_with = get_arg(argc, argv, "--sign-with");

    auto t0 = std::chrono::steady_clock::now();

    Report r;
    r.version           = 1;
    r.ran_at_iso8601    = iso8601_now();
    r.nonce             = nonce;
    r.machine_id        = machine_id_hex();
    r.compute           = run_compute(quick ? 3.0 : 10.0);
    r.raster            = run_raster(quick ? 3.0 : 10.0);
    r.encoder           = run_encoder(quick ? 3.0 : 10.0);
    r.score             = r.compute.score + r.raster.score + r.encoder.score;
    r.duration_ms       = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0).count());

    // GPU hardware info populated by the encoder phase (it has to enumerate
    // adapters anyway).
    if (r.gpu_name.empty()) r.gpu_name = r.encoder.notes.empty() ? "unknown" : r.encoder.notes;

    // No HW encoder + no fallback flag → exit code 2 per the spec.
    if (r.encoder.score == 0 && r.encoder_path != "cpu-only" && !cpu_fallback) {
      std::fputs("dexhero-gpu-bench: no hardware encoder available; pass --cpu-fallback to continue\n", stderr);
      return 2;
    }

    if (!sign_with.empty()) {
      auto pk = load_private_key(sign_with);
      if (!pk) {
        std::fputs("dexhero-gpu-bench: --sign-with file unreadable or wrong size\n", stderr);
        return 3;
      }
      r.signature = sign_report_eip191(r, *pk);
    }

    std::fputs(emit_full_json(r).c_str(), stdout);
    std::fputc('\n', stdout);
    return 0;
  }

} // namespace dexhero::bench

int main(int argc, char **argv) {
  return dexhero::bench::run(argc, argv);
}
