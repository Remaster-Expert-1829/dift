#include "spoink/support/checked_math.h"

#include <limits>

namespace spoink::support {

Result<std::uint64_t> checked_add(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return Result<std::uint64_t>::failure(make_error(ErrorCode::kArithmeticOverflow, "support.arithmetic_overflow"));
  }
  return Result<std::uint64_t>::success(left + right);
}

Result<std::uint64_t> checked_subtract(std::uint64_t left, std::uint64_t right) {
  if (right > left) {
    return Result<std::uint64_t>::failure(make_error(ErrorCode::kArithmeticUnderflow, "support.arithmetic_underflow"));
  }
  return Result<std::uint64_t>::success(left - right);
}

Result<std::uint64_t> checked_multiply(std::uint64_t left, std::uint64_t right) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return Result<std::uint64_t>::failure(make_error(ErrorCode::kArithmeticOverflow, "support.arithmetic_overflow"));
  }
  return Result<std::uint64_t>::success(left * right);
}

Result<std::size_t> checked_to_size(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Result<std::size_t>::failure(make_error(ErrorCode::kConversionOutOfRange, "support.size_out_of_range"));
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(value));
}

Result<std::int64_t> checked_to_int64(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return Result<std::int64_t>::failure(make_error(ErrorCode::kConversionOutOfRange, "support.int64_out_of_range"));
  }
  return Result<std::int64_t>::success(static_cast<std::int64_t>(value));
}

}  // namespace spoink::support
