// SPDX-License-Identifier: GPL-3.0-only
//
// dexhero_bench.hpp — public surface for the dexhero-gpu-bench binary.
// Each phase (compute, raster, encoder probe) returns a sub-score; the
// aggregate score = compute + raster + encoder. machine_id + sign emit
// the canonical signed report via json::emit().
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace dexhero::bench {

  /// Result of one phase. score is in calibrated units; absolute values
  /// scale linearly with the host's measured throughput.
  struct PhaseResult {
    int           score        = 0;
    double        duration_ms  = 0;
    std::string   notes;          // optional diagnostic text
  };

  /// Aggregated report.
  struct Report {
    int           version              = 1;
    std::string   ran_at_iso8601;       // ISO 8601 UTC
    int           duration_ms          = 0;
    int           score                = 0;       // compute + raster + encoder
    PhaseResult   compute;
    PhaseResult   raster;
    PhaseResult   encoder;
    std::string   gpu_name;
    std::string   gpu_vendor;           // "nvidia" | "amd" | "intel" | "apple" | "other"
    int           vram_mb              = 0;
    std::string   encoder_path;         // "nvenc-h264-hw" | ... | "cpu-only"
    std::string   machine_id;           // 0x + 64 hex
    std::string   nonce;                // 0x + 64 hex (server-issued)
    std::string   signature;            // 0x + 130 hex (EIP-191)
    int           claimed_gpu_tier     = 0;
  };

  // ── Phase entry points (each defined in its own TU) ─────────────────

  PhaseResult run_compute(double seconds);
  PhaseResult run_raster(double seconds);
  PhaseResult run_encoder(double seconds);

  // ── Helpers ────────────────────────────────────────────────────────

  std::string machine_id_hex();
  std::string sign_report_eip191(const Report &r,
                                 const std::array<uint8_t, 32> &private_key);

  // Emit canonicalised JSON of a Report (sorted keys, no whitespace) — the
  // input to the EIP-191 hash. The actual emitted output is then this plus
  // the appended signature field.
  std::string canonicalize_for_signing(const Report &r);
  std::string emit_full_json(const Report &r);

  // Read a 32-byte private key from a binary file (the wallet key file
  // the agent passes via --sign-with).
  std::optional<std::array<uint8_t, 32>> load_private_key(const std::string &path);

} // namespace dexhero::bench
