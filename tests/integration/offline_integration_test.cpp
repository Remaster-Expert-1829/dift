//
// M1 Offline Engine — Integration tests
// Covers: multi-file corpus ingest, deduplication, reconstruct, restart/recovery, GC
//
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "spoink/offline/offline.h"

namespace spoink::offline {
namespace {

CdcProfile test_profile() { return {0x3da3358b4dc173ULL, 16U, 64U, 512U, 1024U, 0x07U, 0U}; }

std::vector<std::byte> make_bytes(const std::string& s) {
  std::vector<std::byte> out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  return out;
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::vector<char> raw{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
  std::vector<std::byte> out;
  out.reserve(raw.size());
  for (char c : raw) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

// ─── Multi-file corpus ingest ────────────────────────────────────────────────

TEST_CASE("INT-001: multi-file ingest produces stable manifests") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int001";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "src", ec);

  // Create corpus: two files, some shared content
  const std::string shared = "shared content block that will appear in both files ";
  const auto alpha_data = make_bytes(shared + "alpha unique portion aaaa");
  const auto beta_data = make_bytes(shared + "beta unique portion bbbb");

  {
    std::ofstream f(root / "src" / "alpha.bin", std::ios::binary);
    f.write(reinterpret_cast<const char*>(alpha_data.data()), static_cast<std::streamsize>(alpha_data.size()));
  }
  {
    std::ofstream f(root / "src" / "beta.bin", std::ios::binary);
    f.write(reinterpret_cast<const char*>(beta_data.data()), static_cast<std::streamsize>(beta_data.size()));
  }

  const auto selected = select_files({root / "src"}, root / "src");
  REQUIRE(selected.has_value());
  REQUIRE(selected.value_if()->size() == 2U);

  // Chunk and install all files
  std::vector<ManifestFile> mfiles;
  for (const auto& sf : *selected.value_if()) {
    const auto raw = read_file_bytes(sf.source);
    const auto chunks = chunk_bytes(raw, test_profile());
    REQUIRE(chunks.has_value());
    const auto digest = sha256(raw);
    REQUIRE(digest.has_value());

    std::vector<Chunk> installed;
    for (auto& c : *chunks.value_if()) {
      REQUIRE(install_verified_chunk(root / "store", c).has_value());
      installed.push_back(c);
    }

    ManifestFile mf{sf.manifest_path, raw.size(), *digest.value_if(), installed};
    mfiles.push_back(mf);
  }

  const auto manifest1 = build_manifest(mfiles, test_profile(), 1000U);
  const auto manifest2 = build_manifest(mfiles, test_profile(), 1000U);
  REQUIRE(manifest1.has_value());
  REQUIRE(manifest2.has_value());
  REQUIRE(manifest1.value_if()->bytes == manifest2.value_if()->bytes);

  std::filesystem::remove_all(root, ec);
}

// ─── Deduplication ───────────────────────────────────────────────────────────

TEST_CASE("INT-002: identical file chunks deduplicate in the store") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int002";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("identical content that should only be stored once in the chunk store");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};

  REQUIRE(install_verified_chunk(root / "store", c).has_value());
  REQUIRE(install_verified_chunk(root / "store", c).has_value());

  // Count blobs in store
  std::size_t blob_count = 0U;
  const auto chunks_dir = root / "store" / "chunks";
  if (std::filesystem::exists(chunks_dir, ec)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(chunks_dir, ec)) {
      if (entry.is_regular_file(ec)) ++blob_count;
    }
  }
  REQUIRE(blob_count == 1U);

  std::filesystem::remove_all(root, ec);
}

// ─── Reconstruct ─────────────────────────────────────────────────────────────

TEST_CASE("INT-003: reconstruct multi-chunk file reproduces exact content") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int003";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "dest", ec);

  // Build data large enough to produce multiple chunks
  std::string raw_str(4096U, 'A');
  for (std::size_t i = 0U; i < raw_str.size(); ++i) raw_str[i] = static_cast<char>('A' + (i % 26U));
  const auto data = make_bytes(raw_str);

  const CdcProfile small_profile{0x3da3358b4dc173ULL, 8U, 16U, 64U, 256U, 0x03U, 0U};
  const auto chunks = chunk_bytes(data, small_profile);
  REQUIRE(chunks.has_value());
  REQUIRE(chunks.value_if()->size() > 1U);  // Must have multiple chunks

  const auto digest = sha256(data);
  REQUIRE(digest.has_value());

  for (const auto& c : *chunks.value_if()) {
    REQUIRE(install_verified_chunk(root / "store", c).has_value());
  }

  ManifestFile mf{"result/output.bin", data.size(), *digest.value_if(), *chunks.value_if()};
  std::filesystem::create_directories(root / "dest" / "result", ec);
  const auto staged = reconstruct_verified_file(root / "store", root / "dest", mf);
  REQUIRE(staged.has_value());
  REQUIRE(finalize_verified_file(*staged.value_if()).has_value());

  const auto out = read_file_bytes(staged.value_if()->final_path);
  REQUIRE(out == data);

  std::filesystem::remove_all(root, ec);
}

// ─── Restart / Recovery ──────────────────────────────────────────────────────

TEST_CASE("INT-004: MetadataStore re-opens and retains records after restart") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int004";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("persistent record");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());

  {
    auto db = MetadataStore::open(root / "state");
    REQUIRE(db.has_value());
    REQUIRE(db.value_if()->record_verified(*digest.value_if(), data.size()).has_value());
    REQUIRE(*db.value_if()->verified_count().value_if() == 1U);
    // db goes out of scope here — simulating process restart
  }

  {
    auto db2 = MetadataStore::open(root / "state");
    REQUIRE(db2.has_value());
    REQUIRE(*db2.value_if()->verified_count().value_if() == 1U);
    const auto is_ver = db2.value_if()->is_verified(*digest.value_if());
    REQUIRE(is_ver.has_value());
    REQUIRE(*is_ver.value_if());
  }

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("INT-004: reconcile_blob_store recovers orphaned blobs after restart") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int004b";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("orphaned blob that should be re-adopted");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};

  // Install blob on disk but open fresh DB (simulating crash before DB record)
  REQUIRE(install_verified_chunk(root / "store", c).has_value());
  auto db = MetadataStore::open(root / "state");
  REQUIRE(db.has_value());

  // DB doesn't know about the blob yet
  REQUIRE(*db.value_if()->verified_count().value_if() == 0U);

  // Reconcile adopts it
  const auto report = reconcile_blob_store(root / "store", db.value_if());
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->verified_blobs == 1U);
  REQUIRE(*db.value_if()->verified_count().value_if() == 1U);

  std::filesystem::remove_all(root, ec);
}

// ─── Inspect / GC ────────────────────────────────────────────────────────────

TEST_CASE("INT-005: GC dry-run lists eligible blobs without removing them") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int005";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto db = MetadataStore::open(root / "state");
  REQUIRE(db.has_value());

  // Record a blob with ref_count=0 (eligible for GC)
  const auto data = make_bytes("unreferenced chunk");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", c).has_value());
  REQUIRE(db.value_if()->record_verified(*digest.value_if(), data.size()).has_value());

  GcPolicy policy;
  policy.grace_period_ms = 0U;
  policy.require_explicit = false;
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  // Dry-run: plan_gc with dry_run=true returns the eligible list without making DB changes
  const auto plan = db.value_if()->plan_gc(policy, true, now_ms);
  REQUIRE(plan.has_value());
  REQUIRE(plan.value_if()->is_dry_run);
  REQUIRE_FALSE(plan.value_if()->eligible.empty());

  // execute_gc refuses a dry-run plan — dry-run is inspect-only (check eligible list)
  REQUIRE_FALSE(db.value_if()->execute_gc(root / "store", *plan.value_if(), now_ms).has_value());

  // Blob still exists because no live GC was executed
  const auto still_there = read_verified_chunk(root / "store", *digest.value_if());
  REQUIRE(still_there.has_value());

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("INT-005: GC removes unreferenced blobs after grace period") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int005b";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto db = MetadataStore::open(root / "state");
  REQUIRE(db.has_value());

  const auto data = make_bytes("to be garbage collected");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", c).has_value());
  REQUIRE(db.value_if()->record_verified(*digest.value_if(), data.size()).has_value());

  GcPolicy policy;
  policy.grace_period_ms = 0U;
  policy.require_explicit = false;
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const auto plan = db.value_if()->plan_gc(policy, false, now_ms);
  REQUIRE(plan.has_value());

  const auto report = db.value_if()->execute_gc(root / "store", *plan.value_if(), now_ms);
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->removed >= 1U);

  // Blob should be gone
  const auto gone = read_verified_chunk(root / "store", *digest.value_if());
  REQUIRE_FALSE(gone.has_value());

  std::filesystem::remove_all(root, ec);
}

// ─── decode_manifest round-trip ──────────────────────────────────────────────

TEST_CASE("INT-006: build_manifest then decode_manifest round-trips faithfully") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-int006";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("round trip content for manifest test");
  const auto chunks = chunk_bytes(data, test_profile());
  REQUIRE(chunks.has_value());
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());

  ManifestFile mf{"files/data.bin", data.size(), *digest.value_if(), *chunks.value_if()};
  const auto built = build_manifest({mf}, test_profile(), 9999U);
  REQUIRE(built.has_value());

  const auto decoded = decode_manifest(built.value_if()->bytes);
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value_if()->files.size() == 1U);
  REQUIRE(decoded.value_if()->files[0].path == "files/data.bin");
  REQUIRE(decoded.value_if()->files[0].size == data.size());
  REQUIRE(decoded.value_if()->files[0].digest == *digest.value_if());
  REQUIRE(decoded.value_if()->files[0].chunks.size() == chunks.value_if()->size());

  std::filesystem::remove_all(root, ec);
}

}  // namespace spoink::offline
