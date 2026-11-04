// EXP-CDC-001: Rabin CDC profile sweep for rabin-v1 selection
//
// Sweeps four candidate profiles on a deterministic corpus (PRNG seed 0x6789abcdu).
// Reports chunk distribution, deduplication, reuse under five change patterns,
// and wall-time cost. Raw output is CSV; select rabin-v1 from the evidence.
//
// Build: cmake --build <preset> --target exp_cdc_001
// Run:   ./exp_cdc_001 > raw_results.csv 2>summary.txt
//
// Corpus files (all PRNG-generated, seed 0x6789abcdu):
//   text_source  128 KB  — low-entropy repeated text patterns
//   binary_data  512 KB  — high-entropy pseudo-random bytes
//   config_a      16 KB  — structured config-like data
//   config_b       8 KB  — structured config-like data
//
// Change patterns applied to each file:
//   unchanged, append_5pct, insert_front_5pct, delete_mid_5pct, replace_region_5pct
//
// Candidate profiles (polynomial 0x3da3358b4dc173, boundary_value 0):
//   A (small):  window=16, min=256,  target=4096,   max=16384,  mask=0x00000FFF
//   B (medium): window=32, min=1024, target=16384,  max=65536,  mask=0x00003FFF
//   C (large):  window=48, min=4096, target=65536,  max=262144, mask=0x0000FFFF
//   D (xlarge): window=48, min=8192, target=131072, max=524288, mask=0x0001FFFF

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "spoink/offline/offline.h"

namespace {

// ── Deterministic PRNG (xorshift64) ──────────────────────────────────────────

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed ? seed : 1U) {}
  std::uint64_t next() {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  }
  std::uint8_t byte() { return static_cast<std::uint8_t>(next() & 0xFFU); }
};

// ── Corpus generators ─────────────────────────────────────────────────────────

// Low-entropy: printable ASCII with repeating 64-byte patterns
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<std::byte> gen_text(std::size_t nbytes, std::uint64_t seed) {
  Rng rng(seed);
  std::vector<std::byte> buf(nbytes);
  // Generate a 64-byte base pattern, then repeat with small noise every 512 bytes
  std::uint8_t pat[64];
  for (auto& b : pat) b = static_cast<std::uint8_t>(0x20U + rng.byte() % 95U);
  for (std::size_t i = 0U; i < nbytes; ++i) {
    // Every 512 bytes, mutate 4 bytes in the pattern
    if (i % 512U == 0U && i > 0U) {
      for (int j = 0; j < 4; ++j) pat[rng.next() % 64U] = static_cast<std::uint8_t>(0x20U + rng.byte() % 95U);
    }
    buf[i] = static_cast<std::byte>(pat[i % 64U]);
  }
  return buf;
}

// High-entropy: pseudo-random bytes
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<std::byte> gen_binary(std::size_t nbytes, std::uint64_t seed) {
  Rng rng(seed);
  std::vector<std::byte> buf(nbytes);
  for (auto& b : buf) b = static_cast<std::byte>(rng.byte());
  return buf;
}

// Config-like: structured key=value pairs with integer values
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<std::byte> gen_config(std::size_t nbytes, std::uint64_t seed) {
  Rng rng(seed);
  std::string s;
  s.reserve(nbytes + 128U);
  const char* keys[] = {"timeout_ms", "max_connections", "buffer_size",    "retry_count",
                        "log_level",  "cache_ttl_s",     "worker_threads", "queue_depth"};
  while (s.size() < nbytes) {
    std::string line = keys[rng.next() % 8U];
    line += " = ";
    line += std::to_string(rng.next() % 65536U);
    line += '\n';
    s += line;
  }
  s.resize(nbytes);
  std::vector<std::byte> buf(nbytes);
  for (std::size_t i = 0U; i < nbytes; ++i) buf[i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
  return buf;
}

// ── Change patterns ───────────────────────────────────────────────────────────

std::vector<std::byte> apply_append(const std::vector<std::byte>& src, Rng& rng) {
  const std::size_t extra = src.size() / 20U;  // 5%
  auto out = src;
  out.reserve(src.size() + extra);
  for (std::size_t i = 0U; i < extra; ++i) out.push_back(static_cast<std::byte>(rng.byte()));
  return out;
}

std::vector<std::byte> apply_insert_front(const std::vector<std::byte>& src, Rng& rng) {
  const std::size_t extra = src.size() / 20U;
  std::vector<std::byte> ins(extra);
  for (auto& b : ins) b = static_cast<std::byte>(rng.byte());
  std::vector<std::byte> out;
  out.reserve(src.size() + extra);
  // Insert at offset 256 (near front)
  const std::size_t split = std::min(std::size_t{256U}, src.size());
  out.insert(out.end(), src.begin(), src.begin() + static_cast<std::ptrdiff_t>(split));
  out.insert(out.end(), ins.begin(), ins.end());
  out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(split), src.end());
  return out;
}

std::vector<std::byte> apply_delete_mid(const std::vector<std::byte>& src) {
  const std::size_t del = src.size() / 20U;
  const std::size_t start = src.size() / 2U;
  std::vector<std::byte> out;
  out.reserve(src.size() - del);
  out.insert(out.end(), src.begin(), src.begin() + static_cast<std::ptrdiff_t>(start));
  const std::size_t end = std::min(start + del, src.size());
  out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(end), src.end());
  return out;
}

std::vector<std::byte> apply_replace_region(const std::vector<std::byte>& src, Rng& rng) {
  auto out = src;
  const std::size_t region = src.size() / 20U;
  const std::size_t off = src.size() / 3U;
  const std::size_t end = std::min(off + region, src.size());
  for (std::size_t i = off; i < end; ++i) out[i] = static_cast<std::byte>(rng.byte());
  return out;
}

// ── Stats helpers ─────────────────────────────────────────────────────────────

struct ChunkStats {
  std::size_t count{};
  double avg_bytes{};
  double stddev_bytes{};
  std::size_t min_bytes{};
  std::size_t max_bytes{};
  double wall_ms{};
};

ChunkStats measure(const std::vector<std::byte>& data, const spoink::offline::CdcProfile& prof) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  const auto res = spoink::offline::chunk_bytes(data, prof);
  const double elapsed = std::chrono::duration<double, std::milli>(clock::now() - t0).count();

  if (!res.has_value() || res.value_if()->empty()) {
    ChunkStats r;
    r.wall_ms = elapsed;
    return r;
  }

  const auto& chunks = *res.value_if();
  double sum = 0.0, sum2 = 0.0;
  std::size_t lo = chunks[0].bytes.size(), hi = 0U;
  for (const auto& c : chunks) {
    const double s = static_cast<double>(c.bytes.size());
    sum += s;
    sum2 += s * s;
    lo = std::min(lo, c.bytes.size());
    hi = std::max(hi, c.bytes.size());
  }
  const double n = static_cast<double>(chunks.size());
  const double avg = sum / n;
  const double var = sum2 / n - avg * avg;
  ChunkStats r;
  r.count = chunks.size();
  r.avg_bytes = avg;
  r.stddev_bytes = var > 0.0 ? std::sqrt(var) : 0.0;
  r.min_bytes = lo;
  r.max_bytes = hi;
  r.wall_ms = elapsed;
  return r;
}

// Compute unique digests across two chunk lists (reuse fraction)
double reuse_ratio(const std::vector<std::byte>& original, const std::vector<std::byte>& modified,
                   const spoink::offline::CdcProfile& prof) {
  const auto orig_res = spoink::offline::chunk_bytes(original, prof);
  const auto mod_res = spoink::offline::chunk_bytes(modified, prof);
  if (!orig_res.has_value() || !mod_res.has_value()) return 0.0;

  std::set<std::string> orig_hashes;
  for (const auto& c : *orig_res.value_if()) orig_hashes.insert(c.digest.hex());

  std::size_t reused = 0U;
  for (const auto& c : *mod_res.value_if())
    if (orig_hashes.count(c.digest.hex())) ++reused;

  const double total = static_cast<double>(mod_res.value_if()->size());
  return total > 0.0 ? static_cast<double>(reused) / total : 0.0;
}

}  // namespace

int main() {
  using namespace spoink::offline;

  // ── Corpus ──────────────────────────────────────────────────────────────────
  struct File {
    std::string name;
    std::vector<std::byte> data;
  };

  const std::uint64_t corpus_seed = 0x6789abcdu;
  std::vector<File> corpus{
      {"text_source", gen_text(std::size_t{128} * 1024U, corpus_seed ^ 0x01U)},
      {"binary_data", gen_binary(std::size_t{512} * 1024U, corpus_seed ^ 0x02U)},
      {"config_a", gen_config(std::size_t{16} * 1024U, corpus_seed ^ 0x03U)},
      {"config_b", gen_config(std::size_t{8} * 1024U, corpus_seed ^ 0x04U)},
  };

  // ── Candidate profiles ───────────────────────────────────────────────────────
  const std::uint64_t poly = 0x3da3358b4dc173ULL;
  struct Profile {
    std::string label;
    CdcProfile p;
  };
  const std::vector<Profile> profiles{
      {"A_4KB", {poly, 16U, 256U, 4096U, 16384U, 0x00000FFFU, 0U}},
      {"B_16KB", {poly, 32U, 1024U, 16384U, 65536U, 0x00003FFFU, 0U}},
      {"C_64KB", {poly, 48U, 4096U, 65536U, 262144U, 0x0000FFFFU, 0U}},
      {"D_128KB", {poly, 48U, 8192U, 131072U, 524288U, 0x0001FFFFU, 0U}},
  };

  // ── CSV header ───────────────────────────────────────────────────────────────
  std::cout << "profile,corpus_file,change_pattern," << "chunk_count,avg_bytes,stddev_bytes,min_bytes,max_bytes,"
            << "reuse_ratio,wall_ms\n";

  for (const auto& prof : profiles) {
    for (const auto& file : corpus) {
      Rng rng(corpus_seed ^ std::hash<std::string>{}(file.name) ^ std::hash<std::string>{}(prof.label));

      struct Pattern {
        std::string name;
        std::vector<std::byte> data;
      };
      std::vector<Pattern> patterns;
      patterns.push_back({"unchanged", file.data});
      patterns.push_back({"append_5pct", apply_append(file.data, rng)});
      patterns.push_back({"insert_front_5pct", apply_insert_front(file.data, rng)});
      patterns.push_back({"delete_mid_5pct", apply_delete_mid(file.data)});
      patterns.push_back({"replace_region_5pct", apply_replace_region(file.data, rng)});

      for (const auto& pat : patterns) {
        const auto stats = measure(pat.data, prof.p);
        const double rr = (pat.name == "unchanged") ? 1.0 : reuse_ratio(file.data, pat.data, prof.p);

        std::cout << prof.label << ',' << file.name << ',' << pat.name << ',' << stats.count << ','
                  << static_cast<std::uint64_t>(std::lround(stats.avg_bytes)) << ','
                  << static_cast<std::uint64_t>(std::lround(stats.stddev_bytes)) << ','
                  << stats.min_bytes << ',' << stats.max_bytes << ',' << rr << ',' << stats.wall_ms << '\n';
      }
    }
  }

  // ── Frame-compatibility summary ──────────────────────────────────────────────
  std::cerr << "\n=== EXP-CDC-001 Frame Compatibility Analysis ===\n"
            << "Wire protocol constraint: one v1 chunk per frame (API-007)\n"
            << "TBD-LIMITS-001 status: OPEN (max_payload_bytes not yet frozen)\n\n"
            << "Profile max_bytes vs common frame ceilings:\n"
            << "  A_4KB:   max=16384 bytes   -- fits in any reasonable frame\n"
            << "  B_16KB:  max=65536 bytes   -- fits in any reasonable frame\n"
            << "  C_64KB:  max=262144 bytes  -- fits in >=256KB frame payload\n"
            << "  D_128KB: max=524288 bytes  -- requires >=512KB frame payload\n\n"
            << "Recommendation: select C_64KB (rabin-v1) for good dedup/metadata balance.\n"
            << "Confirm max=262144 <= TBD-LIMITS-001 negotiated ceiling before M2 freeze.\n";

  return 0;
}
