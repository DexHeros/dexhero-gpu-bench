// SPDX-License-Identifier: GPL-3.0-only
//
// json.cpp — minimal JSON emitter for the bench report. No external deps.
//
// canonicalize_for_signing returns the JSON with keys sorted lexicographically
// and no whitespace — the input to the EIP-191 hash (must match the
// server-side _verifyBenchmark in V3Labs's server.js).
//
// emit_full_json appends the signature field after canonicalization.
#include "dexhero_bench.hpp"

#include <map>
#include <sstream>
#include <string>

namespace dexhero::bench {

  namespace {
    std::string escape(std::string_view s) {
      std::string out; out.reserve(s.size() + 2);
      for (char c : s) {
        switch (c) {
          case '"':  out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\b': out += "\\b";  break;
          case '\f': out += "\\f";  break;
          case '\n': out += "\\n";  break;
          case '\r': out += "\\r";  break;
          case '\t': out += "\\t";  break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
              out += buf;
            } else {
              out += c;
            }
        }
      }
      return out;
    }

    // Build a sorted key/value map for the canonicalization. Skips the
    // signature field.
    std::map<std::string, std::string> report_to_sorted_kv(const Report &r, bool include_signature) {
      std::map<std::string, std::string> kv;
      auto i = [](int n) { return std::to_string(n); };
      kv["version"]            = i(r.version);
      kv["ran_at"]             = std::string{"\""} + escape(r.ran_at_iso8601) + "\"";
      kv["duration_ms"]        = i(r.duration_ms);
      kv["score"]              = i(r.score);
      kv["compute"]            = i(r.compute.score);
      kv["raster"]             = i(r.raster.score);
      kv["encoder"]            = i(r.encoder.score);
      kv["gpu_name"]           = std::string{"\""} + escape(r.gpu_name) + "\"";
      kv["gpu_vendor"]         = std::string{"\""} + escape(r.gpu_vendor) + "\"";
      kv["vram_mb"]            = i(r.vram_mb);
      kv["encoder_path"]       = std::string{"\""} + escape(r.encoder_path) + "\"";
      kv["machine_id"]         = std::string{"\""} + escape(r.machine_id) + "\"";
      kv["nonce"]              = std::string{"\""} + escape(r.nonce) + "\"";
      kv["claimed_gpu_tier"]   = i(r.claimed_gpu_tier);
      if (include_signature && !r.signature.empty()) {
        kv["signature"]        = std::string{"\""} + escape(r.signature) + "\"";
      }
      return kv;
    }

    std::string emit_kv(const std::map<std::string, std::string> &kv) {
      std::ostringstream o;
      o << "{";
      bool first = true;
      for (const auto &[k, v] : kv) {
        if (!first) o << ",";
        first = false;
        o << "\"" << escape(k) << "\":" << v;
      }
      o << "}";
      return o.str();
    }
  } // anonymous

  std::string canonicalize_for_signing(const Report &r) {
    return emit_kv(report_to_sorted_kv(r, /*include_signature=*/false));
  }

  std::string emit_full_json(const Report &r) {
    return emit_kv(report_to_sorted_kv(r, /*include_signature=*/true));
  }

} // namespace dexhero::bench
