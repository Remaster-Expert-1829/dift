// libFuzzer target: feed arbitrary bytes into FrameReader::feed() and decode_prefix().
// Build with: cmake -DSPOINK_ENABLE_FUZZING=ON ...
// Run with:   ./spoink_fuzz_framing corpus/ -max_total_time=60

#include <cstddef>
#include <cstdint>

#include "spoink/protocol/framing.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Feed bytes into FrameReader.
  spoink::protocol::FrameReader reader;
  reader.feed(reinterpret_cast<const std::byte*>(data), size);

  // If we accumulated a full 32-byte prefix, try to decode it.
  if (size >= spoink::protocol::kFramePrefixBytes) {
    std::array<std::byte, spoink::protocol::kFramePrefixBytes> prefix_buf{};
    for (std::size_t i = 0; i < spoink::protocol::kFramePrefixBytes; ++i) {
      prefix_buf[i] = static_cast<std::byte>(data[i]);
    }
    (void)spoink::protocol::decode_prefix(prefix_buf);
  }

  return 0;
}
