// Generates EXP-CDC-001 cross-build golden vectors for rabin-v1 profile.
// Run under GCC and Clang; both must produce identical output.
//
// Build:  cmake --build <preset> --target exp_cdc_001_golden
// Run:    ./exp_cdc_001_golden
//
// The chosen rabin-v1 profile (Profile B, 16KB target):
//   polynomial    = 0x3da3358b4dc173
//   window_bytes  = 32
//   min_bytes     = 1024
//   target_bytes  = 16384
//   max_bytes     = 65536
//   boundary_mask = 0x00003FFF
//   boundary_value= 0
//
// Corpus: 524288 bytes (512 KB) of xorshift64 output, seed 0x6789abcdu^0x02.
// Matches the binary_data corpus from EXP-CDC-001 (measured: 32 chunks, avg 16 KB).
// Expected: deterministic chunk boundaries across all supported compilers.

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "spoink/offline/offline.h"

int main() {
  // Deterministic input: 512 KB from xorshift64 seed 0x6789abcdu^0x02
  // (matches binary_data corpus from EXP-CDC-001; produces ~32 chunks)
  std::uint64_t state = 0x6789abcdu ^ 0x02u;
  auto xnext = [&]() -> std::uint8_t {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return static_cast<std::uint8_t>(state & 0xFFU);
  };
  std::vector<std::byte> input(524288U);
  for (auto& b : input) b = static_cast<std::byte>(xnext());

  // rabin-v1 profile (Profile B: 16KB target)
  const spoink::offline::CdcProfile rabin_v1{
      0x3da3358b4dc173ULL,  // polynomial
      32U,                  // window_bytes
      1024U,                // min_bytes
      16384U,               // target_bytes
      65536U,               // max_bytes
      0x00003FFFU,          // boundary_mask
      0U,                   // boundary_value
  };

  const auto whole_digest = spoink::offline::sha256(input);
  const auto chunks = spoink::offline::chunk_bytes(input, rabin_v1);

  if (!whole_digest.has_value() || !chunks.has_value()) {
    std::cerr << "ERROR: chunking or digest failed\n";
    return 1;
  }

  std::cout << "# EXP-CDC-001 golden vectors — rabin-v1 (Profile B, 16KB target)\n"
            << "# corpus: 524288 bytes xorshift64 seed=0x6789abcdu^0x02 (binary_data from EXP-CDC-001)\n"
            << "# profile: poly=0x3da3358b4dc173 window=32 min=1024 "
               "target=16384 max=65536 mask=0x3fff value=0\n"
            << "whole_file_sha256: " << whole_digest.value_if()->hex() << '\n'
            << "chunk_count: " << chunks.value_if()->size() << '\n';

  for (const auto& c : *chunks.value_if()) {
    std::cout << "chunk offset=" << c.offset << " length=" << c.bytes.size() << " sha256=" << c.digest.hex() << '\n';
  }
  return 0;
}
