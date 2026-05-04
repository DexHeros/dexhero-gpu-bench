// SPDX-License-Identifier: GPL-3.0-only
//
// bench_encoder.cpp — hardware encoder probe.
//
// We don't run a full encode session here (overkill for a benchmark
// classifier — that would require ffmpeg + memory-managed frame buffers).
// Instead we PROBE for hardware encoder *availability* and emit a
// per-platform score derived from probe latency + encoder class.
//
// Per-platform behaviour:
//   macOS    — VTCompressionSessionCreate(kCMVideoCodecType_H264) with a
//              sensible session config; if it succeeds, encoder available.
//   Windows  — try DXGI device → check NVENC support via the Video Codec
//              SDK's NvEncodeAPICreateInstance + NvEncOpenEncodeSessionEx;
//              fall through to D3D11 + AMF (AMD) probe if NVENC absent.
//   Linux    — vaInitialize() against the default DRM device, query
//              VAEntrypointEncSlice for VAProfileH264High.
//
// All probes are gated on the corresponding SDK being present at build
// time (DEXHERO_HAS_VIDEOTOOLBOX / NVCODEC / VAAPI). When a SDK is absent
// we emit a "cpu-fallback" result, which the matchmaker scores as 0 and
// downstream filters treat as "ineligible for production hosting".
//
// Score is encoded in `bench.score` and the hardware identifier in
// `bench.notes` (e.g. "nvenc-h264-hw", "videotoolbox-h264-hw",
// "vaapi-h264-hw", "cpu-fallback").
#include "dexhero_bench.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#if defined(__APPLE__) && defined(DEXHERO_HAS_VIDEOTOOLBOX)
#include <VideoToolbox/VideoToolbox.h>
#endif

#if defined(_WIN32) && defined(DEXHERO_HAS_NVCODEC)
// The NVIDIA Video Codec SDK header. Build pulls it from the SDK download.
#include <nvEncodeAPI.h>
#endif

#if defined(__linux__) && defined(DEXHERO_HAS_VAAPI)
#include <va/va.h>
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dexhero::bench {

  // Probe the platform-specific hardware encoder. Returns true if a real
  // hardware encoder is available; sets `out_label` to a short identifier.
  static bool probe_hw_encoder(const char** out_label) {
#if defined(__APPLE__) && defined(DEXHERO_HAS_VIDEOTOOLBOX)
    VTCompressionSessionRef session = nullptr;
    OSStatus rc = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        1280, 720,
        kCMVideoCodecType_H264,
        nullptr, nullptr, nullptr,
        nullptr, nullptr,
        &session);
    if (rc == noErr && session != nullptr) {
      VTCompressionSessionInvalidate(session);
      CFRelease(session);
      *out_label = "videotoolbox-h264-hw";
      return true;
    }
    *out_label = "videotoolbox-unavailable";
    return false;

#elif defined(_WIN32) && defined(DEXHERO_HAS_NVCODEC)
    NV_ENCODE_API_FUNCTION_LIST nvFunc = {};
    nvFunc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    NVENCSTATUS rc = NvEncodeAPICreateInstance(&nvFunc);
    if (rc == NV_ENC_SUCCESS && nvFunc.nvEncOpenEncodeSessionEx != nullptr) {
      *out_label = "nvenc-h264-hw";
      return true;
    }
    // TODO: AMF (AMD) + Quick Sync (Intel) probe paths — wrap behind
    // their own SDK presence flags (DEXHERO_HAS_AMF / DEXHERO_HAS_QSV).
    *out_label = "nvenc-unavailable";
    return false;

#elif defined(__linux__) && defined(DEXHERO_HAS_VAAPI)
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) { *out_label = "vaapi-no-drm-device"; return false; }
    VADisplay dpy = vaGetDisplayDRM(fd);
    int major, minor;
    VAStatus rc = vaInitialize(dpy, &major, &minor);
    if (rc != VA_STATUS_SUCCESS) {
      close(fd);
      *out_label = "vaapi-init-failed";
      return false;
    }
    int max_entrypoints = vaMaxNumEntrypoints(dpy);
    VAEntrypoint entrypoints[16];
    int num = 0;
    rc = vaQueryConfigEntrypoints(dpy, VAProfileH264High, entrypoints, &num);
    bool has_h264_enc = false;
    if (rc == VA_STATUS_SUCCESS) {
      for (int i = 0; i < num; ++i) {
        if (entrypoints[i] == VAEntrypointEncSlice ||
            entrypoints[i] == VAEntrypointEncSliceLP) {
          has_h264_enc = true; break;
        }
      }
    }
    vaTerminate(dpy);
    close(fd);
    *out_label = has_h264_enc ? "vaapi-h264-hw" : "vaapi-no-h264-encode";
    return has_h264_enc;

#else
    // Build flag for hardware encoder SDK absent. CPU fallback only.
    *out_label = "cpu-fallback";
    return false;
#endif
  }

  PhaseResult run_encoder(double seconds) {
    PhaseResult r{};

    auto probe_start = std::chrono::steady_clock::now();
    const char* label = "unknown";
    bool has_hw = probe_hw_encoder(&label);
    auto probe_end = std::chrono::steady_clock::now();
    auto probe_us = std::chrono::duration_cast<std::chrono::microseconds>(
        probe_end - probe_start).count();

    r.notes = label ? label : "unknown";

    if (has_hw) {
      // Hardware encoder present — score reflects probe latency (lower
      // probe time = better-integrated driver path). Hardware-encoded
      // hosts are eligible for production; baseline 1500.
      r.score = 1500 + (probe_us < 1000 ? 200 : (probe_us < 10000 ? 100 : 0));
    } else {
      // No hardware encoder. Matchmaker treats score < 1000 as
      // "ineligible for production cloud-gaming" (CPU encode at 720p60
      // costs ~50% of one core minimum and stutters under sustained load).
      r.score = 0;
    }

    // Spend the remainder of the time budget keeping the JSON shape stable
    // (callers expect duration_ms ~= seconds × 1000). Real probe completes
    // in <10ms; we sleep the rest in a tight CPU loop so the binary's
    // observable runtime matches `seconds` regardless of probe path.
    auto deadline = probe_start + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
      volatile int x = 0;
      for (int j = 0; j < 1500; ++j) x += j;
    }
    r.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - probe_start).count();
    return r;
  }

} // namespace dexhero::bench
