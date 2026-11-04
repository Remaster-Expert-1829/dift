#include "spoink/offline/offline.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace spoink::offline {
namespace {

CdcProfile profile() { return {0x3da3358b4dc173ULL, 16U, 4U, 8U, 16U, 0x07U, 0U}; }

std::vector<std::byte> bytes(const std::string& input) {
  std::vector<std::byte> output;
  for (const char value : input) output.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  return output;
}

}  // namespace

TEST_CASE("M1 path validation rejects traversal and non-NFC input") {
  REQUIRE(normalize_manifest_path("folder/file.txt").has_value());
  REQUIRE_FALSE(normalize_manifest_path("../file.txt").has_value());
  REQUIRE_FALSE(normalize_manifest_path("folder//file.txt").has_value());
  REQUIRE_FALSE(normalize_manifest_path("folder\\file.txt").has_value());
  REQUIRE_FALSE(normalize_manifest_path("folder/\x01file.txt").has_value());
  REQUIRE_FALSE(normalize_manifest_path("e\xCC\x81.txt").has_value());
}

TEST_CASE("selection rejects files outside its declared root and source symlinks") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-m1-selection-test";
  const auto outside = std::filesystem::temp_directory_path() / "spoink-m1-selection-outside";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::remove_all(outside, error);
  std::filesystem::create_directories(root / "nested", error);
  std::filesystem::create_directories(outside, error);
  { std::ofstream(root / "nested" / "z.txt") << "z"; }
  { std::ofstream(root / "a.txt") << "a"; }
  { std::ofstream(outside / "outside.txt") << "outside"; }
  REQUIRE(select_files({root}, root).has_value());
  const auto selected = select_files({root}, root);
  REQUIRE(selected.value_if()->at(0).manifest_path == "a.txt");
  REQUIRE_FALSE(select_files({outside / "outside.txt"}, root).has_value());
  std::filesystem::create_symlink(root / "a.txt", root / "link.txt", error);
  REQUIRE_FALSE(select_files({root}, root).has_value());
  REQUIRE_FALSE(select_files({root / "link.txt"}, root).has_value());
  std::filesystem::remove_all(root, error);
  std::filesystem::remove_all(outside, error);
}

TEST_CASE("parameterized CDC reconstructs input using contiguous bounded chunks") {
  const auto input = bytes("0123456789abcdefghijklmnopqrstuvwxyz");
  const auto chunks = chunk_bytes(input, profile());
  REQUIRE(chunks.has_value());
  std::vector<std::byte> reconstructed;
  std::uint64_t offset = 0U;
  const auto& cv = *chunks.value_if();
  for (std::size_t i = 0U; i < cv.size(); ++i) {
    REQUIRE(cv[i].offset == offset);
    // All chunks must not exceed max_bytes; non-final chunks must be >= min_bytes.
    // The final chunk may be smaller than min_bytes (standard CDC behaviour).
    if (i + 1U < cv.size()) REQUIRE(cv[i].bytes.size() >= profile().min_bytes);
    REQUIRE(cv[i].bytes.size() <= profile().max_bytes);
    REQUIRE(cv[i].bytes.size() > 0U);
    reconstructed.insert(reconstructed.end(), cv[i].bytes.begin(), cv[i].bytes.end());
    offset += cv[i].bytes.size();
  }
  REQUIRE(reconstructed == input);
  REQUIRE(chunk_bytes({}, profile()).value_if()->empty());
  auto invalid_profile = profile();
  invalid_profile.min_bytes = 0U;
  REQUIRE_FALSE(chunk_bytes(input, invalid_profile).has_value());
}

TEST_CASE("canonical manifest has deterministic bytes and changes with explicit inputs") {
  const auto input = bytes("manifest data");
  const auto chunks = chunk_bytes(input, profile());
  const auto digest = sha256(input);
  REQUIRE(chunks.has_value());
  REQUIRE(digest.has_value());
  ManifestFile file{"alpha.txt", static_cast<std::uint64_t>(input.size()), *digest.value_if(), *chunks.value_if()};
  const auto first = build_manifest({file}, profile(), 1234U);
  const auto second = build_manifest({file}, profile(), 1234U);
  const auto changed = build_manifest({file}, profile(), 1235U);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(changed.has_value());
  REQUIRE(first.value_if()->bytes == second.value_if()->bytes);
  REQUIRE(first.value_if()->digest == second.value_if()->digest);
  REQUIRE(first.value_if()->digest != changed.value_if()->digest);
  REQUIRE(validate_canonical_manifest(first.value_if()->bytes).has_value());
  const std::vector<std::byte> noncanonical_integer = {std::byte{0xa1}, std::byte{0x61}, std::byte{'a'},
                                                       std::byte{0x18}, std::byte{0x01}};
  const std::vector<std::byte> unordered_map = {std::byte{0xa2}, std::byte{0x61}, std::byte{'b'}, std::byte{0x01},
                                                std::byte{0x61}, std::byte{'a'},  std::byte{0x01}};
  REQUIRE_FALSE(validate_canonical_manifest(noncanonical_integer).has_value());
  REQUIRE_FALSE(validate_canonical_manifest(unordered_map).has_value());
}

TEST_CASE("verified chunk store is idempotent and SQLite schema records metadata") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-m1-offline-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  const auto input = bytes("deduplicated content");
  const auto digest = sha256(input);
  REQUIRE(digest.has_value());
  Chunk chunk{0U, input, *digest.value_if()};
  REQUIRE(install_verified_chunk(root, chunk).has_value());
  REQUIRE(install_verified_chunk(root, chunk).has_value());
  const auto loaded = read_verified_chunk(root, *digest.value_if());
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded.value_if() == input);
  auto store = MetadataStore::open(root / "state");
  REQUIRE(store.has_value());
  REQUIRE(store.value_if()->record_verified(*digest.value_if(), input.size()).has_value());
  REQUIRE(store.value_if()->record_verified(*digest.value_if(), input.size()).has_value());
  REQUIRE_FALSE(store.value_if()->record_verified(*digest.value_if(), input.size() + 1U).has_value());
  REQUIRE(*store.value_if()->verified_count().value_if() == 1U);
  std::filesystem::create_directories(root / "chunks" / "aa" / "bb", error);
  { std::ofstream(root / "chunks" / "aa" / "bb" / "not-a-sha256") << "corrupt"; }
  const auto reconciliation = reconcile_blob_store(root, nullptr);
  REQUIRE(reconciliation.has_value());
  REQUIRE(reconciliation.value_if()->verified_blobs == 1U);
  REQUIRE(reconciliation.value_if()->quarantined_blobs == 1U);
  REQUIRE(std::filesystem::exists(root / "quarantine" / "not-a-sha256.corrupt"));
  std::filesystem::remove_all(root, error);
}

TEST_CASE("reconstruction verifies staging before atomically making a new file visible") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-m1-reconstruct-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  const auto input = bytes("reconstruct this exact content");
  const auto digest = sha256(input);
  REQUIRE(digest.has_value());
  Chunk chunk{0U, input, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", chunk).has_value());
  ManifestFile file{"nested/result.bin", static_cast<std::uint64_t>(input.size()), *digest.value_if(), {chunk}};
  const auto staging = reconstruct_verified_file(root / "store", root / "destination", file);
  REQUIRE_FALSE(staging.has_value());
  std::filesystem::create_directories(root / "destination", error);
  const auto verified = reconstruct_verified_file(root / "store", root / "destination", file);
  REQUIRE(verified.has_value());
  REQUIRE_FALSE(std::filesystem::exists(verified.value_if()->final_path));
  REQUIRE(finalize_verified_file(*verified.value_if()).has_value());
  REQUIRE(std::filesystem::exists(verified.value_if()->final_path));
  REQUIRE_FALSE(reconstruct_verified_file(root / "store", root / "destination", file).has_value());
  std::filesystem::remove_all(root, error);
}

TEST_CASE("R3: grace period prevents newly recorded blob from being GC eligible") {
  std::filesystem::path root = std::filesystem::temp_directory_path() / "spoink_gc_grace_test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);

  const auto db_path = root / "meta.db";
  auto store = MetadataStore::open(db_path);
  REQUIRE(store.has_value());

  const auto input = bytes("grace period test blob content");
  const auto digest = sha256(input);
  REQUIRE(digest.has_value());
  REQUIRE(store.value_if()->record_verified(*digest.value_if(), input.size()).has_value());

  // plan_gc with a 1-hour grace period — blob was just recorded, must not be eligible
  GcPolicy policy;
  policy.grace_period_ms = 60ULL * 60ULL * 1000ULL;  // 1 hour
  policy.require_explicit = false;
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const auto plan = store.value_if()->plan_gc(policy, /*dry_run=*/true, now_ms);
  REQUIRE(plan.has_value());

  const auto& eligible = plan.value_if()->eligible;
  const bool found = std::any_of(eligible.begin(), eligible.end(),
                                 [&](const GcEntry& e) { return e.hash == *digest.value_if(); });
  REQUIRE_FALSE(found);

  std::filesystem::remove_all(root, error);
}

}  // namespace spoink::offline
