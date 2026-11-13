#include "spoink/protocol/codec.h"

#include <algorithm>
#include <cstring>

#include "spoink/support/error.h"

namespace spoink::protocol {

namespace {

support::Error make_malformed(const char* key) {
  return support::make_error(support::ErrorCode::kInvalidArgument, key);
}

void copy32(std::array<std::byte, 32>& dst, const std::vector<std::byte>& src) {
  std::copy_n(src.begin(), 32, dst.begin());
}

void copy16(std::array<std::byte, 16>& dst, const std::vector<std::byte>& src) {
  std::copy_n(src.begin(), 16, dst.begin());
}

}  // namespace

// ── CborEncoder ──────────────────────────────────────────────────────────────

void CborEncoder::write_head(std::uint8_t major, std::uint64_t value) {
  const std::uint8_t mt = static_cast<std::uint8_t>(major << 5U);
  if (value <= 23U) {
    buf_.push_back(static_cast<std::byte>(mt | static_cast<std::uint8_t>(value)));
  } else if (value <= 0xFFU) {
    buf_.push_back(static_cast<std::byte>(mt | 24U));
    buf_.push_back(static_cast<std::byte>(value & 0xFFU));
  } else if (value <= 0xFFFFU) {
    buf_.push_back(static_cast<std::byte>(mt | 25U));
    buf_.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    buf_.push_back(static_cast<std::byte>(value & 0xFFU));
  } else if (value <= 0xFFFFFFFFU) {
    buf_.push_back(static_cast<std::byte>(mt | 26U));
    buf_.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    buf_.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    buf_.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    buf_.push_back(static_cast<std::byte>(value & 0xFFU));
  } else {
    buf_.push_back(static_cast<std::byte>(mt | 27U));
    for (int i = 7; i >= 0; --i) {
      buf_.push_back(static_cast<std::byte>((value >> (static_cast<unsigned>(i) * 8U)) & 0xFFU));
    }
  }
}

void CborEncoder::encode_uint(std::uint64_t v) { write_head(0, v); }

void CborEncoder::encode_text(std::string_view s) {
  write_head(3, s.size());
  for (char c : s) buf_.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
}

void CborEncoder::encode_bytes(const std::uint8_t* data, std::size_t len) {
  write_head(2, len);
  for (std::size_t i = 0; i < len; ++i) buf_.push_back(static_cast<std::byte>(data[i]));
}

void CborEncoder::encode_bytes(const std::byte* data, std::size_t len) {
  write_head(2, len);
  buf_.insert(buf_.end(), data, data + len);
}

void CborEncoder::encode_bool(bool v) {
  buf_.push_back(static_cast<std::byte>(v ? 0xF5U : 0xF4U));
}

void CborEncoder::encode_null() { buf_.push_back(static_cast<std::byte>(0xF6U)); }

void CborEncoder::encode_array_header(std::uint64_t count) { write_head(4, count); }
void CborEncoder::encode_map_header(std::uint64_t count) { write_head(5, count); }

// ── CborDecoder ──────────────────────────────────────────────────────────────

std::uint8_t CborDecoder::peek_major() const {
  if (pos_ >= size_) return 0xFFU;
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(data_[pos_]) >> 5U);
}

support::Result<std::uint64_t> CborDecoder::read_head(std::uint8_t expected_major) {
  if (!has(1)) return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.eof"));
  const std::uint8_t b = static_cast<std::uint8_t>(data_[pos_]);
  const std::uint8_t major = static_cast<std::uint8_t>(b >> 5U);
  if (major != expected_major) {
    return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.major_mismatch"));
  }
  const std::uint8_t info = b & 0x1FU;
  ++pos_;
  if (info <= 23U) return support::Result<std::uint64_t>::success(info);
  if (info == 24U) {
    if (!has(1)) return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.eof"));
    const std::uint64_t v = static_cast<std::uint8_t>(data_[pos_++]);
    return support::Result<std::uint64_t>::success(v);
  }
  if (info == 25U) {
    if (!has(2)) return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.eof"));
    const std::uint64_t v = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[pos_])) << 8U) |
                            static_cast<std::uint8_t>(data_[pos_ + 1]);
    pos_ += 2;
    return support::Result<std::uint64_t>::success(v);
  }
  if (info == 26U) {
    if (!has(4)) return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.eof"));
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) v = (v << 8U) | static_cast<std::uint8_t>(data_[pos_++]);
    return support::Result<std::uint64_t>::success(v);
  }
  if (info == 27U) {
    if (!has(8)) return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.eof"));
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) v = (v << 8U) | static_cast<std::uint8_t>(data_[pos_++]);
    return support::Result<std::uint64_t>::success(v);
  }
  return support::Result<std::uint64_t>::failure(make_malformed("m2.cbor.indefinite"));
}

support::Result<std::uint64_t> CborDecoder::read_uint() { return read_head(0); }

support::Result<std::string> CborDecoder::read_text(std::size_t max_bytes) {
  auto len_result = read_head(3);
  if (!len_result.has_value()) return support::Result<std::string>::failure(len_result.error());
  const std::size_t len = static_cast<std::size_t>(*len_result.value_if());
  if (len > max_bytes) return support::Result<std::string>::failure(make_malformed("m2.cbor.text_limit"));
  if (!has(len)) return support::Result<std::string>::failure(make_malformed("m2.cbor.eof"));
  std::string s;
  s.resize(len);
  for (std::size_t i = 0; i < len; ++i) s[i] = static_cast<char>(static_cast<unsigned char>(data_[pos_++]));
  return support::Result<std::string>::success(std::move(s));
}

support::Result<std::vector<std::byte>> CborDecoder::read_bytes(std::size_t max_len) {
  auto len_result = read_head(2);
  if (!len_result.has_value()) return support::Result<std::vector<std::byte>>::failure(len_result.error());
  const std::size_t len = static_cast<std::size_t>(*len_result.value_if());
  if (len > max_len) return support::Result<std::vector<std::byte>>::failure(make_malformed("m2.cbor.bytes_limit"));
  if (!has(len)) return support::Result<std::vector<std::byte>>::failure(make_malformed("m2.cbor.eof"));
  std::vector<std::byte> v(data_ + pos_, data_ + pos_ + len);
  pos_ += len;
  return support::Result<std::vector<std::byte>>::success(std::move(v));
}

support::Result<bool> CborDecoder::read_bool() {
  if (!has(1)) return support::Result<bool>::failure(make_malformed("m2.cbor.eof"));
  const std::uint8_t b = static_cast<std::uint8_t>(data_[pos_++]);
  if (b == 0xF5U) return support::Result<bool>::success(true);
  if (b == 0xF4U) return support::Result<bool>::success(false);
  return support::Result<bool>::failure(make_malformed("m2.cbor.not_bool"));
}

support::Result<void> CborDecoder::read_null() {
  if (!has(1)) return support::Result<void>::failure(make_malformed("m2.cbor.eof"));
  if (static_cast<std::uint8_t>(data_[pos_]) != 0xF6U) {
    return support::Result<void>::failure(make_malformed("m2.cbor.not_null"));
  }
  ++pos_;
  return support::Result<void>::success();
}

support::Result<std::uint64_t> CborDecoder::read_array_header() { return read_head(4); }
support::Result<std::uint64_t> CborDecoder::read_map_header() { return read_head(5); }

support::Result<void> CborDecoder::skip() {
  if (!has(1)) return support::Result<void>::failure(make_malformed("m2.cbor.eof"));
  const std::uint8_t b = static_cast<std::uint8_t>(data_[pos_]);
  const std::uint8_t major = static_cast<std::uint8_t>(b >> 5U);
  if (major <= 1U) {
    auto r = read_head(major);
    return r.has_value() ? support::Result<void>::success() : support::Result<void>::failure(r.error());
  }
  if (major == 2U || major == 3U) {
    auto len_r = read_head(major);
    if (!len_r.has_value()) return support::Result<void>::failure(len_r.error());
    const std::size_t len = static_cast<std::size_t>(*len_r.value_if());
    if (!has(len)) return support::Result<void>::failure(make_malformed("m2.cbor.eof"));
    pos_ += len;
    return support::Result<void>::success();
  }
  if (major == 4U) {
    auto cnt_r = read_head(4);
    if (!cnt_r.has_value()) return support::Result<void>::failure(cnt_r.error());
    for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
      auto r = skip();
      if (!r.has_value()) return r;
    }
    return support::Result<void>::success();
  }
  if (major == 5U) {
    auto cnt_r = read_head(5);
    if (!cnt_r.has_value()) return support::Result<void>::failure(cnt_r.error());
    for (std::uint64_t i = 0; i < *cnt_r.value_if() * 2U; ++i) {
      auto r = skip();
      if (!r.has_value()) return r;
    }
    return support::Result<void>::success();
  }
  // Major 7 (simple/float) — simple values
  if (major == 7U) { ++pos_; return support::Result<void>::success(); }
  return support::Result<void>::failure(make_malformed("m2.cbor.unknown_major"));
}

// ── Encode helpers ───────────────────────────────────────────────────────────

namespace {

void enc_kv_text(CborEncoder& enc, std::string_view key, std::string_view val) {
  enc.encode_text(key);
  enc.encode_text(val);
}

void enc_kv_uint(CborEncoder& enc, std::string_view key, std::uint64_t val) {
  enc.encode_text(key);
  enc.encode_uint(val);
}

void enc_kv_bool(CborEncoder& enc, std::string_view key, bool val) {
  enc.encode_text(key);
  enc.encode_bool(val);
}

void enc_kv_bytes(CborEncoder& enc, std::string_view key, const std::byte* data, std::size_t len) {
  enc.encode_text(key);
  enc.encode_bytes(data, len);
}

template <std::size_t N>
void enc_kv_bytes(CborEncoder& enc, std::string_view key, const std::array<std::byte, N>& arr) {
  enc_kv_bytes(enc, key, arr.data(), N);
}

}  // namespace

// ── Encode implementations ───────────────────────────────────────────────────

std::vector<std::byte> encode_pair_hello(const PairHelloMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc_kv_bytes(enc, "peer_id", msg.peer_id);
  enc_kv_text(enc, "display_name", msg.display_name);
  enc_kv_bytes(enc, "attempt_nonce", msg.attempt_nonce);
  return enc.take();
}

std::vector<std::byte> encode_pair_confirm(const PairConfirmMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(1);
  enc_kv_bool(enc, "accepted", msg.accepted);
  return enc.take();
}

std::vector<std::byte> encode_pair_result(const PairResultMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(1);
  enc_kv_text(enc, "status", msg.status);
  return enc.take();
}

std::vector<std::byte> encode_hello(const HelloMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc_kv_bytes(enc, "peer_id", msg.peer_id);
  enc_kv_text(enc, "role", msg.role);
  enc_kv_bytes(enc, "instance_nonce", msg.instance_nonce);
  return enc.take();
}

std::vector<std::byte> encode_error(const ErrorMsg& msg) {
  const std::uint64_t count = msg.retry_after_ms.has_value() ? 3U : 2U;
  CborEncoder enc;
  enc.encode_map_header(count);
  enc_kv_text(enc, "error_code", msg.error_code);
  enc_kv_bool(enc, "retryable", msg.retryable);
  if (msg.retry_after_ms.has_value()) {
    enc_kv_uint(enc, "retry_after_ms", *msg.retry_after_ms);
  }
  return enc.take();
}

std::vector<std::byte> encode_ping(const PingMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(1);
  enc_kv_uint(enc, "monotonic_token", msg.monotonic_token);
  return enc.take();
}

std::vector<std::byte> encode_pong(const PongMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(1);
  enc_kv_uint(enc, "monotonic_token", msg.monotonic_token);
  return enc.take();
}

std::vector<std::byte> encode_session_offer(const SessionOfferMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(7);
  enc_kv_bytes(enc, "manifest_digest", msg.manifest_digest);
  enc_kv_uint(enc, "manifest_schema", msg.manifest_schema);
  enc_kv_text(enc, "chunk_profile_id", msg.chunk_profile_id);
  enc_kv_uint(enc, "file_count", msg.file_count);
  enc_kv_uint(enc, "logical_bytes", msg.logical_bytes);
  enc_kv_bool(enc, "overwrite_requested", msg.overwrite_requested);
  enc.encode_text("files");
  enc.encode_array_header(msg.files.size());
  for (const auto& f : msg.files) {
    enc.encode_map_header(4);
    enc_kv_uint(enc, "file_id", f.file_id);
    enc_kv_text(enc, "relative_path", f.relative_path);
    enc_kv_uint(enc, "logical_size", f.logical_size);
    enc_kv_bytes(enc, "file_sha256", f.file_sha256);
  }
  return enc.take();
}

std::vector<std::byte> encode_session_accept(const SessionAcceptMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc_kv_bytes(enc, "transfer_id", msg.transfer_id);
  enc.encode_text("accepted_file_ids");
  enc.encode_array_header(msg.accepted_file_ids.size());
  for (const auto id : msg.accepted_file_ids) enc.encode_uint(id);
  enc_kv_bool(enc, "overwrite_allowed", msg.overwrite_allowed);
  return enc.take();
}

std::vector<std::byte> encode_session_reject(const SessionRejectMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(1);
  enc_kv_text(enc, "reason", msg.reason);
  return enc.take();
}

std::vector<std::byte> encode_manifest_begin(const ManifestBeginMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(4);
  enc_kv_bytes(enc, "transfer_id", msg.transfer_id);
  enc_kv_bytes(enc, "manifest_digest", msg.manifest_digest);
  enc_kv_uint(enc, "total_bytes", msg.total_bytes);
  enc_kv_uint(enc, "part_count", msg.part_count);
  return enc.take();
}

std::vector<std::byte> encode_manifest_part(const ManifestPartMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc_kv_bytes(enc, "transfer_id", msg.transfer_id);
  enc_kv_uint(enc, "part_index", msg.part_index);
  enc_kv_uint(enc, "part_offset", msg.part_offset);
  return enc.take();
}

std::vector<std::byte> encode_manifest_end(const ManifestEndMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(2);
  enc_kv_bytes(enc, "transfer_id", msg.transfer_id);
  enc_kv_bytes(enc, "manifest_digest", msg.manifest_digest);
  return enc.take();
}

// encode_transfer_plan implemented below as encode_transfer_plan_impl:
static std::vector<std::byte> encode_transfer_plan_impl(const TransferPlanMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(5);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc.encode_text("session_capability");
  enc.encode_bytes(msg.session_capability.data(), 32);
  enc_kv_uint(enc, "plan_part", msg.plan_part);
  enc_kv_bool(enc, "plan_final", msg.plan_final);
  enc.encode_text("missing_chunk_hashes");
  enc.encode_array_header(msg.missing_chunk_hashes.size());
  for (const auto& h : msg.missing_chunk_hashes) enc.encode_bytes(h.data(), 32);
  return enc.take();
}

std::vector<std::byte> encode_data_bind(const DataBindMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc.encode_text("session_capability");
  enc.encode_bytes(msg.session_capability.data(), 32);
  enc.encode_text("connection_nonce");
  enc.encode_bytes(msg.connection_nonce.data(), 32);
  return enc.take();
}

std::vector<std::byte> encode_data_bind_result(const DataBindResultMsg& msg) {
  const std::uint64_t count = msg.error_code.empty() ? 1U : 2U;
  CborEncoder enc;
  enc.encode_map_header(count);
  enc_kv_bool(enc, "accepted", msg.accepted);
  if (!msg.error_code.empty()) enc_kv_text(enc, "error_code", msg.error_code);
  return enc.take();
}

std::vector<std::byte> encode_chunk_put(const ChunkPutMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(6);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc.encode_text("chunk_hash");
  enc.encode_bytes(msg.chunk_hash.data(), 32);
  enc_kv_uint(enc, "plaintext_length", msg.plaintext_length);
  enc_kv_text(enc, "codec", msg.codec);
  enc_kv_uint(enc, "encoded_length", msg.encoded_length);
  enc_kv_uint(enc, "attempt", msg.attempt);
  return enc.take();
}

std::vector<std::byte> encode_chunk_ack(const ChunkAckMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc.encode_text("chunk_hash");
  enc.encode_bytes(msg.chunk_hash.data(), 32);
  enc_kv_text(enc, "status", msg.status);
  return enc.take();
}

std::vector<std::byte> encode_chunk_nack(const ChunkNackMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(4);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc.encode_text("chunk_hash");
  enc.encode_bytes(msg.chunk_hash.data(), 32);
  enc_kv_text(enc, "error_code", msg.error_code);
  enc_kv_bool(enc, "retryable", msg.retryable);
  return enc.take();
}

std::vector<std::byte> encode_file_result(const FileResultMsg& msg) {
  const std::uint64_t count = msg.error_code.empty() ? 3U : 4U;
  CborEncoder enc;
  enc.encode_map_header(count);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc_kv_uint(enc, "file_id", msg.file_id);
  enc_kv_text(enc, "state", msg.state);
  if (!msg.error_code.empty()) enc_kv_text(enc, "error_code", msg.error_code);
  return enc.take();
}

std::vector<std::byte> encode_session_result(const SessionResultMsg& msg) {
  const std::uint64_t count = msg.error_code.empty() ? 4U : 5U;
  CborEncoder enc;
  enc.encode_map_header(count);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc_kv_text(enc, "state", msg.state);
  enc_kv_uint(enc, "files_completed", msg.files_completed);
  enc_kv_uint(enc, "files_failed", msg.files_failed);
  if (!msg.error_code.empty()) enc_kv_text(enc, "error_code", msg.error_code);
  return enc.take();
}

std::vector<std::byte> encode_cancel(const CancelMsg& msg) {
  CborEncoder enc;
  enc.encode_map_header(3);
  enc.encode_text("transfer_id");
  enc.encode_bytes(msg.transfer_id.data(), 16);
  enc_kv_text(enc, "scope", msg.scope);
  enc_kv_text(enc, "reason", msg.reason);
  return enc.take();
}

// Fix encode_transfer_plan to use the proper implementation:
std::vector<std::byte> encode_transfer_plan(const TransferPlanMsg& msg) {
  return encode_transfer_plan_impl(msg);
}

// ── Decode helpers ───────────────────────────────────────────────────────────

namespace {

support::Result<std::array<std::byte, 32>> dec_bytes32(CborDecoder& dec) {
  auto v = dec.read_bytes(32);
  if (!v.has_value()) return support::Result<std::array<std::byte, 32>>::failure(v.error());
  if (v.value_if()->size() != 32) {
    return support::Result<std::array<std::byte, 32>>::failure(make_malformed("m2.cbor.wrong_bytes32"));
  }
  std::array<std::byte, 32> arr{};
  copy32(arr, *v.value_if());
  return support::Result<std::array<std::byte, 32>>::success(arr);
}

support::Result<std::array<std::byte, 16>> dec_bytes16(CborDecoder& dec) {
  auto v = dec.read_bytes(16);
  if (!v.has_value()) return support::Result<std::array<std::byte, 16>>::failure(v.error());
  if (v.value_if()->size() != 16) {
    return support::Result<std::array<std::byte, 16>>::failure(make_malformed("m2.cbor.wrong_bytes16"));
  }
  std::array<std::byte, 16> arr{};
  copy16(arr, *v.value_if());
  return support::Result<std::array<std::byte, 16>>::success(arr);
}

}  // namespace

// ── Decode implementations ───────────────────────────────────────────────────

support::Result<PairHelloMsg> decode_pair_hello(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<PairHelloMsg>::failure(cnt_r.error());

  PairHelloMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<PairHelloMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "peer_id") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<PairHelloMsg>::failure(r.error());
      msg.peer_id = *r.value_if();
    } else if (key == "display_name") {
      auto r = dec.read_text(256);
      if (!r.has_value()) return support::Result<PairHelloMsg>::failure(r.error());
      msg.display_name = *r.value_if();
    } else if (key == "attempt_nonce") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<PairHelloMsg>::failure(r.error());
      msg.attempt_nonce = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<PairHelloMsg>::failure(r.error());
    }
  }
  return support::Result<PairHelloMsg>::success(std::move(msg));
}

support::Result<PairConfirmMsg> decode_pair_confirm(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<PairConfirmMsg>::failure(cnt_r.error());
  PairConfirmMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<PairConfirmMsg>::failure(key_r.error());
    if (*key_r.value_if() == "accepted") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<PairConfirmMsg>::failure(r.error());
      msg.accepted = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<PairConfirmMsg>::failure(r.error());
    }
  }
  return support::Result<PairConfirmMsg>::success(msg);
}

support::Result<PairResultMsg> decode_pair_result(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<PairResultMsg>::failure(cnt_r.error());
  PairResultMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<PairResultMsg>::failure(key_r.error());
    if (*key_r.value_if() == "status") {
      auto r = dec.read_text(32);
      if (!r.has_value()) return support::Result<PairResultMsg>::failure(r.error());
      msg.status = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<PairResultMsg>::failure(r.error());
    }
  }
  return support::Result<PairResultMsg>::success(msg);
}

support::Result<HelloMsg> decode_hello(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<HelloMsg>::failure(cnt_r.error());
  HelloMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<HelloMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "peer_id") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<HelloMsg>::failure(r.error());
      msg.peer_id = *r.value_if();
    } else if (key == "role") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<HelloMsg>::failure(r.error());
      msg.role = *r.value_if();
    } else if (key == "instance_nonce") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<HelloMsg>::failure(r.error());
      msg.instance_nonce = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<HelloMsg>::failure(r.error());
    }
  }
  return support::Result<HelloMsg>::success(std::move(msg));
}

support::Result<ErrorMsg> decode_error(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ErrorMsg>::failure(cnt_r.error());
  ErrorMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ErrorMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "error_code") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<ErrorMsg>::failure(r.error());
      msg.error_code = *r.value_if();
    } else if (key == "retryable") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<ErrorMsg>::failure(r.error());
      msg.retryable = *r.value_if();
    } else if (key == "retry_after_ms") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ErrorMsg>::failure(r.error());
      msg.retry_after_ms = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ErrorMsg>::failure(r.error());
    }
  }
  return support::Result<ErrorMsg>::success(std::move(msg));
}

support::Result<PingMsg> decode_ping(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<PingMsg>::failure(cnt_r.error());
  PingMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<PingMsg>::failure(key_r.error());
    if (*key_r.value_if() == "monotonic_token") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<PingMsg>::failure(r.error());
      msg.monotonic_token = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<PingMsg>::failure(r.error());
    }
  }
  return support::Result<PingMsg>::success(msg);
}

support::Result<PongMsg> decode_pong(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<PongMsg>::failure(cnt_r.error());
  PongMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<PongMsg>::failure(key_r.error());
    if (*key_r.value_if() == "monotonic_token") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<PongMsg>::failure(r.error());
      msg.monotonic_token = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<PongMsg>::failure(r.error());
    }
  }
  return support::Result<PongMsg>::success(msg);
}

support::Result<SessionOfferMsg> decode_session_offer(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<SessionOfferMsg>::failure(cnt_r.error());
  SessionOfferMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<SessionOfferMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "manifest_digest") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.manifest_digest = *r.value_if();
    } else if (key == "manifest_schema") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.manifest_schema = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "chunk_profile_id") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.chunk_profile_id = *r.value_if();
    } else if (key == "file_count") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.file_count = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "logical_bytes") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.logical_bytes = *r.value_if();
    } else if (key == "overwrite_requested") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
      msg.overwrite_requested = *r.value_if();
    } else if (key == "files") {
      auto arr_r = dec.read_array_header();
      if (!arr_r.has_value()) return support::Result<SessionOfferMsg>::failure(arr_r.error());
      for (std::uint64_t j = 0; j < *arr_r.value_if(); ++j) {
        auto f_cnt_r = dec.read_map_header();
        if (!f_cnt_r.has_value()) return support::Result<SessionOfferMsg>::failure(f_cnt_r.error());
        FileSummary fs;
        for (std::uint64_t k = 0; k < *f_cnt_r.value_if(); ++k) {
          auto fk_r = dec.read_text(64);
          if (!fk_r.has_value()) return support::Result<SessionOfferMsg>::failure(fk_r.error());
          const std::string& fk = *fk_r.value_if();
          if (fk == "file_id") {
            auto r = dec.read_uint();
            if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
            fs.file_id = static_cast<std::uint32_t>(*r.value_if());
          } else if (fk == "relative_path") {
            auto r = dec.read_text(4096);
            if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
            fs.relative_path = *r.value_if();
          } else if (fk == "logical_size") {
            auto r = dec.read_uint();
            if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
            fs.logical_size = *r.value_if();
          } else if (fk == "file_sha256") {
            auto r = dec_bytes32(dec);
            if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
            fs.file_sha256 = *r.value_if();
          } else {
            auto r = dec.skip();
            if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
          }
        }
        msg.files.push_back(fs);
      }
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<SessionOfferMsg>::failure(r.error());
    }
  }
  return support::Result<SessionOfferMsg>::success(std::move(msg));
}

support::Result<SessionAcceptMsg> decode_session_accept(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<SessionAcceptMsg>::failure(cnt_r.error());
  SessionAcceptMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<SessionAcceptMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<SessionAcceptMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "accepted_file_ids") {
      auto arr_r = dec.read_array_header();
      if (!arr_r.has_value()) return support::Result<SessionAcceptMsg>::failure(arr_r.error());
      for (std::uint64_t j = 0; j < *arr_r.value_if(); ++j) {
        auto r = dec.read_uint();
        if (!r.has_value()) return support::Result<SessionAcceptMsg>::failure(r.error());
        msg.accepted_file_ids.push_back(static_cast<std::uint32_t>(*r.value_if()));
      }
    } else if (key == "overwrite_allowed") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<SessionAcceptMsg>::failure(r.error());
      msg.overwrite_allowed = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<SessionAcceptMsg>::failure(r.error());
    }
  }
  return support::Result<SessionAcceptMsg>::success(std::move(msg));
}

support::Result<SessionRejectMsg> decode_session_reject(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<SessionRejectMsg>::failure(cnt_r.error());
  SessionRejectMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<SessionRejectMsg>::failure(key_r.error());
    if (*key_r.value_if() == "reason") {
      auto r = dec.read_text(256);
      if (!r.has_value()) return support::Result<SessionRejectMsg>::failure(r.error());
      msg.reason = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<SessionRejectMsg>::failure(r.error());
    }
  }
  return support::Result<SessionRejectMsg>::success(msg);
}

support::Result<ManifestBeginMsg> decode_manifest_begin(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ManifestBeginMsg>::failure(cnt_r.error());
  ManifestBeginMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ManifestBeginMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ManifestBeginMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "manifest_digest") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<ManifestBeginMsg>::failure(r.error());
      msg.manifest_digest = *r.value_if();
    } else if (key == "total_bytes") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ManifestBeginMsg>::failure(r.error());
      msg.total_bytes = *r.value_if();
    } else if (key == "part_count") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ManifestBeginMsg>::failure(r.error());
      msg.part_count = static_cast<std::uint32_t>(*r.value_if());
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ManifestBeginMsg>::failure(r.error());
    }
  }
  return support::Result<ManifestBeginMsg>::success(msg);
}

support::Result<ManifestPartMsg> decode_manifest_part(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ManifestPartMsg>::failure(cnt_r.error());
  ManifestPartMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ManifestPartMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ManifestPartMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "part_index") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ManifestPartMsg>::failure(r.error());
      msg.part_index = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "part_offset") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ManifestPartMsg>::failure(r.error());
      msg.part_offset = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ManifestPartMsg>::failure(r.error());
    }
  }
  return support::Result<ManifestPartMsg>::success(msg);
}

support::Result<ManifestEndMsg> decode_manifest_end(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ManifestEndMsg>::failure(cnt_r.error());
  ManifestEndMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ManifestEndMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ManifestEndMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "manifest_digest") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<ManifestEndMsg>::failure(r.error());
      msg.manifest_digest = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ManifestEndMsg>::failure(r.error());
    }
  }
  return support::Result<ManifestEndMsg>::success(msg);
}

support::Result<TransferPlanMsg> decode_transfer_plan(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<TransferPlanMsg>::failure(cnt_r.error());
  TransferPlanMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<TransferPlanMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "session_capability") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
      msg.session_capability = *r.value_if();
    } else if (key == "plan_part") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
      msg.plan_part = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "plan_final") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
      msg.plan_final = *r.value_if();
    } else if (key == "missing_chunk_hashes") {
      auto arr_r = dec.read_array_header();
      if (!arr_r.has_value()) return support::Result<TransferPlanMsg>::failure(arr_r.error());
      for (std::uint64_t j = 0; j < *arr_r.value_if(); ++j) {
        auto r = dec_bytes32(dec);
        if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
        msg.missing_chunk_hashes.push_back(*r.value_if());
      }
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<TransferPlanMsg>::failure(r.error());
    }
  }
  return support::Result<TransferPlanMsg>::success(std::move(msg));
}

support::Result<DataBindMsg> decode_data_bind(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<DataBindMsg>::failure(cnt_r.error());
  DataBindMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<DataBindMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<DataBindMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "session_capability") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<DataBindMsg>::failure(r.error());
      msg.session_capability = *r.value_if();
    } else if (key == "connection_nonce") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<DataBindMsg>::failure(r.error());
      msg.connection_nonce = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<DataBindMsg>::failure(r.error());
    }
  }
  return support::Result<DataBindMsg>::success(msg);
}

support::Result<DataBindResultMsg> decode_data_bind_result(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<DataBindResultMsg>::failure(cnt_r.error());
  DataBindResultMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<DataBindResultMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "accepted") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<DataBindResultMsg>::failure(r.error());
      msg.accepted = *r.value_if();
    } else if (key == "error_code") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<DataBindResultMsg>::failure(r.error());
      msg.error_code = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<DataBindResultMsg>::failure(r.error());
    }
  }
  return support::Result<DataBindResultMsg>::success(msg);
}

support::Result<ChunkPutMsg> decode_chunk_put(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ChunkPutMsg>::failure(cnt_r.error());
  ChunkPutMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ChunkPutMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "chunk_hash") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.chunk_hash = *r.value_if();
    } else if (key == "plaintext_length") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.plaintext_length = *r.value_if();
    } else if (key == "codec") {
      auto r = dec.read_text(16);
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.codec = *r.value_if();
    } else if (key == "encoded_length") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.encoded_length = *r.value_if();
    } else if (key == "attempt") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
      msg.attempt = static_cast<std::uint32_t>(*r.value_if());
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ChunkPutMsg>::failure(r.error());
    }
  }
  return support::Result<ChunkPutMsg>::success(std::move(msg));
}

support::Result<ChunkAckMsg> decode_chunk_ack(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ChunkAckMsg>::failure(cnt_r.error());
  ChunkAckMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ChunkAckMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ChunkAckMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "chunk_hash") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<ChunkAckMsg>::failure(r.error());
      msg.chunk_hash = *r.value_if();
    } else if (key == "status") {
      auto r = dec.read_text(32);
      if (!r.has_value()) return support::Result<ChunkAckMsg>::failure(r.error());
      msg.status = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ChunkAckMsg>::failure(r.error());
    }
  }
  return support::Result<ChunkAckMsg>::success(std::move(msg));
}

support::Result<ChunkNackMsg> decode_chunk_nack(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<ChunkNackMsg>::failure(cnt_r.error());
  ChunkNackMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<ChunkNackMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<ChunkNackMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "chunk_hash") {
      auto r = dec_bytes32(dec);
      if (!r.has_value()) return support::Result<ChunkNackMsg>::failure(r.error());
      msg.chunk_hash = *r.value_if();
    } else if (key == "error_code") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<ChunkNackMsg>::failure(r.error());
      msg.error_code = *r.value_if();
    } else if (key == "retryable") {
      auto r = dec.read_bool();
      if (!r.has_value()) return support::Result<ChunkNackMsg>::failure(r.error());
      msg.retryable = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<ChunkNackMsg>::failure(r.error());
    }
  }
  return support::Result<ChunkNackMsg>::success(std::move(msg));
}

support::Result<FileResultMsg> decode_file_result(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<FileResultMsg>::failure(cnt_r.error());
  FileResultMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<FileResultMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<FileResultMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "file_id") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<FileResultMsg>::failure(r.error());
      msg.file_id = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "state") {
      auto r = dec.read_text(32);
      if (!r.has_value()) return support::Result<FileResultMsg>::failure(r.error());
      msg.state = *r.value_if();
    } else if (key == "error_code") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<FileResultMsg>::failure(r.error());
      msg.error_code = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<FileResultMsg>::failure(r.error());
    }
  }
  return support::Result<FileResultMsg>::success(std::move(msg));
}

support::Result<SessionResultMsg> decode_session_result(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<SessionResultMsg>::failure(cnt_r.error());
  SessionResultMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<SessionResultMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "state") {
      auto r = dec.read_text(32);
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
      msg.state = *r.value_if();
    } else if (key == "files_completed") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
      msg.files_completed = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "files_failed") {
      auto r = dec.read_uint();
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
      msg.files_failed = static_cast<std::uint32_t>(*r.value_if());
    } else if (key == "error_code") {
      auto r = dec.read_text(64);
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
      msg.error_code = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<SessionResultMsg>::failure(r.error());
    }
  }
  return support::Result<SessionResultMsg>::success(std::move(msg));
}

support::Result<CancelMsg> decode_cancel(const std::vector<std::byte>& data) {
  CborDecoder dec{data};
  auto cnt_r = dec.read_map_header();
  if (!cnt_r.has_value()) return support::Result<CancelMsg>::failure(cnt_r.error());
  CancelMsg msg;
  for (std::uint64_t i = 0; i < *cnt_r.value_if(); ++i) {
    auto key_r = dec.read_text(64);
    if (!key_r.has_value()) return support::Result<CancelMsg>::failure(key_r.error());
    const std::string& key = *key_r.value_if();
    if (key == "transfer_id") {
      auto r = dec_bytes16(dec);
      if (!r.has_value()) return support::Result<CancelMsg>::failure(r.error());
      msg.transfer_id = *r.value_if();
    } else if (key == "scope") {
      auto r = dec.read_text(16);
      if (!r.has_value()) return support::Result<CancelMsg>::failure(r.error());
      msg.scope = *r.value_if();
    } else if (key == "reason") {
      auto r = dec.read_text(256);
      if (!r.has_value()) return support::Result<CancelMsg>::failure(r.error());
      msg.reason = *r.value_if();
    } else {
      auto r = dec.skip();
      if (!r.has_value()) return support::Result<CancelMsg>::failure(r.error());
    }
  }
  return support::Result<CancelMsg>::success(std::move(msg));
}

}  // namespace spoink::protocol
