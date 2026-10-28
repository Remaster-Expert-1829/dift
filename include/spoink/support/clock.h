#pragma once

#include <chrono>

namespace spoink::support {

using MonotonicTime = std::chrono::steady_clock::time_point;
using UtcTime = std::chrono::system_clock::time_point;

class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual MonotonicTime now() const noexcept = 0;
};

class SystemMonotonicClock final : public Clock {
 public:
  [[nodiscard]] MonotonicTime now() const noexcept override { return std::chrono::steady_clock::now(); }
};

class FakeClock final : public Clock {
 public:
  explicit FakeClock(MonotonicTime initial = MonotonicTime{}) : now_(initial) {}

  [[nodiscard]] MonotonicTime now() const noexcept override { return now_; }
  void advance(std::chrono::steady_clock::duration duration) noexcept { now_ += duration; }
  void set(MonotonicTime value) noexcept { now_ = value; }

 private:
  MonotonicTime now_;
};

}  // namespace spoink::support
