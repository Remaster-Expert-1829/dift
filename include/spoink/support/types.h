#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "spoink/support/result.h"

namespace spoink::support {

template <typename Tag, std::size_t ByteCount>
class Identifier {
 public:
  static constexpr std::size_t kByteCount = ByteCount;

  explicit Identifier(std::array<std::byte, kByteCount> bytes) : bytes_(bytes) {}
  [[nodiscard]] const std::array<std::byte, kByteCount>& bytes() const noexcept { return bytes_; }
  [[nodiscard]] bool operator==(const Identifier& other) const noexcept { return bytes_ == other.bytes_; }
  [[nodiscard]] bool operator!=(const Identifier& other) const noexcept { return !(*this == other); }

 private:
  std::array<std::byte, kByteCount> bytes_;
};

struct PeerIdTag {};
struct TransferIdTag {};
struct RequestIdTag {};
using PeerId = Identifier<PeerIdTag, 32>;
using TransferId = Identifier<TransferIdTag, 16>;

class RequestId {
 public:
  explicit constexpr RequestId(std::uint64_t value) : value_(value) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

 private:
  std::uint64_t value_;
};

class ByteLength {
 public:
  explicit constexpr ByteLength(std::uint64_t value) : value_(value) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

 private:
  std::uint64_t value_;
};

class ByteOffset {
 public:
  explicit constexpr ByteOffset(std::uint64_t value) : value_(value) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

 private:
  std::uint64_t value_;
};

class Sha256Digest {
 public:
  static constexpr std::size_t kByteCount = 32;

  explicit Sha256Digest(std::array<std::byte, kByteCount> bytes) : bytes_(bytes) {}
  [[nodiscard]] const std::array<std::byte, kByteCount>& bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::string hex() const;
  [[nodiscard]] static Result<Sha256Digest> from_hex(std::string_view encoded);
  [[nodiscard]] bool operator==(const Sha256Digest& other) const noexcept { return bytes_ == other.bytes_; }
  [[nodiscard]] bool operator!=(const Sha256Digest& other) const noexcept { return !(*this == other); }

 private:
  std::array<std::byte, kByteCount> bytes_;
};

}  // namespace spoink::support
