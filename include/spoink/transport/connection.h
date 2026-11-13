#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "spoink/identity/identity.h"
#include "spoink/protocol/framing.h"
#include "spoink/support/openssl_raii.h"
#include "spoink/support/result.h"
#include "spoink/support/types.h"

struct sqlite3;

namespace spoink::transport {

// Options for the normal (non-pairing) TLS listener.
struct ListenOptions {
  std::uint16_t port{0};  // 0 → OS-assigned
  std::uint32_t handshake_timeout_ms{10000};
  std::uint32_t idle_timeout_ms{60000};
};

// Options for the normal TLS initiator.
struct ConnectOptions {
  std::string host;
  std::uint16_t port{0};
  std::uint32_t handshake_timeout_ms{10000};
  std::uint32_t idle_timeout_ms{60000};
};

// A verified TLS connection for the spoink/1 protocol.
// move-only; holds the fd and SSL state.
class TlsConnection {
 public:
  TlsConnection() = default;
  ~TlsConnection();
  TlsConnection(const TlsConnection&) = delete;
  TlsConnection& operator=(const TlsConnection&) = delete;
  TlsConnection(TlsConnection&&) noexcept;
  TlsConnection& operator=(TlsConnection&&) noexcept;

  [[nodiscard]] bool valid() const noexcept { return ssl_ != nullptr; }
  [[nodiscard]] const support::PeerId& peer_id() const noexcept { return peer_id_; }

  // Send a complete frame. Blocks until all bytes are written.
  [[nodiscard]] support::Result<void> send_frame(const protocol::FramePrefix& prefix,
                                                 const std::vector<std::byte>& header,
                                                 const std::vector<std::byte>& payload);

  // Read one complete frame. Blocks until a full frame arrives.
  // Returns the prefix; header and payload are appended to the out-params.
  [[nodiscard]] support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>> read_frame();

  void close();

 private:
  friend support::Result<TlsConnection> tls_connect(const identity::DeviceIdentity& identity,
                                                    const ConnectOptions& opts, sqlite3* db);
  friend support::Result<TlsConnection> tls_accept_one(const identity::DeviceIdentity& identity,
                                                       const ListenOptions& opts, sqlite3* db);

  TlsConnection(ssl_st* ssl, int fd, support::PeerId peer_id) : ssl_(ssl), fd_(fd), peer_id_(peer_id) {}

  ssl_st* ssl_{nullptr};
  int fd_{-1};
  support::PeerId peer_id_{std::array<std::byte, 32>{}};
};

// Connect to a spoink/1 server, verify its SPKI pin, return the connection.
[[nodiscard]] support::Result<TlsConnection> tls_connect(const identity::DeviceIdentity& identity,
                                                         const ConnectOptions& opts, sqlite3* db);

// Accept one spoink/1 connection (blocks), verify SPKI pin, return the connection.
[[nodiscard]] support::Result<TlsConnection> tls_accept_one(const identity::DeviceIdentity& identity,
                                                            const ListenOptions& opts, sqlite3* db);

// Verify peer SPKI matches the pinned value from the trust store.
// Constant-time comparison to prevent timing side-channel.
[[nodiscard]] support::Result<void> verify_spki_pin(ssl_st* ssl, const std::vector<std::byte>& expected_spki_der);

// Retrieve the actual listening port after bind (for OS-assigned ports).
[[nodiscard]] support::Result<std::uint16_t> get_bound_port(int server_fd);

}  // namespace spoink::transport
