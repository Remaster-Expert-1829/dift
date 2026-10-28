#pragma once

#include <cstdint>
#include <string>

namespace spoink::support {

enum class ErrorCode : std::uint8_t {
  kInvalidArgument,
  kArithmeticOverflow,
  kArithmeticUnderflow,
  kConversionOutOfRange,
  kResourceFailure,
  kCancelled,
  kInternal,
};

enum class ErrorCategory : std::uint8_t {
  kValidation,
  kResource,
  kOperation,
  kInternal,
};

enum class RetryClass : std::uint8_t {
  kNever,
  kAfterBackoff,
  kAfterOperatorAction,
};

enum class ErrorScope : std::uint8_t {
  kProcess,
  kOperation,
  kTransfer,
};

struct Error {
  ErrorCode code;
  ErrorCategory category;
  RetryClass retry_class;
  std::string message_key;
  std::string correlation_id;
  ErrorScope scope;
  bool resumable;
  std::string diagnostic;
};

Error make_error(ErrorCode code, std::string message_key, std::string diagnostic = {});

}  // namespace spoink::support
