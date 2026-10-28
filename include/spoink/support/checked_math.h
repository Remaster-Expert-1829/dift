#pragma once

#include <cstddef>
#include <cstdint>

#include "spoink/support/result.h"

namespace spoink::support {

[[nodiscard]] Result<std::uint64_t> checked_add(std::uint64_t left, std::uint64_t right);
[[nodiscard]] Result<std::uint64_t> checked_subtract(std::uint64_t left, std::uint64_t right);
[[nodiscard]] Result<std::uint64_t> checked_multiply(std::uint64_t left, std::uint64_t right);
[[nodiscard]] Result<std::size_t> checked_to_size(std::uint64_t value);
[[nodiscard]] Result<std::int64_t> checked_to_int64(std::uint64_t value);

}  // namespace spoink::support
