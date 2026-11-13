#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "spoink/identity/identity.h"
#include "spoink/pairing/pairing.h"
#include "spoink/support/result.h"

// M2 integration tests require loopback networking. Full E2E (pair + transfer)
// is in m2_e2e_test.cpp. These tests verify individual subsystem contracts.

TEST_CASE("derive_sas: 20-bit output in AA-BBB format", "[pairing][sas]") {
  std::array<std::byte, 32> material{};
  material[0] = std::byte{0x5A};  // AA = 5A
  material[1] = std::byte{0xFE};  // BBB upper byte
  material[2] = std::byte{0x20};  // BBB lower nibble: (0xFE << 4 | 0x20 >> 4) = 0xFE2
  // Expected: "5A-FE2"

  const auto sas = spoink::pairing::derive_sas(material);
  const std::string s{sas.text};
  REQUIRE(s.size() == 6);
  REQUIRE(s[2] == '-');
  REQUIRE(s == "5A-FE2");
}

TEST_CASE("derive_sas: zero material → 00-000", "[pairing][sas]") {
  std::array<std::byte, 32> material{};
  const auto sas = spoink::pairing::derive_sas(material);
  REQUIRE(std::string{sas.text} == "00-000");
}

TEST_CASE("derive_sas: max material → FF-FFF", "[pairing][sas]") {
  std::array<std::byte, 32> material{};
  material[0] = std::byte{0xFF};
  material[1] = std::byte{0xFF};
  material[2] = std::byte{0xF0};  // upper nibble = F → BBB = FFF
  const auto sas = spoink::pairing::derive_sas(material);
  REQUIRE(std::string{sas.text} == "FF-FFF");
}

TEST_CASE("identity: load_or_create is idempotent across processes (file-system)", "[identity][integration]") {
  const auto root = std::filesystem::temp_directory_path() / "spoink_m2_int_test";
  std::filesystem::remove_all(root);

  auto r1 = spoink::identity::load_or_create_identity(root, "Device1");
  REQUIRE(r1.has_value());

  auto r2 = spoink::identity::load_or_create_identity(root, "Ignored");
  REQUIRE(r2.has_value());

  REQUIRE(r1.value_if()->peer_id() == r2.value_if()->peer_id());

  std::filesystem::remove_all(root);
}
