#include <catch2/catch_test_macros.hpp>

#include "spoink/support/result.h"

namespace spoink::support {

TEST_CASE("Result preserves successful values") {
  const auto result = Result<int>::success(42);

  REQUIRE(result.has_value());
  REQUIRE(*result.value_if() == 42);
}

TEST_CASE("Result preserves structured failures without exposing diagnostics as a message key") {
  const auto result = Result<void>::failure(
      make_error(ErrorCode::kResourceFailure, "support.resource_failure", "open returned EMFILE"));

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == ErrorCode::kResourceFailure);
  REQUIRE(result.error().category == ErrorCategory::kResource);
  REQUIRE(result.error().retry_class == RetryClass::kAfterBackoff);
  REQUIRE(result.error().scope == ErrorScope::kOperation);
  REQUIRE(result.error().resumable);
  REQUIRE(result.error().message_key == "support.resource_failure");
  REQUIRE(result.error().diagnostic == "open returned EMFILE");
}

}  // namespace spoink::support
