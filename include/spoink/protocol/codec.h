#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "spoink/support/result.h"

namespace spoink::protocol {

// ── Minimal deterministic CBOR encoder/decoder for protocol headers ──────────
//
// Subset used: unsigned integer, text string, byte string, boolean, null, array,
// map (sorted). Deterministic: shortest integer encoding; map keys sorted by
// length then lexicographic. No floating-point, no indefinite-length items.

// ── Encoder ─────────────────────────────────────────────────────────────────

class CborEncoder {
 public:
  void encode_uint(std::uint64_t v);
  void encode_text(std::string_view s);
  void encode_bytes(const std::uint8_t* data, std::size_t len);
  void encode_bytes(const std::byte* data, std::size_t len);
  void encode_bool(bool v);
  void encode_null();
  void encode_array_header(std::uint64_t count);
  void encode_map_header(std::uint64_t count);

  [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return buf_; }
  [[nodiscard]] std::vector<std::byte> take() { return std::move(buf_); }

 private:
  void write_head(std::uint8_t major, std::uint64_t value);
  std::vector<std::byte> buf_;
};

// ── Decoder ─────────────────────────────────────────────────────────────────

// Cursor-based decoder; advances through a byte slice.
class CborDecoder {
 public:
  explicit CborDecoder(const std::vector<std::byte>& data) : data_(data.data()), size_(data.size()) {}
  explicit CborDecoder(const std::byte* data, std::size_t size) : data_(data), size_(size) {}

  [[nodiscard]] bool done() const noexcept { return pos_ >= size_; }
  [[nodiscard]] std::size_t pos() const noexcept { return pos_; }

  // Read next item type without consuming.
  [[nodiscard]] std::uint8_t peek_major() const;

  support::Result<std::uint64_t> read_uint();
  support::Result<std::string> read_text(std::size_t max_bytes);
  support::Result<std::vector<std::byte>> read_bytes(std::size_t max_len);
  support::Result<bool> read_bool();
  support::Result<void> read_null();
  support::Result<std::uint64_t> read_array_header();
  support::Result<std::uint64_t> read_map_header();

  // Skip one CBOR item (recursive for nested structures).
  support::Result<void> skip();

 private:
  support::Result<std::uint64_t> read_head(std::uint8_t expected_major);
  [[nodiscard]] bool has(std::size_t n) const noexcept { return pos_ + n <= size_; }

  const std::byte* data_;
  std::size_t size_;
  std::size_t pos_{0};
};

// ── Message structs ──────────────────────────────────────────────────────────

struct PairHelloMsg {
  std::array<std::byte, 32> peer_id{};
  std::string display_name;
  std::array<std::byte, 32> attempt_nonce{};
};

struct PairConfirmMsg {
  bool accepted{false};
};

struct PairResultMsg {
  std::string status;  // "PAIRED" or "REJECTED"
};

struct HelloMsg {
  std::array<std::byte, 32> peer_id{};
  std::string role;
  std::array<std::byte, 32> instance_nonce{};
};

struct ErrorMsg {
  std::string error_code;
  bool retryable{false};
  std::optional<std::uint64_t> retry_after_ms;
};

struct PingMsg {
  std::uint64_t monotonic_token{0};
};

struct PongMsg {
  std::uint64_t monotonic_token{0};
};

struct FileSummary {
  std::uint32_t file_id{0};
  std::string relative_path;
  std::uint64_t logical_size{0};
  std::array<std::byte, 32> file_sha256{};
};

struct SessionOfferMsg {
  std::array<std::byte, 32> manifest_digest{};
  std::uint32_t manifest_schema{1};
  std::string chunk_profile_id;
  std::uint32_t file_count{0};
  std::uint64_t logical_bytes{0};
  bool overwrite_requested{false};
  std::vector<FileSummary> files;
};

struct SessionAcceptMsg {
  std::array<std::byte, 16> transfer_id{};
  std::vector<std::uint32_t> accepted_file_ids;
  bool overwrite_allowed{false};
};

struct SessionRejectMsg {
  std::string reason;
};

struct ManifestBeginMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> manifest_digest{};
  std::uint64_t total_bytes{0};
  std::uint32_t part_count{0};
};

struct ManifestPartMsg {
  std::array<std::byte, 16> transfer_id{};
  std::uint32_t part_index{0};
  std::uint64_t part_offset{0};
  // payload bytes sent separately as frame payload
};

struct ManifestEndMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> manifest_digest{};
};

struct TransferPlanMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> session_capability{};
  std::uint32_t plan_part{0};
  bool plan_final{true};
  std::vector<std::array<std::byte, 32>> missing_chunk_hashes;
};

struct DataBindMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> session_capability{};
  std::array<std::byte, 32> connection_nonce{};
};

struct DataBindResultMsg {
  bool accepted{false};
  std::string error_code;
};

struct ChunkPutMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> chunk_hash{};
  std::uint64_t plaintext_length{0};
  std::string codec;  // "raw" in M2
  std::uint64_t encoded_length{0};
  std::uint32_t attempt{0};
};

struct ChunkAckMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> chunk_hash{};
  std::string status;  // "STORED" or "ALREADY_PRESENT"
};

struct ChunkNackMsg {
  std::array<std::byte, 16> transfer_id{};
  std::array<std::byte, 32> chunk_hash{};
  std::string error_code;
  bool retryable{false};
};

struct FileResultMsg {
  std::array<std::byte, 16> transfer_id{};
  std::uint32_t file_id{0};
  std::string state;
  std::string error_code;
};

struct SessionResultMsg {
  std::array<std::byte, 16> transfer_id{};
  std::string state;
  std::uint32_t files_completed{0};
  std::uint32_t files_failed{0};
  std::string error_code;
};

struct CancelMsg {
  std::array<std::byte, 16> transfer_id{};
  std::string scope;  // "SESSION" or "FILE"
  std::string reason;
};

// ── Encode/decode functions ──────────────────────────────────────────────────

[[nodiscard]] std::vector<std::byte> encode_pair_hello(const PairHelloMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_pair_confirm(const PairConfirmMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_pair_result(const PairResultMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_hello(const HelloMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_error(const ErrorMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_ping(const PingMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_pong(const PongMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_session_offer(const SessionOfferMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_session_accept(const SessionAcceptMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_session_reject(const SessionRejectMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_manifest_begin(const ManifestBeginMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_manifest_part(const ManifestPartMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_manifest_end(const ManifestEndMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_transfer_plan(const TransferPlanMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_data_bind(const DataBindMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_data_bind_result(const DataBindResultMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_chunk_put(const ChunkPutMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_chunk_ack(const ChunkAckMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_chunk_nack(const ChunkNackMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_file_result(const FileResultMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_session_result(const SessionResultMsg& msg);
[[nodiscard]] std::vector<std::byte> encode_cancel(const CancelMsg& msg);

[[nodiscard]] support::Result<PairHelloMsg> decode_pair_hello(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<PairConfirmMsg> decode_pair_confirm(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<PairResultMsg> decode_pair_result(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<HelloMsg> decode_hello(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ErrorMsg> decode_error(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<PingMsg> decode_ping(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<PongMsg> decode_pong(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<SessionOfferMsg> decode_session_offer(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<SessionAcceptMsg> decode_session_accept(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<SessionRejectMsg> decode_session_reject(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ManifestBeginMsg> decode_manifest_begin(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ManifestPartMsg> decode_manifest_part(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ManifestEndMsg> decode_manifest_end(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<TransferPlanMsg> decode_transfer_plan(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<DataBindMsg> decode_data_bind(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<DataBindResultMsg> decode_data_bind_result(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ChunkPutMsg> decode_chunk_put(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ChunkAckMsg> decode_chunk_ack(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<ChunkNackMsg> decode_chunk_nack(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<FileResultMsg> decode_file_result(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<SessionResultMsg> decode_session_result(const std::vector<std::byte>& data);
[[nodiscard]] support::Result<CancelMsg> decode_cancel(const std::vector<std::byte>& data);

}  // namespace spoink::protocol
