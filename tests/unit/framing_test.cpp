#include <catch2/catch_test_macros.hpp>

#include "spoink/protocol/framing.h"

using namespace spoink::protocol;

TEST_CASE("encode/decode prefix round-trip", "[framing]") {
  FramePrefix in;
  in.major = 1;
  in.minor = 0;
  in.channel = Channel::kControl;
  in.message_type = MessageType::kHello;
  in.flags = 0;
  in.header_length = 42;
  in.payload_length = 1024;
  in.request_id = 0xDEADBEEFCAFEBABEULL;

  const auto wire = encode_prefix(in);
  const auto r = decode_prefix(wire);
  REQUIRE(r.has_value());
  const auto& out = *r.value_if();

  REQUIRE(out.major == in.major);
  REQUIRE(out.minor == in.minor);
  REQUIRE(out.channel == in.channel);
  REQUIRE(out.message_type == in.message_type);
  REQUIRE(out.flags == in.flags);
  REQUIRE(out.header_length == in.header_length);
  REQUIRE(out.payload_length == in.payload_length);
  REQUIRE(out.request_id == in.request_id);
}

TEST_CASE("decode_prefix: rejects bad magic", "[framing][sec]") {
  std::array<std::byte, kFramePrefixBytes> raw{};
  raw[0] = std::byte{0xDE};  // wrong magic
  REQUIRE(!decode_prefix(raw).has_value());
}

TEST_CASE("decode_prefix: rejects reserved nonzero", "[framing][sec]") {
  FramePrefix p;
  p.major = 1;
  auto wire = encode_prefix(p);
  wire[10] = std::byte{0x01};  // reserved byte must be zero
  REQUIRE(!decode_prefix(wire).has_value());
}

TEST_CASE("decode_prefix: rejects major version != 1", "[framing][sec]") {
  FramePrefix p;
  p.major = 1;
  auto wire = encode_prefix(p);
  wire[4] = std::byte{2};  // major=2
  REQUIRE(!decode_prefix(wire).has_value());
}

TEST_CASE("validate_prefix_lengths: rejects oversized header", "[framing][sec]") {
  FramePrefix p;
  p.header_length = Limits::kMaxHeaderBytes + 1;
  REQUIRE(!validate_prefix_lengths(p).has_value());
}

TEST_CASE("validate_prefix_lengths: rejects oversized payload", "[framing][sec]") {
  FramePrefix p;
  p.payload_length = Limits::kMaxPayloadBytes + 1;
  REQUIRE(!validate_prefix_lengths(p).has_value());
}

TEST_CASE("FrameReader: accumulates prefix then header then payload", "[framing]") {
  FramePrefix pfx;
  pfx.major = 1;
  pfx.channel = Channel::kData;
  pfx.message_type = MessageType::kChunkPut;
  pfx.header_length = 4;
  pfx.payload_length = 8;

  const auto wire = encode_prefix(pfx);
  std::array<std::byte, 4> hdr_data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  std::array<std::byte, 8> pay_data{};
  for (std::size_t i = 0; i < 8; ++i) pay_data[i] = static_cast<std::byte>(i + 10);

  std::vector<std::byte> full_frame(wire.begin(), wire.end());
  full_frame.insert(full_frame.end(), hdr_data.begin(), hdr_data.end());
  full_frame.insert(full_frame.end(), pay_data.begin(), pay_data.end());

  FrameReader reader;
  REQUIRE(reader.state() == FrameReader::State::kNeedPrefix);

  const std::size_t consumed = reader.feed(full_frame.data(), full_frame.size());
  REQUIRE(consumed == full_frame.size());
  REQUIRE(reader.complete());
  REQUIRE(reader.prefix().channel == Channel::kData);
  REQUIRE(reader.header_bytes().size() == 4);
  REQUIRE(reader.payload_bytes().size() == 8);
}

TEST_CASE("FrameReader: handles byte-by-byte feeding", "[framing]") {
  FramePrefix pfx;
  pfx.major = 1;
  pfx.header_length = 2;
  pfx.payload_length = 2;

  const auto wire = encode_prefix(pfx);
  std::vector<std::byte> full_frame(wire.begin(), wire.end());
  full_frame.push_back(std::byte{0xAA});
  full_frame.push_back(std::byte{0xBB});
  full_frame.push_back(std::byte{0xCC});
  full_frame.push_back(std::byte{0xDD});

  FrameReader reader;
  for (const auto& b : full_frame) {
    if (!reader.complete()) {
      reader.feed(&b, 1);
    }
  }
  REQUIRE(reader.complete());
}
