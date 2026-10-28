#include "spoink/support/checked_math.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

namespace spoink::support {

TEST_CASE("checked arithmetic returns values within unsigned 64-bit bounds") {
  const auto sum = checked_add(40, 2);
  const auto difference = checked_subtract(42, 2);
  const auto product = checked_multiply(6, 7);

  REQUIRE(sum.has_value());
  REQUIRE(*sum.value_if() == 42);
  REQUIRE(difference.has_value());
  REQUIRE(*difference.value_if() == 40);
  REQUIRE(product.has_value());
  REQUIRE(*product.value_if() == 42);
}

TEST_CASE("checked arithmetic rejects overflow and underflow") {
  const auto overflow = checked_add(std::numeric_limits<std::uint64_t>::max(), 1);
  const auto multiplication_overflow = checked_multiply(std::numeric_limits<std::uint64_t>::max(), 2);
  const auto underflow = checked_subtract(0, 1);

  REQUIRE_FALSE(overflow.has_value());
  REQUIRE(overflow.error().code == ErrorCode::kArithmeticOverflow);
  REQUIRE_FALSE(multiplication_overflow.has_value());
  REQUIRE(multiplication_overflow.error().code == ErrorCode::kArithmeticOverflow);
  REQUIRE_FALSE(underflow.has_value());
  REQUIRE(underflow.error().code == ErrorCode::kArithmeticUnderflow);
}

TEST_CASE("checked conversions reject values unavailable to target types") {
  const auto int64_result = checked_to_int64(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U);

  REQUIRE_FALSE(int64_result.has_value());
  REQUIRE(int64_result.error().code == ErrorCode::kConversionOutOfRange);
}

}  // namespace spoink::support
