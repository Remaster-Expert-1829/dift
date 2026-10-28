#include "spoink/support/types.h"

#include <array>

namespace spoink::support {

namespace {

constexpr std::array<char, 16> kHexDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

int nibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

}  // namespace

std::string Sha256Digest::hex() const {
  std::string encoded;
  encoded.reserve(kByteCount * 2);
  for (const std::byte value : bytes_) {
    const auto byte = static_cast<unsigned int>(value);
    encoded.push_back(kHexDigits[(byte >> 4U) & 0x0fU]);
    encoded.push_back(kHexDigits[byte & 0x0fU]);
  }
  return encoded;
}

Result<Sha256Digest> Sha256Digest::from_hex(std::string_view encoded) {
  if (encoded.size() != kByteCount * 2) {
    return Result<Sha256Digest>::failure(make_error(ErrorCode::kInvalidArgument, "support.sha256_length"));
  }

  std::array<std::byte, kByteCount> decoded{};
  for (std::size_t index = 0; index < kByteCount; ++index) {
    const int high = nibble(encoded[index * 2]);
    const int low = nibble(encoded[index * 2 + 1]);
    if (high < 0 || low < 0) {
      return Result<Sha256Digest>::failure(make_error(ErrorCode::kInvalidArgument, "support.sha256_encoding"));
    }
    const auto combined = static_cast<unsigned int>((high << 4) | low);
    decoded[index] = static_cast<std::byte>(combined);
  }
  return Result<Sha256Digest>::success(Sha256Digest(decoded));
}

}  // namespace spoink::support
