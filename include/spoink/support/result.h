#pragma once

#include <cassert>
#include <optional>
#include <utility>

#include "spoink/support/error.h"

namespace spoink::support {

template <typename T>
class [[nodiscard]] Result {
 public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(Error error) { return Result(std::move(error)); }

  [[nodiscard]] bool has_value() const noexcept { return value_.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const T* value_if() const noexcept { return value_ ? &*value_ : nullptr; }
  [[nodiscard]] T* value_if() noexcept { return value_ ? &*value_ : nullptr; }
  [[nodiscard]] const Error& error() const noexcept {
    assert(error_.has_value());
    return *error_;  // NOLINT(bugprone-unchecked-optional-access): owner=M0 Builder; reason=asserted failure-only
                     // precondition; scope=Result<T>::error(); removal=remove when clang-tidy recognizes the preceding
                     // assertion.
  }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(Error error) : error_(std::move(error)) {}

  std::optional<T> value_;
  std::optional<Error> error_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  static Result success() { return Result(true); }
  static Result failure(Error error) { return Result(std::move(error)); }

  [[nodiscard]] bool has_value() const noexcept { return success_; }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const Error& error() const noexcept {
    assert(error_.has_value());
    return *error_;  // NOLINT(bugprone-unchecked-optional-access): owner=M0 Builder; reason=asserted failure-only
                     // precondition; scope=Result<void>::error(); removal=remove when clang-tidy recognizes the
                     // preceding assertion.
  }

 private:
  explicit Result(bool success) : success_(success) {}
  explicit Result(Error error) : success_(false), error_(std::move(error)) {}

  bool success_ = false;
  std::optional<Error> error_;
};

}  // namespace spoink::support
