// TEST-SEC-001: MITM/pin-mismatch — untrusted peer connection must be rejected.
//
// Two identities (A = server, B = client) are created with empty trust DBs.
// When B connects to A, neither peer is in the other's trust store, so both
// tls_connect and tls_accept_one must return errors.

#include <sqlite3.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>

#include "spoink/identity/identity.h"
#include "spoink/support/result.h"
#include "spoink/transport/connection.h"

namespace {

static constexpr std::uint16_t kSecTestPort = 19877U;

static constexpr const char* kCreateSchema = R"(
CREATE TABLE IF NOT EXISTS trusted_peers (
  peer_id              BLOB    NOT NULL PRIMARY KEY,
  display_name         TEXT    NOT NULL,
  spki_der             BLOB    NOT NULL,
  last_certificate_der BLOB    NOT NULL,
  paired_at_ms         INTEGER NOT NULL,
  last_seen_at_ms      INTEGER NOT NULL,
  revoked_at_ms        INTEGER
);
)";

sqlite3* make_empty_db() {
  sqlite3* db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return nullptr;
  sqlite3_exec(db, kCreateSchema, nullptr, nullptr, nullptr);
  return db;
}

}  // namespace

TEST_CASE("TEST-SEC-001: untrusted peer is rejected on connect", "[security]") {
  const auto rootA = std::filesystem::temp_directory_path() / "spoink_sec001_A";
  const auto rootB = std::filesystem::temp_directory_path() / "spoink_sec001_B";
  std::filesystem::remove_all(rootA);
  std::filesystem::remove_all(rootB);

  auto idA_r = spoink::identity::load_or_create_identity(rootA, "SecTestA");
  REQUIRE(idA_r.has_value());
  auto idB_r = spoink::identity::load_or_create_identity(rootB, "SecTestB");
  REQUIRE(idB_r.has_value());

  sqlite3* dbA = make_empty_db();
  sqlite3* dbB = make_empty_db();
  REQUIRE(dbA != nullptr);
  REQUIRE(dbB != nullptr);

  spoink::transport::ListenOptions listen_opts;
  listen_opts.port = kSecTestPort;
  listen_opts.handshake_timeout_ms = 5000;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

  std::optional<bool> server_rejected;
  const auto& idA = *idA_r.value_if();
  std::thread server_thread([&]() {
    auto r = spoink::transport::tls_accept_one(idA, listen_opts, dbA);
    server_rejected = !r.has_value();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

  spoink::transport::ConnectOptions connect_opts;
  connect_opts.host = "127.0.0.1";
  connect_opts.port = kSecTestPort;
  connect_opts.handshake_timeout_ms = 5000;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

  auto client_r = spoink::transport::tls_connect(*idB_r.value_if(), connect_opts, dbB);

  // B is not in A's trust store; the connection must be rejected.
  REQUIRE(!client_r.has_value());

  server_thread.join();
  REQUIRE(server_rejected.has_value());
  REQUIRE(*server_rejected);

  sqlite3_close(dbA);
  sqlite3_close(dbB);
  std::filesystem::remove_all(rootA);
  std::filesystem::remove_all(rootB);
}
