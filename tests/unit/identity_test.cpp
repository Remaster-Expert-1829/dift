#include <catch2/catch_test_macros.hpp>

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "spoink/identity/identity.h"
#include "spoink/support/result.h"

namespace {

std::filesystem::path temp_state_root() {
  return std::filesystem::temp_directory_path() / "spoink_identity_test";
}

}  // namespace

TEST_CASE("load_or_create_identity: creates new identity on first call", "[identity]") {
  const auto root = temp_state_root() / "create_test";
  std::filesystem::remove_all(root);

  auto r = spoink::identity::load_or_create_identity(root, "TestDevice");
  REQUIRE(r.has_value());
  const auto& id = *r.value_if();
  REQUIRE(id.valid());
  REQUIRE(!id.display_name().empty());

  // peer_id must be non-zero
  const auto& pid = id.peer_id().bytes();
  bool any_nonzero = false;
  for (auto b : pid) if (b != std::byte{0}) { any_nonzero = true; break; }
  REQUIRE(any_nonzero);

  std::filesystem::remove_all(root);
}

TEST_CASE("load_or_create_identity: second call loads same identity", "[identity]") {
  const auto root = temp_state_root() / "load_test";
  std::filesystem::remove_all(root);

  auto r1 = spoink::identity::load_or_create_identity(root, "TestDevice");
  REQUIRE(r1.has_value());
  const auto pid1 = r1.value_if()->peer_id();

  auto r2 = spoink::identity::load_or_create_identity(root, "Ignored");
  REQUIRE(r2.has_value());
  const auto pid2 = r2.value_if()->peer_id();

  REQUIRE(pid1 == pid2);

  std::filesystem::remove_all(root);
}

TEST_CASE("load_or_create_identity: make_tls_context returns valid ctx for pair ALPN", "[identity]") {
  const auto root = temp_state_root() / "tls_test";
  std::filesystem::remove_all(root);

  auto r = spoink::identity::load_or_create_identity(root, "TLSTest");
  REQUIRE(r.has_value());
  const auto& id = *r.value_if();

  auto ctx_r = id.make_tls_context("spoink-pair/1", false);
  REQUIRE(ctx_r.has_value());
  REQUIRE(ctx_r.value_if()->get() != nullptr);

  std::filesystem::remove_all(root);
}

TEST_CASE("load_or_create_identity: make_tls_context rejects unknown ALPN", "[identity]") {
  const auto root = temp_state_root() / "alpn_test";
  std::filesystem::remove_all(root);

  auto r = spoink::identity::load_or_create_identity(root, "ALPNTest");
  REQUIRE(r.has_value());

  auto ctx_r = r.value_if()->make_tls_context("unknown-alpn/99", false);
  REQUIRE(!ctx_r.has_value());

  std::filesystem::remove_all(root);
}

TEST_CASE("verify_key_permissions: detects insecure mode", "[identity][sec]") {
  const auto tmp = std::filesystem::temp_directory_path() / "spoink_perm_test.key";
  {
    std::FILE* f = std::fopen(tmp.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fclose(f);
  }
  // Set world-readable: mode 0644
  ::chmod(tmp.c_str(), 0644);

  auto r = spoink::identity::verify_key_permissions(tmp);
  REQUIRE(!r.has_value());

  std::filesystem::remove(tmp);
}
