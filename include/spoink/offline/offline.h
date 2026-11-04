#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "spoink/support/result.h"
#include "spoink/support/types.h"

struct sqlite3;

namespace spoink::offline {

struct Limits {
  static constexpr std::size_t kMaxPathBytes = 4096;
  static constexpr std::size_t kMaxSegmentBytes = 255;
  static constexpr std::size_t kMaxFiles = 10000;
  static constexpr std::size_t kMaxManifestBytes = std::size_t{16} * 1024U * 1024U;
  static constexpr std::size_t kMaxChunkReferences = 1000000;
  static constexpr std::size_t kMinWindowBytes = 1;
  static constexpr std::size_t kMaxWindowBytes = 1024;
  static constexpr std::size_t kMinChunkBytes = 1;
  static constexpr std::size_t kMaxChunkBytes = std::size_t{64} * 1024U * 1024U;
  static constexpr std::uint64_t kMaxFileSizeBytes = std::uint64_t{256} * 1024U * 1024U * 1024U;
};

struct CdcProfile {
  std::uint64_t polynomial;
  std::size_t window_bytes;
  std::size_t min_bytes;
  std::size_t target_bytes;
  std::size_t max_bytes;
  std::uint64_t boundary_mask;
  std::uint64_t boundary_value;
};

// rabin-v1: selected by EXP-CDC-001 (2026-11-03). Parameterized runtime remains
// authoritative; this constant exists only to record the frozen selection.
// M2 freeze prerequisite: confirm max_bytes (65536) <= TBD-LIMITS-001 ceiling.
[[nodiscard]] inline CdcProfile rabin_v1_profile() noexcept {
  return {0x3da3358b4dc173ULL, 32U, 1024U, 16384U, 65536U, 0x00003FFFULL, 0U};
}

struct Chunk {
  std::uint64_t offset;
  std::vector<std::byte> bytes;
  support::Sha256Digest digest;
};

struct SelectedFile {
  std::filesystem::path source;
  std::string manifest_path;
  std::uint64_t size;
};

struct ManifestFile {
  std::string path;
  std::uint64_t size;
  support::Sha256Digest digest;
  std::vector<Chunk> chunks;
};

struct CanonicalManifest {
  std::vector<std::byte> bytes;
  support::Sha256Digest digest;
  std::vector<ManifestFile> files;
};

// A token returned only after reconstruction has verified the whole file.
// Prevents callers from finalizing an unchecked staging path.
struct VerifiedStagingFile {
  std::filesystem::path staging_path;
  std::filesystem::path final_path;
  std::uint64_t size;
  support::Sha256Digest digest;
};

struct ReconciliationReport {
  std::size_t verified_blobs = 0U;
  std::size_t quarantined_blobs = 0U;
};

struct GcPolicy {
  std::uint64_t grace_period_ms = 0U;
  bool require_explicit = true;
};

struct GcEntry {
  support::Sha256Digest hash;
  std::uint64_t length;
  std::string store_relpath;
};

struct GcPlan {
  std::vector<GcEntry> eligible;
  GcPolicy policy;
  bool is_dry_run = true;
};

struct GcReport {
  std::size_t removed = 0U;
  std::size_t retained = 0U;
  std::size_t errors = 0U;
};

// Single-instance state-root lock held for the lifetime of this object.
class StateRootLock {
 public:
  StateRootLock() = default;
  ~StateRootLock();
  StateRootLock(const StateRootLock&) = delete;
  StateRootLock& operator=(const StateRootLock&) = delete;
  StateRootLock(StateRootLock&& other) noexcept;
  StateRootLock& operator=(StateRootLock&& other) noexcept;

  [[nodiscard]] static support::Result<StateRootLock> acquire(const std::filesystem::path& state_root);
  [[nodiscard]] bool is_held() const noexcept { return fd_ >= 0; }

 private:
  explicit StateRootLock(int fd) : fd_(fd) {}
  int fd_ = -1;
};

class MetadataStore {
 public:
  MetadataStore() = default;
  ~MetadataStore();
  MetadataStore(const MetadataStore&) = delete;
  MetadataStore& operator=(const MetadataStore&) = delete;
  MetadataStore(MetadataStore&& other) noexcept;
  MetadataStore& operator=(MetadataStore&& other) noexcept;

  [[nodiscard]] static support::Result<MetadataStore> open(const std::filesystem::path& state_root);

  // Schema management
  [[nodiscard]] support::Result<int> schema_version() const;
  [[nodiscard]] support::Result<void> verify_wal_and_fk() const;

  // Chunk catalog operations
  [[nodiscard]] support::Result<void> record_verified(const support::Sha256Digest& digest, std::uint64_t byte_length);
  [[nodiscard]] support::Result<bool> is_verified(const support::Sha256Digest& digest) const;
  [[nodiscard]] support::Result<std::uint64_t> verified_count() const;
  [[nodiscard]] support::Result<void> mark_quarantined(const support::Sha256Digest& digest, const std::string& reason);

  // Ref-count recomputation (call after reconciliation)
  [[nodiscard]] support::Result<void> recompute_ref_counts();

  // GC: returns eligible unreferenced, unleased, expired-grace-period blobs.
  // Dry-run makes no DB changes. Execute claims rows with a GC lease.
  [[nodiscard]] support::Result<GcPlan> plan_gc(const GcPolicy& policy, bool dry_run, std::uint64_t now_ms) const;
  [[nodiscard]] support::Result<GcReport> execute_gc(const std::filesystem::path& store_root, const GcPlan& plan,
                                                     std::uint64_t now_ms);

  // Dirty marker: write before mutations, clear after successful reconciliation
  [[nodiscard]] support::Result<void> set_dirty(bool dirty);
  [[nodiscard]] support::Result<bool> is_dirty() const;

 private:
  explicit MetadataStore(sqlite3* database) : database_(database) {}
  sqlite3* database_ = nullptr;
};

// Path/selection
[[nodiscard]] support::Result<std::string> normalize_manifest_path(const std::string& path);
[[nodiscard]] support::Result<std::vector<SelectedFile>> select_files(const std::vector<std::filesystem::path>& inputs,
                                                                      const std::filesystem::path& root);

// Hashing
[[nodiscard]] support::Result<support::Sha256Digest> sha256(const std::vector<std::byte>& bytes);

// CDC with correct Rabin sliding window
[[nodiscard]] support::Result<std::vector<Chunk>> chunk_bytes(const std::vector<std::byte>& bytes,
                                                              const CdcProfile& profile);

// Manifest codec
[[nodiscard]] support::Result<CanonicalManifest> build_manifest(std::vector<ManifestFile> files,
                                                                const CdcProfile& profile,
                                                                std::uint64_t created_at_unix_ms);

// Structural CBOR canonical-form check (fast reject of clearly non-canonical bytes)
[[nodiscard]] support::Result<void> validate_canonical_manifest(const std::vector<std::byte>& bytes);

// Strict schema-1 decoder: returns validated domain objects or failure
[[nodiscard]] support::Result<CanonicalManifest> decode_manifest(const std::vector<std::byte>& bytes);

// Blob store
[[nodiscard]] support::Result<void> install_verified_chunk(const std::filesystem::path& store_root, const Chunk& chunk);
[[nodiscard]] support::Result<std::vector<std::byte>> read_verified_chunk(const std::filesystem::path& store_root,
                                                                          const support::Sha256Digest& digest);

// Reconstruction (no existing destination modified before final atomic rename)
[[nodiscard]] support::Result<VerifiedStagingFile> reconstruct_verified_file(
    const std::filesystem::path& store_root, const std::filesystem::path& destination_root, const ManifestFile& file);
[[nodiscard]] support::Result<void> finalize_verified_file(const VerifiedStagingFile& staging);

// Startup reconciliation
[[nodiscard]] support::Result<ReconciliationReport> reconcile_blob_store(const std::filesystem::path& store_root,
                                                                         MetadataStore* db);

}  // namespace spoink::offline
