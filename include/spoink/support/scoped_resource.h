#pragma once

#include <unistd.h>

#include <utility>

namespace spoink::support {

class FileDescriptor {
 public:
  explicit FileDescriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {}
  ~FileDescriptor() { reset(); }

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  FileDescriptor(FileDescriptor&& other) noexcept : descriptor_(other.release()) {}
  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] bool valid() const noexcept { return descriptor_ >= 0; }
  [[nodiscard]] int release() noexcept { return std::exchange(descriptor_, -1); }
  void reset(int descriptor = -1) noexcept {
    if (valid()) {
      static_cast<void>(::close(descriptor_));
    }
    descriptor_ = descriptor;
  }

 private:
  int descriptor_;
};

template <typename Cleanup>
class ScopedCleanup {
 public:
  explicit ScopedCleanup(Cleanup cleanup) : cleanup_(std::move(cleanup)) {}
  ~ScopedCleanup() { reset(); }

  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;

  ScopedCleanup(ScopedCleanup&& other) noexcept : cleanup_(std::move(other.cleanup_)), active_(other.release()) {}
  ScopedCleanup& operator=(ScopedCleanup&& other) noexcept {
    if (this != &other) {
      reset();
      cleanup_ = std::move(other.cleanup_);
      active_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] bool release() noexcept { return std::exchange(active_, false); }
  void reset() noexcept {
    if (std::exchange(active_, false)) {
      cleanup_();
    }
  }

 private:
  Cleanup cleanup_;
  bool active_ = true;
};

template <typename Cleanup>
[[nodiscard]] ScopedCleanup<Cleanup> make_scoped_cleanup(Cleanup cleanup) {
  return ScopedCleanup<Cleanup>(std::move(cleanup));
}

}  // namespace spoink::support
