#include "spoink/identity/identity.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <sys/stat.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "spoink/support/result.h"

namespace {

std::filesystem::path temp_state_root() { return std::filesystem::temp_directory_path() / "spoink_identity_test"; }

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
  for (auto b : pid)
    if (b != std::byte{0}) {
      any_nonzero = true;
      break;
    }
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

TEST_CASE("load_or_create_identity: rejects non-Ed25519 key", "[identity][sec]") {
  const auto root = temp_state_root() / "wrong_key_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "identity");

  // Generate an RSA-2048 key and write as PEM — load_or_create_identity must reject it.
  EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  REQUIRE(kctx != nullptr);
  REQUIRE(EVP_PKEY_keygen_init(kctx) == 1);
  REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) > 0);  // NOLINT(cppcoreguidelines-avoid-magic-numbers)
  EVP_PKEY* rsa_pkey = nullptr;
  REQUIRE(EVP_PKEY_keygen(kctx, &rsa_pkey) == 1);
  EVP_PKEY_CTX_free(kctx);

  const auto key_path = root / "identity" / "device.key";
  BIO* bio = BIO_new_file(key_path.c_str(), "w");
  REQUIRE(bio != nullptr);
  PEM_write_bio_PrivateKey(bio, rsa_pkey, nullptr, nullptr, 0, nullptr, nullptr);
  BIO_free(bio);
  EVP_PKEY_free(rsa_pkey);
  ::chmod(key_path.c_str(), 0600);

  // A dummy cert file so the loader doesn't fail before reaching key-type check.
  { std::ofstream f(root / "identity" / "device.crt"); }

  auto r = spoink::identity::load_or_create_identity(root, "WrongKeyTest");
  REQUIRE(!r.has_value());

  std::filesystem::remove_all(root);
}

TEST_CASE("verify_key_permissions: detects insecure mode", "[identity][sec]") {
  const auto tmp = std::filesystem::temp_directory_path() / "spoink_perm_test.key";
  {
    std::FILE* f = std::fopen(tmp.c_str(), "w");
    REQUIRE(f != nullptr);
    (void)std::fclose(f);
  }
  // Set world-readable: mode 0644
  ::chmod(tmp.c_str(), 0644);

  auto r = spoink::identity::verify_key_permissions(tmp);
  REQUIRE(!r.has_value());

  std::filesystem::remove(tmp);
}
