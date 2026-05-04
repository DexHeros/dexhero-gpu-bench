# DexHero GPU Benchmark

Tiny Vulkan-based GPU benchmark shipped with the DexHero Host installer. Runs once at registration time + opportunistically during sessions (Phase 5.2 spot-checks). Outputs a single integer score consumed by [server.js](../../server.js) `/api/node/register` and `/api/matchmaker/candidates` to detect self-reported `gpu_tier` mismatches (Sybil resistance: a malicious host can't claim "RTX 4090" with a GTX 1060).

## What it measures

A 30-second deterministic compute + raster workload designed to scale linearly with GPU compute throughput:

- **Compute pass** (10s): Vulkan compute shader running 4096-element FFT in a tight loop. Score = (FFTs/sec × constant).
- **Raster pass** (10s): triangle setup + textured quads, 4K-equivalent fillrate test. Score = (gigatexels/sec × constant).
- **Encoder probe** (10s): NVENC / VCE / VAAPI hardware encode of a 1080p H.264 stream. Score = (Mbps encoded × constant). Falls back to software-encode probe if no HW encoder available — flagged accordingly because a software-only host can't multi-seat AAA.

Total score = `floor(compute_score + raster_score + encoder_score)`. Reference values:

| GPU                         | Score   |
|---                          |---      |
| GTX 1650 / RX 580           | ~ 3,500 |
| RTX 3060 / RX 6600 XT       | ~ 7,500 |
| RTX 4070 / RX 7800 XT       | ~14,000 |
| RTX 4090 / RX 7900 XTX      | ~24,000 |

The exact constants are calibrated by running the binary on each reference card during release CI (see `gpu-bench/calibrate.yml`).

## Output

```json
{
    "version":      1,
    "score":        14283,
    "compute":      4321,
    "raster":       6109,
    "encoder":      3853,
    "gpu_name":     "NVIDIA GeForce RTX 4070",
    "gpu_vendor":   "nvidia",
    "vram_mb":      12288,
    "encoder_path": "nvenc-h264-hw",
    "duration_ms":  30142,
    "ran_at":       "2026-05-01T16:30:12Z",
    "machine_id":   "0x...",                  // SHA-256 of (cpuid, motherboard UUID, primary MAC). Stable per-host.
    "signature":    "0x..."                   // EIP-191 over the rest of the JSON, signed by the host's wallet.
}
```

The `machine_id` field is hashed (not raw identifiers) so a host's hardware fingerprint can be tracked across registrations without exposing PII. Used by [server.js](../../server.js) anti-fraud (Phase 5.2) to detect the same physical machine re-registering under multiple wallets.

## Build (CI)

```bash
cd tools/gpu-bench
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# Outputs build/dexhero-gpu-bench (or .exe on Windows)
```

Cross-compile for Windows + Linux + macOS in `.github/workflows/gpu-bench-build.yml` (TODO once `bench.cpp` lands).

## Running

The DexHero Host installer drops the binary at `<install>/dexhero-gpu-bench[.exe]`. The node-agent invokes it on registration:

```bash
$ ./dexhero-gpu-bench --json --sign-with /path/to/wallet.key
{ "score": 14283, "signature": "0x...", ... }
```

Server-side acceptance (in [server.js](../../server.js) `/api/node/register`): the report's `machine_id` and `signature` are recorded; the score is compared to `gpu_tier_thresholds` for the host's claimed tier; mismatch → `benchmark_flagged = true` + matchmaker filter excludes the host.

## Anti-cheat

A naive attacker could try to lie:
1. **Submit a bigger score than they ran**: prevented by EIP-191-signing the *entire* report (including the deterministic per-run `nonce` field). The server provides the nonce; the binary signs it; replay attempts fail because the nonce expires after 5 minutes.
2. **Re-submit a different host's signed report**: prevented by the wallet binding — the signature must be by the *registering* wallet, and the wallet+machine_id pair is consistent over time. Sudden machine_id flip on a wallet → flagged.
3. **Patch the binary to lie about its measurements**: prevented by reproducible-build SHA verification (Phase 1.3 `BUILD-MANIFEST.json`). The binary's hash in the host's installation must match the latest signed manifest. Hosts running a hand-patched binary fail `pre-flight`.
4. **Replace the binary at runtime**: harder; mitigated by the host service's PID-bound capture (only the agent's child process can submit benchmarks) plus reproducible-build verification at startup.

None of these alone is bulletproof. Combined with the legitimacy indexer (Phase 1.7) and pattern-analysis worker (Phase 5.2), they raise the cost of fraud past the gain.
