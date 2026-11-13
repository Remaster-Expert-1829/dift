#include <catch2/catch_test_macros.hpp>

#include "spoink/protocol/codec.h"

using namespace spoink::protocol;

TEST_CASE("CBOR encoder: encode_uint deterministic", "[codec]") {
  CborEncoder enc;
  enc.encode_uint(0);
  enc.encode_uint(23);
  enc.encode_uint(24);
  enc.encode_uint(255);
  enc.encode_uint(256);
  enc.encode_uint(0xFFFFFFFFFFFFFFFFULL);

  const auto& b = enc.bytes();
  REQUIRE(!b.empty());
  // 0 → 0x00, 23 → 0x17, 24 → 0x18 0x18, 255 → 0x18 0xFF, 256 → 0x19 0x01 0x00
  REQUIRE(b[0] == std::byte{0x00});
  REQUIRE(b[1] == std::byte{0x17});
  REQUIRE(b[2] == std::byte{0x18});
  REQUIRE(b[3] == std::byte{0x18});
}

TEST_CASE("PairHello encode/decode round-trip", "[codec]") {
  PairHelloMsg msg;
  msg.peer_id.fill(std::byte{0xAB});
  msg.display_name = "alice";
  msg.attempt_nonce.fill(std::byte{0x01});

  const auto encoded = encode_pair_hello(msg);
  REQUIRE(!encoded.empty());

  const auto r = decode_pair_hello(encoded);
  REQUIRE(r.has_value());
  const auto& out = *r.value_if();

  REQUIRE(out.peer_id == msg.peer_id);
  REQUIRE(out.display_name == msg.display_name);
  REQUIRE(out.attempt_nonce == msg.attempt_nonce);
}

TEST_CASE("PairConfirm encode/decode round-trip", "[codec]") {
  PairConfirmMsg msg{true};
  const auto encoded = encode_pair_confirm(msg);
  const auto r = decode_pair_confirm(encoded);
  REQUIRE(r.has_value());
  REQUIRE(r.value_if()->accepted == true);
}

TEST_CASE("Hello encode/decode round-trip", "[codec]") {
  HelloMsg msg;
  msg.peer_id.fill(std::byte{0x12});
  msg.role = "SENDER";
  msg.instance_nonce.fill(std::byte{0x34});

  const auto encoded = encode_hello(msg);
  const auto r = decode_hello(encoded);
  REQUIRE(r.has_value());
  const auto& out = *r.value_if();
  REQUIRE(out.peer_id == msg.peer_id);
  REQUIRE(out.role == "SENDER");
}

TEST_CASE("SessionOffer encode/decode round-trip", "[codec]") {
  SessionOfferMsg msg;
  msg.manifest_digest.fill(std::byte{0x99});
  msg.manifest_schema = 1;
  msg.chunk_profile_id = "rabin-v1";
  msg.file_count = 1;
  msg.logical_bytes = 65536;
  msg.overwrite_requested = false;

  FileSummary fs;
  fs.file_id = 0;
  fs.relative_path = "data/file.bin";
  fs.logical_size = 65536;
  fs.file_sha256.fill(std::byte{0x55});
  msg.files.push_back(fs);

  const auto encoded = encode_session_offer(msg);
  const auto r = decode_session_offer(encoded);
  REQUIRE(r.has_value());
  const auto& out = *r.value_if();
  REQUIRE(out.chunk_profile_id == "rabin-v1");
  REQUIRE(out.file_count == 1);
  REQUIRE(out.files.size() == 1);
  REQUIRE(out.files[0].relative_path == "data/file.bin");
}

TEST_CASE("ChunkPut encode/decode round-trip", "[codec]") {
  ChunkPutMsg msg;
  msg.transfer_id.fill(std::byte{0x01});
  msg.chunk_hash.fill(std::byte{0xDE});
  msg.plaintext_length = 16384;
  msg.codec = "raw";
  msg.encoded_length = 16384;
  msg.attempt = 1;

  const auto encoded = encode_chunk_put(msg);
  const auto r = decode_chunk_put(encoded);
  REQUIRE(r.has_value());
  REQUIRE(r.value_if()->codec == "raw");
  REQUIRE(r.value_if()->plaintext_length == 16384);
}

TEST_CASE("TransferPlan encode/decode round-trip with hashes", "[codec]") {
  TransferPlanMsg msg;
  msg.transfer_id.fill(std::byte{0x02});
  msg.session_capability.fill(std::byte{0xCA});
  msg.plan_part = 0;
  msg.plan_final = true;

  std::array<std::byte, 32> h1;
  h1.fill(std::byte{0xAA});
  std::array<std::byte, 32> h2;
  h2.fill(std::byte{0xBB});
  msg.missing_chunk_hashes.push_back(h1);
  msg.missing_chunk_hashes.push_back(h2);

  const auto encoded = encode_transfer_plan(msg);
  const auto r = decode_transfer_plan(encoded);
  REQUIRE(r.has_value());
  const auto& out = *r.value_if();
  REQUIRE(out.missing_chunk_hashes.size() == 2);
  REQUIRE(out.missing_chunk_hashes[0] == h1);
  REQUIRE(out.missing_chunk_hashes[1] == h2);
  REQUIRE(out.plan_final == true);
}

TEST_CASE("Ping/Pong encode/decode round-trip", "[codec]") {
  PingMsg ping{0x1234567890ABCDEFULL};
  const auto enc_p = encode_ping(ping);
  const auto r = decode_ping(enc_p);
  REQUIRE(r.has_value());
  REQUIRE(r.value_if()->monotonic_token == 0x1234567890ABCDEFULL);

  PongMsg pong{ping.monotonic_token};
  const auto enc_po = encode_pong(pong);
  const auto r2 = decode_pong(enc_po);
  REQUIRE(r2.has_value());
  REQUIRE(r2.value_if()->monotonic_token == ping.monotonic_token);
}

TEST_CASE("CborDecoder: rejects truncated input", "[codec][sec]") {
  const std::vector<std::byte> empty;
  CborDecoder dec(empty);
  REQUIRE(!dec.read_uint().has_value());
}
