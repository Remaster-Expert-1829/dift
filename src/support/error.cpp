#include "spoink/support/error.h"

#include <utility>

namespace spoink::support {

namespace {

struct ErrorDefaults {
  ErrorCategory category;
  RetryClass retry_class;
  ErrorScope scope;
  bool resumable;
};

ErrorDefaults defaults_for(ErrorCode code) {
  switch (code) {
    case ErrorCode::kInvalidArgument:
    case ErrorCode::kArithmeticOverflow:
    case ErrorCode::kArithmeticUnderflow:
    case ErrorCode::kConversionOutOfRange:
      return {ErrorCategory::kValidation, RetryClass::kNever, ErrorScope::kOperation, false};
    case ErrorCode::kResourceFailure:
      return {ErrorCategory::kResource, RetryClass::kAfterBackoff, ErrorScope::kOperation, true};
    case ErrorCode::kCancelled:
      return {ErrorCategory::kOperation, RetryClass::kAfterOperatorAction, ErrorScope::kOperation, true};
    case ErrorCode::kInternal:
      return {ErrorCategory::kInternal, RetryClass::kNever, ErrorScope::kProcess, false};
  }
  return {ErrorCategory::kInternal, RetryClass::kNever, ErrorScope::kProcess, false};
}

}  // namespace

Error make_error(ErrorCode code, std::string message_key, std::string diagnostic) {
  const ErrorDefaults defaults = defaults_for(code);
  return {code, defaults.category, defaults.retry_class, std::move(message_key),
          {},   defaults.scope,    defaults.resumable,   std::move(diagnostic)};
}

}  // namespace spoink::support
