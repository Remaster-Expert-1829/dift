#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "spoink/support/result.h"

namespace spoink::protocol {

// ── Compiled ceilings (protocol-major-1 contract) ───────────────────────────

struct Limits {
  static constexpr std::uint32_t kMaxHeaderBytes = 8192U;
  static constexpr std::uint64_t kMaxPayloadBytes = 131072U;
  static constexpr std::uint64_t kMaxManifestBytes = 16777216U;
  static constexpr std::uint32_t kMaxPathBytes = 4096U;
  static constexpr std::uint32_t kMaxFilesPerSession = 10000U;
  static constexpr std::uint32_t kMaxChunksPerSession = 1000000U;
  static constexpr std::uint32_t kMaxDataConnections = 8U;
  static constexpr std::uint32_t kMaxConcurrentSessions = 4U;
  static constexpr std::uint32_t kMaxConcurrentHandshakes = 8U;
  static constexpr std::uint32_t kHandshakeTimeoutMs = 10000U;
  static constexpr std::uint32_t kHandshakeTimeoutMaxMs = 30000U;
  static constexpr std::uint32_t kIdleTimeoutMs = 60000U;
  static constexpr std::uint32_t kIdleTimeoutMaxMs = 300000U;
  static constexpr std::uint32_t kOperationTimeoutMs = 60000U;
  static constexpr std::uint32_t kOperationTimeoutMaxMs = 300000U;
  static constexpr std::uint32_t kPairingTimeoutMs = 60000U;
  static constexpr std::uint32_t kPairingTimeoutMaxMs = 120000U;
};

// ── Channel and message type constants ──────────────────────────────────────

enum class Channel : std::uint8_t {
  kPairing = 0,
  kControl = 1,
  kData = 2,
};

enum class MessageType : std::uint8_t {
  kHello = 0x01,
  kError = 0x02,
  kPing = 0x03,
  kPong = 0x04,
  kPairHello = 0x10,
  kPairConfirm = 0x11,
  kPairResult = 0x12,
  kSessionOffer = 0x20,
  kSessionAccept = 0x21,
  kSessionReject = 0x22,
  kManifestBegin = 0x23,
  kManifestPart = 0x24,
  kManifestEnd = 0x25,
  kTransferPlan = 0x26,
  kResumeRequest = 0x27,
  kResumeAccept = 0x28,
  kResumeReject = 0x29,
  kCancel = 0x2a,
  kBackpressure = 0x2b,
  kFileResult = 0x2c,
  kSessionResult = 0x2d,
  kDataBind = 0x40,
  kDataBindResult = 0x41,
  kChunkPut = 0x42,
  kChunkAck = 0x43,
  kChunkNack = 0x44,
};

static constexpr std::uint16_t kFlagOptional = 0x0001U;

// ── 32-byte fixed frame prefix ───────────────────────────────────────────────

static constexpr std::size_t kFramePrefixBytes = 32;

struct FramePrefix {
  Channel channel{Channel::kControl};
  MessageType message_type{MessageType::kHello};
  std::uint8_t major{1};
  std::uint8_t minor{0};
  std::uint16_t flags{0};
  std::uint32_t header_length{0};
  std::uint64_t payload_length{0};
  std::uint64_t request_id{0};
};

// Encode prefix to 32-byte wire representation (big-endian).
[[nodiscard]] std::array<std::byte, kFramePrefixBytes> encode_prefix(const FramePrefix& prefix);

// Decode 32-byte wire representation. Validates magic, reserved, and version.
[[nodiscard]] support::Result<FramePrefix> decode_prefix(const std::array<std::byte, kFramePrefixBytes>& raw);

// Validate lengths against absolute ceilings (before allocation).
[[nodiscard]] support::Result<void> validate_prefix_lengths(const FramePrefix& prefix);

// ── Partial-read state machine ───────────────────────────────────────────────

// Accumulates bytes from a stream until a complete frame is available.
// Callers feed bytes from a socket; call next() after each feed.
class FrameReader {
 public:
  FrameReader() = default;

  enum class State { kNeedPrefix, kNeedHeader, kNeedPayload, kComplete };

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool complete() const noexcept { return state_ == State::kComplete; }

  // Feed bytes into the reader. Returns the number of bytes consumed.
  std::size_t feed(const std::byte* data, std::size_t size);

  // When state() == kComplete, retrieve the parsed frame.
  [[nodiscard]] const FramePrefix& prefix() const noexcept { return prefix_; }
  [[nodiscard]] const std::vector<std::byte>& header_bytes() const noexcept { return header_; }
  [[nodiscard]] const std::vector<std::byte>& payload_bytes() const noexcept { return payload_; }

  // Reset to accept the next frame.
  void reset();

 private:
  State state_{State::kNeedPrefix};
  std::array<std::byte, kFramePrefixBytes> prefix_buf_{};
  std::size_t prefix_filled_{0};
  FramePrefix prefix_{};
  std::vector<std::byte> header_;
  std::vector<std::byte> payload_;
};

}  // namespace spoink::protocol
