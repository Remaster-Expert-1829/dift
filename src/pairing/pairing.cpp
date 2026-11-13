#include "spoink/pairing/pairing.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "spoink/protocol/codec.h"
#include "spoink/protocol/framing.h"
#include "spoink/support/error.h"

namespace spoink::pairing {

namespace {

support::Error make_net(const char* key) { return support::make_error(support::ErrorCode::kResourceFailure, key); }
support::Error make_proto(const char* key) { return support::make_error(support::ErrorCode::kInvalidArgument, key); }
support::Error make_db(const char* key) { return support::make_error(support::ErrorCode::kResourceFailure, key); }

// ── SAS derivation ────────────────────────────────────────────────────────────

// Writes one frame to a TLS session. Returns false on write error.
bool ssl_write_frame(ssl_st* ssl, const protocol::FramePrefix& pfx, const std::vector<std::byte>& header,
                     const std::vector<std::byte>& payload) {
  const auto wire_prefix = protocol::encode_prefix(pfx);
  if (SSL_write(ssl, wire_prefix.data(), static_cast<int>(protocol::kFramePrefixBytes)) !=
      static_cast<int>(protocol::kFramePrefixBytes)) {
    return false;
  }
  if (!header.empty()) {
    if (SSL_write(ssl, header.data(), static_cast<int>(header.size())) != static_cast<int>(header.size())) {
      return false;
    }
  }
  if (!payload.empty()) {
    if (SSL_write(ssl, payload.data(), static_cast<int>(payload.size())) != static_cast<int>(payload.size())) {
      return false;
    }
  }
  return true;
}

// Reads exactly n bytes from a TLS session into buf.
bool ssl_read_exact(ssl_st* ssl, void* buf, int n) {
  int remaining = n;
  auto* ptr = static_cast<char*>(buf);
  while (remaining > 0) {
    const int rc = SSL_read(ssl, ptr, remaining);
    if (rc <= 0) return false;
    ptr += rc;
    remaining -= rc;
  }
  return true;
}

// Reads one complete frame. Validates limits before allocating.
support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>> ssl_read_frame(ssl_st* ssl) {
  std::array<std::byte, protocol::kFramePrefixBytes> raw_prefix{};
  if (!ssl_read_exact(ssl, raw_prefix.data(), static_cast<int>(protocol::kFramePrefixBytes))) {
    return support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>::failure(
        make_net("m2.pair.read_prefix"));
  }
  auto pfx_r = protocol::decode_prefix(raw_prefix);
  if (!pfx_r.has_value()) {
    return support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>::failure(pfx_r.error());
  }
  const auto& pfx = *pfx_r.value_if();
  auto len_r = protocol::validate_prefix_lengths(pfx);
  if (!len_r.has_value()) {
    return support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>::failure(len_r.error());
  }

  const std::size_t total = pfx.header_length + static_cast<std::size_t>(pfx.payload_length);
  std::vector<std::byte> body(total);
  if (total > 0 && !ssl_read_exact(ssl, body.data(), static_cast<int>(total))) {
    return support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>::failure(
        make_net("m2.pair.read_body"));
  }
  return support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>::success(
      std::make_pair(pfx, std::move(body)));
}

// Sends a PAIR_HELLO frame.
bool send_pair_hello(ssl_st* ssl, const protocol::PairHelloMsg& msg) {
  const auto header = protocol::encode_pair_hello(msg);
  protocol::FramePrefix pfx;
  pfx.channel = protocol::Channel::kPairing;
  pfx.message_type = protocol::MessageType::kPairHello;
  pfx.header_length = static_cast<std::uint32_t>(header.size());
  pfx.payload_length = 0;
  return ssl_write_frame(ssl, pfx, header, {});
}

// Sends a PAIR_CONFIRM frame.
bool send_pair_confirm(ssl_st* ssl, bool accepted) {
  const auto header = protocol::encode_pair_confirm(protocol::PairConfirmMsg{accepted});
  protocol::FramePrefix pfx;
  pfx.channel = protocol::Channel::kPairing;
  pfx.message_type = protocol::MessageType::kPairConfirm;
  pfx.header_length = static_cast<std::uint32_t>(header.size());
  pfx.payload_length = 0;
  return ssl_write_frame(ssl, pfx, header, {});
}

// Sends a PAIR_RESULT frame.
bool send_pair_result(ssl_st* ssl, bool paired) {
  protocol::PairResultMsg msg;
  msg.status = paired ? "PAIRED" : "REJECTED";
  const auto header = protocol::encode_pair_result(msg);
  protocol::FramePrefix pfx;
  pfx.channel = protocol::Channel::kPairing;
  pfx.message_type = protocol::MessageType::kPairResult;
  pfx.header_length = static_cast<std::uint32_t>(header.size());
  pfx.payload_length = 0;
  return ssl_write_frame(ssl, pfx, header, {});
}

// Receives and decodes a PAIR_HELLO.
support::Result<protocol::PairHelloMsg> recv_pair_hello(ssl_st* ssl) {
  auto frame_r = ssl_read_frame(ssl);
  if (!frame_r.has_value()) return support::Result<protocol::PairHelloMsg>::failure(frame_r.error());
  const auto& [pfx, body] = *frame_r.value_if();
  if (pfx.message_type != protocol::MessageType::kPairHello) {
    return support::Result<protocol::PairHelloMsg>::failure(make_proto("m2.pair.expected_hello"));
  }
  const std::vector<std::byte> header_bytes(body.begin(),
                                            body.begin() + static_cast<std::ptrdiff_t>(pfx.header_length));
  return protocol::decode_pair_hello(header_bytes);
}

// Receives and decodes a PAIR_CONFIRM.
support::Result<protocol::PairConfirmMsg> recv_pair_confirm(ssl_st* ssl) {
  auto frame_r = ssl_read_frame(ssl);
  if (!frame_r.has_value()) return support::Result<protocol::PairConfirmMsg>::failure(frame_r.error());
  const auto& [pfx, body] = *frame_r.value_if();
  if (pfx.message_type != protocol::MessageType::kPairConfirm) {
    return support::Result<protocol::PairConfirmMsg>::failure(make_proto("m2.pair.expected_confirm"));
  }
  const std::vector<std::byte> header_bytes(body.begin(),
                                            body.begin() + static_cast<std::ptrdiff_t>(pfx.header_length));
  return protocol::decode_pair_confirm(header_bytes);
}

// Receives and decodes a PAIR_RESULT.
support::Result<protocol::PairResultMsg> recv_pair_result(ssl_st* ssl) {
  auto frame_r = ssl_read_frame(ssl);
  if (!frame_r.has_value()) return support::Result<protocol::PairResultMsg>::failure(frame_r.error());
  const auto& [pfx, body] = *frame_r.value_if();
  if (pfx.message_type != protocol::MessageType::kPairResult) {
    return support::Result<protocol::PairResultMsg>::failure(make_proto("m2.pair.expected_result"));
  }
  const std::vector<std::byte> header_bytes(body.begin(),
                                            body.begin() + static_cast<std::ptrdiff_t>(pfx.header_length));
  return protocol::decode_pair_result(header_bytes);
}

// Derives SAS context bytes: min(a,b) || max(a,b) || min(n1,n2) || max(n1,n2).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<unsigned char> make_exporter_context(const std::array<std::byte, 32>& id_local,
                                                 const std::array<std::byte, 32>& id_remote,
                                                 const std::array<std::byte, 32>& nonce_local,
                                                 const std::array<std::byte, 32>& nonce_remote) {
  const bool id_local_smaller = (id_local < id_remote);
  const auto& id_min = id_local_smaller ? id_local : id_remote;
  const auto& id_max = id_local_smaller ? id_remote : id_local;

  const bool nonce_local_smaller = (nonce_local < nonce_remote);
  const auto& n_min = nonce_local_smaller ? nonce_local : nonce_remote;
  const auto& n_max = nonce_local_smaller ? nonce_remote : nonce_local;

  std::vector<unsigned char> ctx;
  ctx.reserve(128);
  for (auto b : id_min) ctx.push_back(static_cast<unsigned char>(b));
  for (auto b : id_max) ctx.push_back(static_cast<unsigned char>(b));
  for (auto b : n_min) ctx.push_back(static_cast<unsigned char>(b));
  for (auto b : n_max) ctx.push_back(static_cast<unsigned char>(b));
  return ctx;
}

// Extracts the peer's SPKI DER and certificate DER from an established SSL.
support::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>> extract_peer_cert_info(ssl_st* ssl) {
  X509* cert = SSL_get_peer_certificate(ssl);
  if (cert == nullptr) {
    return support::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>>::failure(
        make_proto("m2.pair.no_peer_cert"));
  }

  // Certificate DER
  unsigned char* cert_der = nullptr;
  const int cert_len = i2d_X509(cert, &cert_der);
  if (cert_len <= 0 || cert_der == nullptr) {
    X509_free(cert);
    return support::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>>::failure(
        make_net("m2.pair.cert_der"));
  }
  std::vector<std::byte> cert_bytes(static_cast<std::size_t>(cert_len));
  for (int i = 0; i < cert_len; ++i) cert_bytes[static_cast<std::size_t>(i)] = static_cast<std::byte>(cert_der[i]);
  OPENSSL_free(cert_der);

  // SPKI DER
  EVP_PKEY* pkey = X509_get0_pubkey(cert);
  unsigned char* spki_der = nullptr;
  const int spki_len = i2d_PUBKEY(pkey, &spki_der);
  X509_free(cert);
  if (spki_len <= 0 || spki_der == nullptr) {
    return support::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>>::failure(
        make_net("m2.pair.spki_der"));
  }
  std::vector<std::byte> spki_bytes(static_cast<std::size_t>(spki_len));
  for (int i = 0; i < spki_len; ++i) spki_bytes[static_cast<std::size_t>(i)] = static_cast<std::byte>(spki_der[i]);
  OPENSSL_free(spki_der);

  return support::Result<std::pair<std::vector<std::byte>, std::vector<std::byte>>>::success(
      std::make_pair(std::move(spki_bytes), std::move(cert_bytes)));
}

// Sets SO_RCVTIMEO / SO_SNDTIMEO on a socket.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void set_socket_timeout(int fd, std::uint32_t timeout_ms) {
  struct timeval tv {};
  tv.tv_sec = static_cast<time_t>(timeout_ms / 1000U);
  tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000U) * 1000U);
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// Core pairing exchange over an established TLS session (after SSL_accept or SSL_connect).
// is_listener: true for the listening side (sends PAIR_HELLO first).
support::Result<PairedPeer> run_pairing_exchange(ssl_st* ssl, const identity::DeviceIdentity& identity,
                                                 bool is_listener, const SasConfirmFn& confirm_fn) {
  // Generate a 32-byte attempt nonce.
  std::array<std::byte, 32> local_nonce{};
  if (RAND_bytes(reinterpret_cast<unsigned char*>(local_nonce.data()), 32) != 1) {
    return support::Result<PairedPeer>::failure(make_net("m2.pair.rand_nonce"));
  }

  protocol::PairHelloMsg local_hello;
  local_hello.peer_id = identity.peer_id().bytes();
  local_hello.display_name = identity.display_name();
  local_hello.attempt_nonce = local_nonce;

  // Exchange PAIR_HELLO. Listener sends first to avoid deadlock.
  protocol::PairHelloMsg remote_hello;
  if (is_listener) {
    if (!send_pair_hello(ssl, local_hello)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_hello"));
    }
    auto r = recv_pair_hello(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
    remote_hello = std::move(*r.value_if());
  } else {
    auto r = recv_pair_hello(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
    remote_hello = std::move(*r.value_if());
    if (!send_pair_hello(ssl, local_hello)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_hello"));
    }
  }

  // Derive SAS via TLS exporter.
  const auto ctx =
      make_exporter_context(local_hello.peer_id, remote_hello.peer_id, local_nonce, remote_hello.attempt_nonce);
  std::array<std::byte, 32> export_material{};
  if (SSL_export_keying_material(ssl, reinterpret_cast<unsigned char*>(export_material.data()), 32,
                                 "EXPORTER-Spoink-Pairing-v1", 26, ctx.data(), ctx.size(), 1) != 1) {
    return support::Result<PairedPeer>::failure(make_net("m2.pair.exporter"));
  }

  const Sas sas = derive_sas(export_material);

  // Ask local user.
  bool local_accepted = false;
  try {
    local_accepted = confirm_fn(sas, remote_hello.display_name);
  } catch (...) {
    local_accepted = false;
  }

  // Exchange PAIR_CONFIRM (listener sends first).
  bool remote_accepted = false;
  if (is_listener) {
    if (!send_pair_confirm(ssl, local_accepted)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_confirm"));
    }
    auto r = recv_pair_confirm(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
    remote_accepted = r.value_if()->accepted;
  } else {
    auto r = recv_pair_confirm(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
    remote_accepted = r.value_if()->accepted;
    if (!send_pair_confirm(ssl, local_accepted)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_confirm"));
    }
  }

  const bool both_accepted = local_accepted && remote_accepted;

  // Exchange PAIR_RESULT (listener sends first).
  if (is_listener) {
    if (!send_pair_result(ssl, both_accepted)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_result"));
    }
    auto r = recv_pair_result(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
  } else {
    auto r = recv_pair_result(ssl);
    if (!r.has_value()) return support::Result<PairedPeer>::failure(r.error());
    if (!send_pair_result(ssl, both_accepted)) {
      return support::Result<PairedPeer>::failure(make_net("m2.pair.send_result"));
    }
  }

  if (!both_accepted) {
    return support::Result<PairedPeer>::failure(make_proto("m2.pair.rejected"));
  }

  // Extract peer certificate and SPKI.
  auto cert_info_r = extract_peer_cert_info(ssl);
  if (!cert_info_r.has_value()) {
    return support::Result<PairedPeer>::failure(cert_info_r.error());
  }
  auto& [spki_der, cert_der] = *cert_info_r.value_if();

  // Build PeerId from the remote peer_id field (SHA-256 of SPKI, as asserted by the peer).
  std::array<std::byte, 32> pid_bytes{};
  std::copy(remote_hello.peer_id.begin(), remote_hello.peer_id.end(), pid_bytes.begin());

  PairedPeer peer{support::PeerId{pid_bytes}, std::move(remote_hello.display_name), std::move(spki_der),
                  std::move(cert_der)};

  return support::Result<PairedPeer>::success(std::move(peer));
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────────

Sas derive_sas(const std::array<std::byte, 32>& export_material) {
  // 20 bits: byte[0] = AA (8 bits), upper 12 bits of bytes[1..2] = BBB.
  const unsigned aa = static_cast<unsigned>(export_material[0]);
  const unsigned bbb =
      (static_cast<unsigned>(export_material[1]) << 4U) | (static_cast<unsigned>(export_material[2]) >> 4U);
  Sas sas{};
  (void)std::snprintf(sas.text, sizeof(sas.text), "%02X-%03X", aa, bbb);
  return sas;
}

support::Result<PairedPeer> pair_listen(const identity::DeviceIdentity& identity, const PairListenOptions& opts,
                                        const SasConfirmFn& confirm_fn) {
  // Create TLS context for pairing (listener side).
  auto ctx_r = identity.make_tls_context("spoink-pair/1", false);
  if (!ctx_r.has_value()) return support::Result<PairedPeer>::failure(ctx_r.error());
  ssl_ctx_st* ctx = ctx_r.value_if()->get();

  // Create TCP listener socket.
  const int server_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (server_fd < 0) return support::Result<PairedPeer>::failure(make_net("m2.pair.socket"));

  int on = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  set_socket_timeout(server_fd, opts.timeout_ms);

  struct sockaddr_in6 addr {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons(opts.port);

  if (::bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(server_fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.bind"));
  }
  if (::listen(server_fd, 1) != 0) {
    ::close(server_fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.listen"));
  }

  struct sockaddr_in6 client_addr {};
  socklen_t client_len = sizeof(client_addr);
  const int client_fd = ::accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
  ::close(server_fd);

  if (client_fd < 0) return support::Result<PairedPeer>::failure(make_net("m2.pair.accept"));

  set_socket_timeout(client_fd, opts.timeout_ms);

  // TLS handshake.
  ssl_st* ssl = SSL_new(ctx);
  if (ssl == nullptr) {
    ::close(client_fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.ssl_new"));
  }
  SSL_set_fd(ssl, client_fd);
  if (SSL_accept(ssl) != 1) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.ssl_accept"));
  }

  auto result = run_pairing_exchange(ssl, identity, true, confirm_fn);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  ::close(client_fd);

  return result;
}

support::Result<PairedPeer> pair_connect(const identity::DeviceIdentity& identity, const PairConnectOptions& opts,
                                         const SasConfirmFn& confirm_fn) {
  // Create TLS context for pairing (initiator side).
  auto ctx_r = identity.make_tls_context("spoink-pair/1", false);
  if (!ctx_r.has_value()) return support::Result<PairedPeer>::failure(ctx_r.error());
  ssl_ctx_st* ctx = ctx_r.value_if()->get();

  // Resolve host and connect.
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string port_str = std::to_string(opts.port);
  struct addrinfo* ai = nullptr;
  if (::getaddrinfo(opts.host.c_str(), port_str.c_str(), &hints, &ai) != 0 || ai == nullptr) {
    return support::Result<PairedPeer>::failure(make_net("m2.pair.getaddrinfo"));
  }

  const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(ai);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.socket"));
  }

  set_socket_timeout(fd, opts.timeout_ms);

  if (::connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
    ::freeaddrinfo(ai);
    ::close(fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.connect"));
  }
  ::freeaddrinfo(ai);

  // TLS handshake.
  ssl_st* ssl = SSL_new(ctx);
  if (ssl == nullptr) {
    ::close(fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.ssl_new"));
  }
  SSL_set_fd(ssl, fd);
  if (SSL_connect(ssl) != 1) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<PairedPeer>::failure(make_net("m2.pair.ssl_connect"));
  }

  auto result = run_pairing_exchange(ssl, identity, false, confirm_fn);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  ::close(fd);

  return result;
}

// ── DB operations ─────────────────────────────────────────────────────────────

support::Result<void> save_trusted_peer(sqlite3* db, const PairedPeer& peer) {
  const auto now_ms = static_cast<sqlite3_int64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());

  const char* sql = R"(
    INSERT INTO trusted_peers
      (peer_id, display_name, spki_der, last_certificate_der, paired_at_ms, last_seen_at_ms)
    VALUES (?, ?, ?, ?, ?, ?)
    ON CONFLICT(peer_id) DO UPDATE SET
      display_name = excluded.display_name,
      spki_der = excluded.spki_der,
      last_certificate_der = excluded.last_certificate_der,
      last_seen_at_ms = excluded.last_seen_at_ms,
      revoked_at_ms = NULL
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return support::Result<void>::failure(make_db("m2.pair.db_prepare_save"));
  }

  const auto& pid = peer.peer_id.bytes();
  sqlite3_bind_blob(stmt, 1, pid.data(), static_cast<int>(pid.size()), SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, peer.display_name.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, peer.spki_der.data(), static_cast<int>(peer.spki_der.size()), SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 4, peer.cert_der.data(), static_cast<int>(peer.cert_der.size()), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 5, now_ms);
  sqlite3_bind_int64(stmt, 6, now_ms);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return support::Result<void>::failure(make_db("m2.pair.db_save"));
  }
  return support::Result<void>::success();
}

support::Result<void> revoke_peer(sqlite3* db, const support::PeerId& peer_id) {
  const auto now_ms = static_cast<sqlite3_int64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());

  const char* sql = "UPDATE trusted_peers SET revoked_at_ms = ? WHERE peer_id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return support::Result<void>::failure(make_db("m2.pair.db_prepare_revoke"));
  }
  sqlite3_bind_int64(stmt, 1, now_ms);
  const auto& pid = peer_id.bytes();
  sqlite3_bind_blob(stmt, 2, pid.data(), static_cast<int>(pid.size()), SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return support::Result<void>::failure(make_db("m2.pair.db_revoke"));
  }
  return support::Result<void>::success();
}

support::Result<bool> is_trusted_peer(sqlite3* db, const support::PeerId& peer_id) {
  const char* sql = "SELECT 1 FROM trusted_peers WHERE peer_id = ? AND revoked_at_ms IS NULL LIMIT 1";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return support::Result<bool>::failure(make_db("m2.pair.db_prepare_check"));
  }
  const auto& pid = peer_id.bytes();
  sqlite3_bind_blob(stmt, 1, pid.data(), static_cast<int>(pid.size()), SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) return support::Result<bool>::success(true);
  if (rc == SQLITE_DONE) return support::Result<bool>::success(false);
  return support::Result<bool>::failure(make_db("m2.pair.db_check"));
}

support::Result<std::string> get_peer_display_name(sqlite3* db, const support::PeerId& peer_id) {
  const char* sql = "SELECT display_name FROM trusted_peers WHERE peer_id = ? AND revoked_at_ms IS NULL LIMIT 1";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return support::Result<std::string>::failure(make_db("m2.pair.db_prepare_name"));
  }
  const auto& pid = peer_id.bytes();
  sqlite3_bind_blob(stmt, 1, pid.data(), static_cast<int>(pid.size()), SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return support::Result<std::string>::failure(make_db("m2.pair.db_name_notfound"));
  }
  const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  std::string result = name != nullptr ? std::string{name} : std::string{};
  sqlite3_finalize(stmt);
  return support::Result<std::string>::success(std::move(result));
}

support::Result<std::vector<std::byte>> get_peer_spki_der(sqlite3* db, const support::PeerId& peer_id) {
  const char* sql = "SELECT spki_der FROM trusted_peers WHERE peer_id = ? AND revoked_at_ms IS NULL LIMIT 1";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return support::Result<std::vector<std::byte>>::failure(make_db("m2.pair.db_prepare_spki"));
  }
  const auto& pid = peer_id.bytes();
  sqlite3_bind_blob(stmt, 1, pid.data(), static_cast<int>(pid.size()), SQLITE_STATIC);
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return support::Result<std::vector<std::byte>>::failure(make_db("m2.pair.db_spki_notfound"));
  }
  const void* data = sqlite3_column_blob(stmt, 0);
  const int len = sqlite3_column_bytes(stmt, 0);
  std::vector<std::byte> spki(static_cast<std::size_t>(len));
  if (data != nullptr && len > 0) {
    std::memcpy(spki.data(), data, static_cast<std::size_t>(len));
  }
  sqlite3_finalize(stmt);
  return support::Result<std::vector<std::byte>>::success(std::move(spki));
}

}  // namespace spoink::pairing
