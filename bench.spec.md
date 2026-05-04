# `dexhero-gpu-bench` C++ binary spec

This is the **specification** for the GPU benchmark binary. Implementation lives in `bench.cpp` (TBD) once the C++/Vulkan team picks it up. Spec-first so the JSON output contract is reviewable separately from line-noise.

## Public interface (CLI)

```
dexhero-gpu-bench [options]
```

| Flag | Purpose |
|---|---|
| `--json`                | Emit a single JSON object to stdout, exit code 0 on success |
| `--sign-with PATH`      | Read an EIP-191 wallet private key from PATH (binary 32 bytes) and sign the report. Without it, `signature` is empty. |
| `--nonce HEX`           | Server-supplied nonce baked into the signed report. Optional; if absent, `null`. |
| `--quick`               | 10s instead of 30s; used by the M4 spot-check pulse. Score still calibrated to comparable units. |
| `--vendor-hint=auto`    | `nvidia` / `amd` / `intel` / `apple` / `auto`. Selects the encoder path. Default `auto` (Vulkan-driver detection). |
| `--cpu-fallback`        | Run a CPU-only encode as fallback if no HW encoder. Score will fail any GPU tier > 0 but documents that the host has no GPU encoder. |
| `--version`             | Print version + build SHA, exit 0. |
| `--help`                | Print this help, exit 0. |

## JSON output contract

The signed payload (= input to the EIP-191 hash) is the JSON object **minus** the `signature` field, with keys sorted lexicographically and no whitespace. The `signature` is appended to the emitted JSON.

```json
{
    "version":      1,
    "ran_at":       "2026-05-01T16:30:12Z",
    "duration_ms":  30142,
    "score":        14283,
    "compute":      4321,
    "raster":       6109,
    "encoder":      3853,
    "gpu_name":     "NVIDIA GeForce RTX 4070",
    "gpu_vendor":   "nvidia",
    "vram_mb":      12288,
    "encoder_path": "nvenc-h264-hw",
    "machine_id":   "0xHEX64",
    "nonce":        "0xHEX64",
    "signature":    "0xHEX130"
}
```

### Field semantics

- **score**: `floor(compute + raster + encoder)`. The matchmaker compares this against `gpu_tier_thresholds.min_score` for the host's declared `gpu_tier`. Mismatch → flagged.
- **compute / raster / encoder**: per-pass subscores; logged for diagnostics. Only `score` is compared.
- **gpu_vendor**: enum `nvidia | amd | intel | apple | other`.
- **encoder_path**: one of `nvenc-h264-hw`, `nvenc-hevc-hw`, `nvenc-av1-hw`, `vce-h264-hw`, `vce-hevc-hw`, `vaapi-h264-hw`, `vaapi-hevc-hw`, `videotoolbox-hw`, `cpu-only`. The matchmaker can prefer hardware-encoder hosts for AAA streams.
- **machine_id**: `keccak256(cpuid_features || mb_uuid || mac_primary)` → 0x-prefixed hex. Stable per physical machine; mostly for fraud correlation.
- **nonce**: when supplied via `--nonce`, the binary echoes it back. Server uses this to prevent submission of stale benchmarks.
- **signature**: EIP-191 of the canonicalized signed payload above. Server recovers the address and verifies it matches the registering wallet.

## Exit codes

| Code | Meaning |
|---|---|
| 0    | Successful benchmark, JSON emitted on stdout |
| 1    | Vulkan initialisation failed (no compatible GPU) |
| 2    | Encoder enumeration failed (HW encode unavailable + `--cpu-fallback` not set) |
| 3    | Sign-with key file not readable / not 32 bytes |
| 4    | Benchmark detected throttling / inconsistent results — host should re-run |

## Determinism

Same hardware + same build = same score within ±1.5%. Variance comes from: thermal throttling (mitigated by 3-sec warm-up loop discarded from measurements), driver background work (timed work-items only, not wall-clock), and OS scheduling (pinned to a single CPU thread for the dispatch driver).

## Threading + isolation

The benchmark is single-thread on the CPU side and single-stream on the GPU side. It does NOT compete with a running M4 instance — when run via `node-agent`, `node-agent` first checks that no `seats_in_use > 0` before launching. The Phase 5.2 spot-check uses `--quick` (10s) and is gated on the same condition.

## Build inputs

- C++17 + Vulkan SDK 1.3+
- `glslangValidator` for shader compilation (`bench.comp`, `bench.vert`, `bench.frag`)
- `libsodium` for keccak256 + ed25519 sign (the same libsodium pinned in dexhero-host)
- macOS: MoltenVK for Vulkan-on-Metal

## Tests (added under `tests/`)

- `score_is_deterministic_within_tolerance()` — same machine, 10 consecutive runs, all scores within ±2%.
- `signed_payload_recovers_to_signing_wallet()` — verify with ethers-rs in a test driver.
- `bad_signature_rejected_server_side()` — flip a bit in the signature, expect 401 from /api/node/register.
- `nonce_replay_rejected()` — submit the same signed report twice, expect 409 on the second.
- `cpu_fallback_emits_cpu_only_path()` — when no HW encoder, encoder_path is `cpu-only` and score is suppressed below tier 1's min_score.
