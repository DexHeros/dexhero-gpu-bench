// SPDX-License-Identifier: GPL-3.0-only
//
// sign.cpp — EIP-191 signing for the bench report.
//
// EIP-191 personal-message format:
//   keccak256("\x19Ethereum Signed Message:\n" + len(msg) + msg)
//   ECDSA secp256k1 sign with the recovery id (v).
//
// We hash the canonicalized JSON via keccak256, then sign with libsecp256k1.
// The signature is encoded as 65 bytes: r(32) || s(32) || v(1, where 27/28).
//
// load_private_key reads a 32-byte binary file (the wallet's secp256k1
// private key). Production: protect the key file with appropriate ACLs.
#include "dexhero_bench.hpp"

#include <fstream>
#include <vector>

#include <openssl/evp.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

// keccak256 isn't in OpenSSL 1.x but is in OpenSSL 3+ as "KECCAK-256".
// We fall back to the SHA3-256 EVP backed by Keccak when available.

namespace dexhero::bench {

  namespace {
    bool keccak256(const uint8_t *data, size_t len, uint8_t out[32]) {
      // OpenSSL 3.x exposes "KECCAK-256" (not the same as SHA3-256 due to
      // padding differences — we want the original Keccak which is what
      // EIP-191 uses).
      EVP_MD *md = EVP_MD_fetch(nullptr, "KECCAK-256", nullptr);
      if (!md) return false;
      EVP_MD_CTX *ctx = EVP_MD_CTX_new();
      if (!ctx) { EVP_MD_free(md); return false; }
      bool ok = EVP_DigestInit_ex(ctx, md, nullptr) == 1
             && EVP_DigestUpdate(ctx, data, len)    == 1
             && EVP_DigestFinal_ex(ctx, out, nullptr) == 1;
      EVP_MD_CTX_free(ctx);
      EVP_MD_free(md);
      return ok;
    }
  } // anonymous

  std::optional<std::array<uint8_t, 32>> load_private_key(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::array<uint8_t, 32> out{};
    f.read(reinterpret_cast<char *>(out.data()), out.size());
    if (f.gcount() != 32) return std::nullopt;
    return out;
  }

  std::string sign_report_eip191(const Report &r,
                                 const std::array<uint8_t, 32> &private_key) {
    std::string canonical = canonicalize_for_signing(r);

    std::string prefix = std::string{"\x19""Ethereum Signed Message:\n"} + std::to_string(canonical.size());
    std::vector<uint8_t> msg;
    msg.reserve(prefix.size() + canonical.size());
    msg.insert(msg.end(), prefix.begin(), prefix.end());
    msg.insert(msg.end(), canonical.begin(), canonical.end());

    uint8_t hash[32];
    if (!keccak256(msg.data(), msg.size(), hash)) return {};

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) return {};
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash, private_key.data(), nullptr, nullptr)) {
      secp256k1_context_destroy(ctx);
      return {};
    }
    uint8_t out64[64]; int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, out64, &recid, &sig);
    secp256k1_context_destroy(ctx);

    static const char hex[] = "0123456789abcdef";
    std::string sig_hex{"0x"};
    sig_hex.reserve(2 + 130);
    for (uint8_t b : out64) {
      sig_hex += hex[(b >> 4) & 0xf];
      sig_hex += hex[b & 0xf];
    }
    uint8_t v = 27 + static_cast<uint8_t>(recid);
    sig_hex += hex[(v >> 4) & 0xf];
    sig_hex += hex[v & 0xf];
    return sig_hex;
  }

} // namespace dexhero::bench
