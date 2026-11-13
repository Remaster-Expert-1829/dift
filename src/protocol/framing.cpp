#include "spoink/protocol/framing.h"

#include <algorithm>
#include <cstring>

#include "spoink/support/error.h"

namespace spoink::protocol {

namespace {

static constexpr std::array<std::byte, 4> kMagic{std::byte{0x53}, std::byte{0x50},
                                                   std::byte{0x4E}, std::byte{0x4B}};

void write_be16(std::byte* dst, std::uint16_t v) {
  dst[0] = static_cast<std::byte>((v >> 8U) & 0xFFU);
  dst[1] = static_cast<std::byte>(v & 0xFFU);
}

void write_be32(std::byte* dst, std::uint32_t v) {
  dst[0] = static_cast<std::byte>((v >> 24U) & 0xFFU);
  dst[1] = static_cast<std::byte>((v >> 16U) & 0xFFU);
  dst[2] = static_cast<std::byte>((v >> 8U) & 0xFFU);
  dst[3] = static_cast<std::byte>(v & 0xFFU);
}

void write_be64(std::byte* dst, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    dst[7 - static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (static_cast<unsigned>(i) * 8U)) & 0xFFU);
  }
}

std::uint16_t read_be16(const std::byte* src) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(src[0]) << 8U) |
                                    static_cast<std::uint16_t>(src[1]));
}

std::uint32_t read_be32(const std::byte* src) {
  return (static_cast<std::uint32_t>(src[0]) << 24U) | (static_cast<std::uint32_t>(src[1]) << 16U) |
         (static_cast<std::uint32_t>(src[2]) << 8U) | static_cast<std::uint32_t>(src[3]);
}

std::uint64_t read_be64(const std::byte* src) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    v = (v << 8U) | static_cast<std::uint64_t>(src[i]);
  }
  return v;
}

support::Error make_malformed(const char* key) {
  return support::make_error(support::ErrorCode::kInvalidArgument, key);
}

}  // namespace

std::array<std::byte, kFramePrefixBytes> encode_prefix(const FramePrefix& prefix) {
  std::array<std::byte, kFramePrefixBytes> buf{};
  // magic
  buf[0] = kMagic[0];
  buf[1] = kMagic[1];
  buf[2] = kMagic[2];
  buf[3] = kMagic[3];
  // major/minor/channel/message_type
  buf[4] = static_cast<std::byte>(prefix.major);
  buf[5] = static_cast<std::byte>(prefix.minor);
  buf[6] = static_cast<std::byte>(prefix.channel);
  buf[7] = static_cast<std::byte>(prefix.message_type);
  // flags (2 bytes BE)
  write_be16(buf.data() + 8, prefix.flags);
  // reserved (2 bytes) — always zero
  buf[10] = std::byte{0};
  buf[11] = std::byte{0};
  // header_length (4 bytes BE)
  write_be32(buf.data() + 12, prefix.header_length);
  // payload_length (8 bytes BE)
  write_be64(buf.data() + 16, prefix.payload_length);
  // request_id (8 bytes BE)
  write_be64(buf.data() + 24, prefix.request_id);
  return buf;
}

support::Result<FramePrefix> decode_prefix(const std::array<std::byte, kFramePrefixBytes>& raw) {
  // Check magic
  if (raw[0] != kMagic[0] || raw[1] != kMagic[1] || raw[2] != kMagic[2] || raw[3] != kMagic[3]) {
    return support::Result<FramePrefix>::failure(make_malformed("m2.frame.bad_magic"));
  }

  const std::uint8_t major = static_cast<std::uint8_t>(raw[4]);
  if (major != 1U) {
    return support::Result<FramePrefix>::failure(make_malformed("m2.frame.major_mismatch"));
  }

  // Reserved bytes 10-11 must be zero in major 1
  if (raw[10] != std::byte{0} || raw[11] != std::byte{0}) {
    return support::Result<FramePrefix>::failure(make_malformed("m2.frame.reserved_nonzero"));
  }

  FramePrefix prefix;
  prefix.major = major;
  prefix.minor = static_cast<std::uint8_t>(raw[5]);
  prefix.channel = static_cast<Channel>(static_cast<std::uint8_t>(raw[6]));
  prefix.message_type = static_cast<MessageType>(static_cast<std::uint8_t>(raw[7]));
  prefix.flags = read_be16(raw.data() + 8);
  prefix.header_length = read_be32(raw.data() + 12);
  prefix.payload_length = read_be64(raw.data() + 16);
  prefix.request_id = read_be64(raw.data() + 24);

  return support::Result<FramePrefix>::success(prefix);
}

support::Result<void> validate_prefix_lengths(const FramePrefix& prefix) {
  if (prefix.header_length > Limits::kMaxHeaderBytes) {
    return support::Result<void>::failure(make_malformed("m2.frame.header_limit"));
  }
  if (prefix.payload_length > Limits::kMaxPayloadBytes) {
    return support::Result<void>::failure(make_malformed("m2.frame.payload_limit"));
  }
  return support::Result<void>::success();
}

// ── FrameReader ──────────────────────────────────────────────────────────────

std::size_t FrameReader::feed(const std::byte* data, std::size_t size) {
  std::size_t consumed = 0;

  while (consumed < size && state_ != State::kComplete) {
    switch (state_) {
      case State::kNeedPrefix: {
        const std::size_t need = kFramePrefixBytes - prefix_filled_;
        const std::size_t take = std::min(need, size - consumed);
        std::memcpy(prefix_buf_.data() + prefix_filled_, data + consumed, take);
        prefix_filled_ += take;
        consumed += take;
        if (prefix_filled_ == kFramePrefixBytes) {
          auto result = decode_prefix(prefix_buf_);
          if (!result.has_value()) {
            // malformed prefix: mark error by staying in kNeedPrefix forever
            prefix_filled_ = 0;
            return consumed;
          }
          prefix_ = *result.value_if();
          auto len_result = validate_prefix_lengths(prefix_);
          if (!len_result.has_value()) {
            prefix_filled_ = 0;
            return consumed;
          }
          header_.clear();
          header_.reserve(prefix_.header_length);
          payload_.clear();
          if (prefix_.payload_length > 0) {
            payload_.reserve(static_cast<std::size_t>(prefix_.payload_length));
          }
          state_ = (prefix_.header_length > 0) ? State::kNeedHeader : State::kNeedPayload;
          if (state_ == State::kNeedPayload && prefix_.payload_length == 0) {
            state_ = State::kComplete;
          }
        }
        break;
      }
      case State::kNeedHeader: {
        const std::size_t need = static_cast<std::size_t>(prefix_.header_length) - header_.size();
        const std::size_t take = std::min(need, size - consumed);
        header_.insert(header_.end(), data + consumed, data + consumed + take);
        consumed += take;
        if (header_.size() == prefix_.header_length) {
          state_ = (prefix_.payload_length > 0) ? State::kNeedPayload : State::kComplete;
        }
        break;
      }
      case State::kNeedPayload: {
        const std::size_t need = static_cast<std::size_t>(prefix_.payload_length) - payload_.size();
        const std::size_t take = std::min(need, size - consumed);
        payload_.insert(payload_.end(), data + consumed, data + consumed + take);
        consumed += take;
        if (payload_.size() == static_cast<std::size_t>(prefix_.payload_length)) {
          state_ = State::kComplete;
        }
        break;
      }
      case State::kComplete:
        break;
    }
  }
  return consumed;
}

void FrameReader::reset() {
  state_ = State::kNeedPrefix;
  prefix_filled_ = 0;
  prefix_ = FramePrefix{};
  header_.clear();
  payload_.clear();
}

}  // namespace spoink::protocol
