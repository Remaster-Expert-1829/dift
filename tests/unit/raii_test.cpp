#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <cerrno>

#include "spoink/support/scoped_resource.h"

namespace spoink::support {

TEST_CASE("FileDescriptor closes exactly once and leaves moved-from values inert") {
  int descriptors[2] = {-1, -1};
  REQUIRE(::pipe(descriptors) == 0);
  const int read_descriptor = descriptors[0];
  const int write_descriptor = descriptors[1];

  {
    FileDescriptor source(read_descriptor);
    FileDescriptor destination(std::move(source));
    // NOLINTNEXTLINE(bugprone-use-after-move): owner=M0 Builder; reason=test; scope=assertion; removal=tidy move model.
    REQUIRE_FALSE(source.valid());
    REQUIRE(destination.valid());
    REQUIRE(destination.get() == read_descriptor);
  }

  errno = 0;
  REQUIRE(::close(read_descriptor) == -1);
  REQUIRE(errno == EBADF);
  REQUIRE(::close(write_descriptor) == 0);
}

TEST_CASE("ScopedCleanup runs once unless ownership is explicitly released") {
  int cleanup_count = 0;
  {
    auto cleanup = make_scoped_cleanup([&cleanup_count]() noexcept { ++cleanup_count; });
    cleanup.reset();
    cleanup.reset();
  }
  REQUIRE(cleanup_count == 1);

  {
    auto cleanup = make_scoped_cleanup([&cleanup_count]() noexcept { ++cleanup_count; });
    REQUIRE(cleanup.release());
  }
  REQUIRE(cleanup_count == 1);
}

}  // namespace spoink::support
