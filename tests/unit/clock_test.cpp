#include "spoink/support/clock.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

namespace spoink::support {

TEST_CASE("FakeClock advances deterministically without reading wall time") {
  FakeClock clock;
  const auto initial = clock.now();

  clock.advance(std::chrono::milliseconds(125));

  REQUIRE(clock.now() - initial == std::chrono::milliseconds(125));
}

}  // namespace spoink::support
