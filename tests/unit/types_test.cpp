#include "spoink/support/types.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <type_traits>

namespace spoink::support {

TEST_CASE("SHA-256 digest requires exactly 32 bytes encoded as hexadecimal") {
  const std::array<std::byte, Sha256Digest::kByteCount> bytes{};
  const Sha256Digest digest(bytes);

  REQUIRE(digest.hex() == std::string(64, '0'));
  const auto decoded = Sha256Digest::from_hex(digest.hex());
  REQUIRE(decoded.has_value());
  REQUIRE(*decoded.value_if() == digest);
  REQUIRE_FALSE(Sha256Digest::from_hex("abc").has_value());
  REQUIRE_FALSE(Sha256Digest::from_hex(std::string(64, 'g')).has_value());
}

TEST_CASE("approved identifier representations are strong and non-interconvertible") {
  static_assert(PeerId::kByteCount == 32, "PeerId is SHA-256");
  static_assert(TransferId::kByteCount == 16, "TransferId is 16 bytes");
  static_assert(std::is_same<decltype(RequestId{0}.value()), std::uint64_t>::value, "RequestId is uint64");
  static_assert(!std::is_constructible<PeerId, std::array<std::byte, 16>>::value, "no truncation");
  static_assert(!std::is_convertible<PeerId, TransferId>::value, "strong IDs");
  const std::array<std::byte, 32> bytes{};
  const std::array<std::byte, 16> transfer_bytes{};
  const PeerId peer(bytes);
  const PeerId same_peer(bytes);
  const TransferId transfer(transfer_bytes);
  const RequestId request(42U);
  const ByteLength length(17);
  const ByteOffset offset(23);

  REQUIRE(peer == same_peer);
  REQUIRE(peer.bytes().size() == 32);
  REQUIRE(transfer.bytes().size() == 16);
  REQUIRE(request.value() == 42U);
  REQUIRE(length.value() == 17);
  REQUIRE(offset.value() == 23);
}

}  // namespace spoink::support
