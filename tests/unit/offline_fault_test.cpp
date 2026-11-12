//
// TEST-FAULT-002: Chunk installation fault behavior
// TEST-FAULT-003: Staging/finalization fault behavior
// TEST-FAULT-004: Corrupt/missing blob behavior
// TEST-FAULT-006: Reconciliation after interrupted state
//
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "spoink/offline/offline.h"

namespace spoink::offline {
namespace {

std::vector<std::byte> make_bytes(const std::string& s) {
  std::vector<std::byte> out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

// ─── TEST-FAULT-002: Chunk installation ──────────────────────────────────────

TEST_CASE("TEST-FAULT-002: installing a chunk with wrong digest fails") {
  const auto store = std::filesystem::temp_directory_path() / "spoink-fault002-hash";
  std::error_code ec;
  std::filesystem::remove_all(store, ec);

  const auto data = make_bytes("correct data");
  const auto correct_digest = sha256(data);
  REQUIRE(correct_digest.has_value());

  // Build a chunk with a wrong digest
  const auto wrong_data = make_bytes("wrong data for digest");
  const auto wrong_digest = sha256(wrong_data);
  REQUIRE(wrong_digest.has_value());

  Chunk bad_chunk{0U, data, *wrong_digest.value_if()};
  REQUIRE_FALSE(install_verified_chunk(store, bad_chunk).has_value());

  // Verify no partial file was left
  REQUIRE_FALSE(std::filesystem::exists(store / "chunks", ec));

  std::filesystem::remove_all(store, ec);
}

TEST_CASE("TEST-FAULT-002: installing an empty chunk fails") {
  const auto store = std::filesystem::temp_directory_path() / "spoink-fault002-empty";
  std::error_code ec;
  std::filesystem::remove_all(store, ec);

  const auto digest_result = sha256({});
  REQUIRE(digest_result.has_value());
  Chunk empty_chunk{0U, {}, *digest_result.value_if()};
  REQUIRE_FALSE(install_verified_chunk(store, empty_chunk).has_value());

  std::filesystem::remove_all(store, ec);
}

TEST_CASE("TEST-FAULT-002: duplicate valid chunk installation is idempotent") {
  const auto store = std::filesystem::temp_directory_path() / "spoink-fault002-dup";
  std::error_code ec;
  std::filesystem::remove_all(store, ec);

  const auto data = make_bytes("duplicate content");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};

  REQUIRE(install_verified_chunk(store, c).has_value());
  REQUIRE(install_verified_chunk(store, c).has_value());  // must succeed (idempotent)

  const auto loaded = read_verified_chunk(store, *digest.value_if());
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded.value_if() == data);

  std::filesystem::remove_all(store, ec);
}

// ─── TEST-FAULT-004: Corrupt and missing blobs ───────────────────────────────

TEST_CASE("TEST-FAULT-004: read_verified_chunk rejects corrupted blobs") {
  const auto store = std::filesystem::temp_directory_path() / "spoink-fault004-corrupt";
  std::error_code ec;
  std::filesystem::remove_all(store, ec);

  const auto data = make_bytes("original content");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(store, c).has_value());

  // Overwrite the blob with garbage
  const std::string hex = digest.value_if()->hex();
  const auto blob_path = store / "chunks" / hex.substr(0U, 2U) / hex.substr(2U, 2U) / hex;
  {
    std::ofstream f(blob_path, std::ios::trunc | std::ios::binary);
    f << "corrupted";
  }

  REQUIRE_FALSE(read_verified_chunk(store, *digest.value_if()).has_value());

  std::filesystem::remove_all(store, ec);
}

TEST_CASE("TEST-FAULT-004: read_verified_chunk fails for missing blobs") {
  const auto store = std::filesystem::temp_directory_path() / "spoink-fault004-missing";
  std::error_code ec;
  std::filesystem::remove_all(store, ec);
  std::filesystem::create_directories(store, ec);

  const auto data = make_bytes("missing chunk");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());

  // Don't install, just try to read
  REQUIRE_FALSE(read_verified_chunk(store, *digest.value_if()).has_value());

  std::filesystem::remove_all(store, ec);
}

// ─── TEST-FAULT-003: Staging and finalization ────────────────────────────────

TEST_CASE("TEST-FAULT-003: reconstruction fails if destination parent does not exist") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault003-nodir";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("content");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", c).has_value());

  ManifestFile f{"out.bin", data.size(), *digest.value_if(), {c}};
  // Destination root does not exist
  REQUIRE_FALSE(reconstruct_verified_file(root / "store", root / "nonexistent_dest", f).has_value());

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-003: reconstruction fails if destination file already exists") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault003-exists";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "dest", ec);

  const auto data = make_bytes("content");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", c).has_value());

  // Pre-create the destination
  {
    std::ofstream f(root / "dest" / "out.bin");
    f << "existing";
  }

  ManifestFile f{"out.bin", data.size(), *digest.value_if(), {c}};
  REQUIRE_FALSE(reconstruct_verified_file(root / "store", root / "dest", f).has_value());

  // The pre-existing file must still exist and be unchanged
  REQUIRE(std::filesystem::exists(root / "dest" / "out.bin", ec));
  {
    std::ifstream rf(root / "dest" / "out.bin");
    std::string content{std::istreambuf_iterator<char>(rf), std::istreambuf_iterator<char>()};
    REQUIRE(content == "existing");
  }

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-003: finalize_verified_file requires staging to exist") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault003-nostage";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);

  // Build a VerifiedStagingFile with non-existent staging path
  const auto d = sha256(make_bytes("x"));
  REQUIRE(d.has_value());
  VerifiedStagingFile fake{root / ".nosuchstaging", root / "final.bin", 1U, *d.value_if()};

  REQUIRE_FALSE(finalize_verified_file(fake).has_value());

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-003: successful reconstruction then finalize produces correct file") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault003-full";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "dest", ec);

  const auto data = make_bytes("reconstructed file content for fault test");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root / "store", c).has_value());

  ManifestFile f{"output/result.bin", data.size(), *digest.value_if(), {c}};
  const auto staged = reconstruct_verified_file(root / "store", root / "dest", f);
  REQUIRE(staged.has_value());
  REQUIRE(std::filesystem::exists(staged.value_if()->staging_path, ec));
  REQUIRE_FALSE(std::filesystem::exists(staged.value_if()->final_path, ec));

  REQUIRE(finalize_verified_file(*staged.value_if()).has_value());
  REQUIRE(std::filesystem::exists(staged.value_if()->final_path, ec));

  // Final file must contain correct content
  std::ifstream rf(staged.value_if()->final_path, std::ios::binary);
  std::vector<char> raw_content{std::istreambuf_iterator<char>(rf), std::istreambuf_iterator<char>()};
  std::vector<std::byte> read_bytes;
  read_bytes.reserve(raw_content.size());
  for (char c_val : raw_content) read_bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c_val)));
  REQUIRE(read_bytes == data);

  std::filesystem::remove_all(root, ec);
}

// ─── TEST-FAULT-006: Reconciliation ─────────────────────────────────────────

TEST_CASE("TEST-FAULT-006: reconcile_blob_store quarantines corrupt blobs") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault006-reconcile";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // Install one good blob
  const auto good_data = make_bytes("good blob");
  const auto good_digest = sha256(good_data);
  REQUIRE(good_digest.has_value());
  Chunk good_c{0U, good_data, *good_digest.value_if()};
  REQUIRE(install_verified_chunk(root, good_c).has_value());

  // Plant a corrupt file with a sha256-name that doesn't match content
  const std::string fake_hex = "aabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccdd";
  std::filesystem::create_directories(root / "chunks" / "aa" / "bb", ec);
  {
    std::ofstream f(root / "chunks" / "aa" / "bb" / fake_hex);
    f << "not what it claims";
  }

  // Plant a file with a non-hex name (should be quarantined)
  std::filesystem::create_directories(root / "chunks" / "xx" / "yy", ec);
  {
    std::ofstream f(root / "chunks" / "xx" / "yy" / "not-a-sha256-name");
    f << "garbage";
  }

  const auto report = reconcile_blob_store(root, nullptr);
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->verified_blobs == 1U);
  REQUIRE(report.value_if()->quarantined_blobs == 2U);
  REQUIRE(std::filesystem::exists(root / "quarantine", ec));

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-006: reconcile_blob_store with empty store produces empty report") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault006-empty";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);

  const auto report = reconcile_blob_store(root, nullptr);
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->verified_blobs == 0U);
  REQUIRE(report.value_if()->quarantined_blobs == 0U);

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-006: reconcile updates MetadataStore when DB provided") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault006-db";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("db reconcile test");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root, c).has_value());

  // Open DB but don't pre-record the chunk
  auto db = MetadataStore::open(root / "state");
  REQUIRE(db.has_value());

  // Reconcile with DB: should adopt the orphaned valid blob
  const auto report = reconcile_blob_store(root, db.value_if());
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->verified_blobs >= 1U);

  // After reconcile, the chunk should be visible in the DB
  const auto is_ver = db.value_if()->is_verified(*digest.value_if());
  REQUIRE(is_ver.has_value());
  REQUIRE(*is_ver.value_if());

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("TEST-FAULT-006: corrupt blob with DB marks entry quarantined") {
  const auto root = std::filesystem::temp_directory_path() / "spoink-fault006-dbcorrupt";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const auto data = make_bytes("original");
  const auto digest = sha256(data);
  REQUIRE(digest.has_value());
  Chunk c{0U, data, *digest.value_if()};
  REQUIRE(install_verified_chunk(root, c).has_value());

  auto db = MetadataStore::open(root / "state");
  REQUIRE(db.has_value());
  REQUIRE(db.value_if()->record_verified(*digest.value_if(), data.size()).has_value());

  // Corrupt the blob on disk
  const std::string hex = digest.value_if()->hex();
  const auto blob = root / "chunks" / hex.substr(0U, 2U) / hex.substr(2U, 2U) / hex;
  {
    std::ofstream f(blob, std::ios::trunc | std::ios::binary);
    f << "corrupted";
  }

  const auto report = reconcile_blob_store(root, db.value_if());
  REQUIRE(report.has_value());
  REQUIRE(report.value_if()->quarantined_blobs == 1U);

  std::filesystem::remove_all(root, ec);
}

}  // namespace spoink::offline
