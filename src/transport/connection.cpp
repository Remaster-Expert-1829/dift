#include "spoink/transport/connection.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "spoink/pairing/pairing.h"
#include "spoink/support/error.h"

namespace spoink::transport {

namespace {

support::Error make_net(const char* key) {
  return support::make_error(support::ErrorCode::kResourceFailure, key);
}

support::Error make_auth(const char* key) {
  return support::make_error(support::ErrorCode::kInvalidArgument, key);
}

void set_socket_timeout(int fd, std::uint32_t ms) {
  struct timeval tv {};
  tv.tv_sec = static_cast<time_t>(ms / 1000U);
  tv.tv_usec = static_cast<suseconds_t>((ms % 1000U) * 1000U);
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool ssl_write_all(ssl_st* ssl, const void* data, int n) {
  int remaining = n;
  const auto* ptr = static_cast<const char*>(data);
  while (remaining > 0) {
    const int rc = SSL_write(ssl, ptr, remaining);
    if (rc <= 0) return false;
    ptr += rc;
    remaining -= rc;
  }
  return true;
}

bool ssl_read_all(ssl_st* ssl, void* buf, int n) {
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

}  // namespace

// ── TlsConnection ─────────────────────────────────────────────────────────────

TlsConnection::~TlsConnection() { close(); }

TlsConnection::TlsConnection(TlsConnection&& o) noexcept
    : ssl_(o.ssl_), fd_(o.fd_), peer_id_(o.peer_id_) {
  o.ssl_ = nullptr;
  o.fd_ = -1;
}

TlsConnection& TlsConnection::operator=(TlsConnection&& o) noexcept {
  if (this != &o) {
    close();
    ssl_ = o.ssl_;
    fd_ = o.fd_;
    peer_id_ = o.peer_id_;
    o.ssl_ = nullptr;
    o.fd_ = -1;
  }
  return *this;
}

void TlsConnection::close() {
  if (ssl_ != nullptr) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

support::Result<void> TlsConnection::send_frame(
    const protocol::FramePrefix& prefix,
    const std::vector<std::byte>& header,
    const std::vector<std::byte>& payload) {
  if (ssl_ == nullptr) return support::Result<void>::failure(make_net("m2.transport.not_connected"));
  const auto wire = protocol::encode_prefix(prefix);
  if (!ssl_write_all(ssl_, wire.data(), static_cast<int>(wire.size()))) {
    return support::Result<void>::failure(make_net("m2.transport.write_prefix"));
  }
  if (!header.empty() && !ssl_write_all(ssl_, header.data(), static_cast<int>(header.size()))) {
    return support::Result<void>::failure(make_net("m2.transport.write_header"));
  }
  if (!payload.empty() && !ssl_write_all(ssl_, payload.data(), static_cast<int>(payload.size()))) {
    return support::Result<void>::failure(make_net("m2.transport.write_payload"));
  }
  return support::Result<void>::success();
}

support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>
TlsConnection::read_frame() {
  using R = support::Result<std::pair<protocol::FramePrefix, std::vector<std::byte>>>;
  if (ssl_ == nullptr) return R::failure(make_net("m2.transport.not_connected"));

  std::array<std::byte, protocol::kFramePrefixBytes> raw_prefix{};
  if (!ssl_read_all(ssl_, raw_prefix.data(), static_cast<int>(protocol::kFramePrefixBytes))) {
    return R::failure(make_net("m2.transport.read_prefix"));
  }

  auto pfx_r = protocol::decode_prefix(raw_prefix);
  if (!pfx_r.has_value()) return R::failure(pfx_r.error());

  const auto& pfx = *pfx_r.value_if();
  auto len_r = protocol::validate_prefix_lengths(pfx);
  if (!len_r.has_value()) return R::failure(len_r.error());

  const std::size_t total =
      static_cast<std::size_t>(pfx.header_length) + static_cast<std::size_t>(pfx.payload_length);
  std::vector<std::byte> body(total);
  if (total > 0 && !ssl_read_all(ssl_, body.data(), static_cast<int>(total))) {
    return R::failure(make_net("m2.transport.read_body"));
  }
  return R::success(std::make_pair(pfx, std::move(body)));
}

// ── SPKI pin verification ─────────────────────────────────────────────────────

support::Result<void>
verify_spki_pin(ssl_st* ssl, const std::vector<std::byte>& expected_spki_der) {
  X509* cert = SSL_get_peer_certificate(ssl);
  if (cert == nullptr) {
    return support::Result<void>::failure(make_auth("m2.transport.no_peer_cert"));
  }

  EVP_PKEY* pkey = X509_get0_pubkey(cert);
  unsigned char* actual_der = nullptr;
  const int actual_len = i2d_PUBKEY(pkey, &actual_der);
  X509_free(cert);
  if (actual_len <= 0 || actual_der == nullptr) {
    return support::Result<void>::failure(make_auth("m2.transport.spki_encode"));
  }

  const bool len_match = (static_cast<std::size_t>(actual_len) == expected_spki_der.size());
  int diff = len_match ? 0 : 1;
  if (len_match) {
    // Constant-time comparison to avoid timing oracle.
    diff = CRYPTO_memcmp(actual_der, expected_spki_der.data(),
                         static_cast<std::size_t>(actual_len));
  }
  OPENSSL_free(actual_der);

  if (diff != 0) {
    return support::Result<void>::failure(make_auth("m2.transport.spki_mismatch"));
  }
  return support::Result<void>::success();
}

// ── tls_connect / tls_accept_one ─────────────────────────────────────────────

support::Result<TlsConnection>
tls_connect(const identity::DeviceIdentity& identity,
            const ConnectOptions& opts,
            sqlite3* db) {
  auto ctx_r = identity.make_tls_context("spoink/1", true);
  if (!ctx_r.has_value()) return support::Result<TlsConnection>::failure(ctx_r.error());
  ssl_ctx_st* ctx = ctx_r.value_if()->get();

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string port_str = std::to_string(opts.port);
  struct addrinfo* ai = nullptr;
  if (::getaddrinfo(opts.host.c_str(), port_str.c_str(), &hints, &ai) != 0 || ai == nullptr) {
    return support::Result<TlsConnection>::failure(make_net("m2.transport.getaddrinfo"));
  }

  const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(ai);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.socket"));
  }
  set_socket_timeout(fd, opts.handshake_timeout_ms);
  if (::connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
    ::freeaddrinfo(ai);
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.connect"));
  }
  ::freeaddrinfo(ai);

  ssl_st* ssl = SSL_new(ctx);
  if (ssl == nullptr) {
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.ssl_new"));
  }
  SSL_set_fd(ssl, fd);
  if (SSL_connect(ssl) != 1) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.ssl_connect"));
  }

  // Verify peer SPKI pin against trust store.
  X509* peer_cert = SSL_get_peer_certificate(ssl);
  if (peer_cert == nullptr) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.no_peer_cert"));
  }
  EVP_PKEY* peer_pkey = X509_get0_pubkey(peer_cert);
  unsigned char* spki_der = nullptr;
  const int spki_len = i2d_PUBKEY(peer_pkey, &spki_der);
  X509_free(peer_cert);

  if (spki_len <= 0 || spki_der == nullptr) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.peer_spki"));
  }

  // Compute peer_id from SPKI DER to look up in trust store.
  std::array<unsigned char, 32> hash{};
  {
    unsigned int len = 32;
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, spki_der, static_cast<std::size_t>(spki_len));
    EVP_DigestFinal_ex(mdctx, hash.data(), &len);
    EVP_MD_CTX_free(mdctx);
  }

  std::array<std::byte, 32> pid_bytes{};
  for (std::size_t i = 0; i < 32; ++i) pid_bytes[i] = static_cast<std::byte>(hash[i]);
  const support::PeerId peer_id{pid_bytes};

  // Check trust store.
  auto trusted_r = pairing::is_trusted_peer(db, peer_id);
  if (!trusted_r.has_value() || !*trusted_r.value_if()) {
    OPENSSL_free(spki_der);
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.untrusted_peer"));
  }

  // Retrieve stored SPKI to verify pin.
  auto stored_spki_r = pairing::get_peer_spki_der(db, peer_id);
  OPENSSL_free(spki_der);
  if (!stored_spki_r.has_value()) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(stored_spki_r.error());
  }

  auto pin_r = verify_spki_pin(ssl, *stored_spki_r.value_if());
  if (!pin_r.has_value()) {
    SSL_free(ssl);
    ::close(fd);
    return support::Result<TlsConnection>::failure(pin_r.error());
  }

  set_socket_timeout(fd, opts.idle_timeout_ms);
  return support::Result<TlsConnection>::success(TlsConnection{ssl, fd, peer_id});
}

support::Result<std::uint16_t>
get_bound_port(int server_fd) {
  struct sockaddr_in6 addr {};
  socklen_t len = sizeof(addr);
  if (::getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
    return support::Result<std::uint16_t>::failure(make_net("m2.transport.getsockname"));
  }
  return support::Result<std::uint16_t>::success(ntohs(addr.sin6_port));
}

support::Result<TlsConnection>
tls_accept_one(const identity::DeviceIdentity& identity,
               const ListenOptions& opts,
               sqlite3* db) {
  auto ctx_r = identity.make_tls_context("spoink/1", true);
  if (!ctx_r.has_value()) return support::Result<TlsConnection>::failure(ctx_r.error());
  ssl_ctx_st* ctx = ctx_r.value_if()->get();

  const int server_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
  if (server_fd < 0) return support::Result<TlsConnection>::failure(make_net("m2.transport.socket"));

  int on = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  set_socket_timeout(server_fd, opts.handshake_timeout_ms);

  struct sockaddr_in6 addr {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons(opts.port);

  if (::bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(server_fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.bind"));
  }
  if (::listen(server_fd, 8) != 0) {
    ::close(server_fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.listen"));
  }

  struct sockaddr_in6 client_addr {};
  socklen_t client_len = sizeof(client_addr);
  const int client_fd = ::accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
  ::close(server_fd);
  if (client_fd < 0) return support::Result<TlsConnection>::failure(make_net("m2.transport.accept"));

  set_socket_timeout(client_fd, opts.handshake_timeout_ms);

  ssl_st* ssl = SSL_new(ctx);
  if (ssl == nullptr) {
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.ssl_new"));
  }
  SSL_set_fd(ssl, client_fd);
  if (SSL_accept(ssl) != 1) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(make_net("m2.transport.ssl_accept"));
  }

  // Extract peer cert and compute peer_id.
  X509* peer_cert = SSL_get_peer_certificate(ssl);
  if (peer_cert == nullptr) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.no_peer_cert"));
  }
  EVP_PKEY* peer_pkey = X509_get0_pubkey(peer_cert);
  unsigned char* spki_der = nullptr;
  const int spki_len = i2d_PUBKEY(peer_pkey, &spki_der);
  X509_free(peer_cert);
  if (spki_len <= 0 || spki_der == nullptr) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.peer_spki"));
  }

  std::array<unsigned char, 32> hash{};
  {
    unsigned int len = 32;
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, spki_der, static_cast<std::size_t>(spki_len));
    EVP_DigestFinal_ex(mdctx, hash.data(), &len);
    EVP_MD_CTX_free(mdctx);
  }

  std::array<std::byte, 32> pid_bytes{};
  for (std::size_t i = 0; i < 32; ++i) pid_bytes[i] = static_cast<std::byte>(hash[i]);
  const support::PeerId peer_id{pid_bytes};

  auto trusted_r = pairing::is_trusted_peer(db, peer_id);
  if (!trusted_r.has_value() || !*trusted_r.value_if()) {
    OPENSSL_free(spki_der);
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(make_auth("m2.transport.untrusted_peer"));
  }

  auto stored_spki_r = pairing::get_peer_spki_der(db, peer_id);
  OPENSSL_free(spki_der);
  if (!stored_spki_r.has_value()) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(stored_spki_r.error());
  }

  auto pin_r = verify_spki_pin(ssl, *stored_spki_r.value_if());
  if (!pin_r.has_value()) {
    SSL_free(ssl);
    ::close(client_fd);
    return support::Result<TlsConnection>::failure(pin_r.error());
  }

  set_socket_timeout(client_fd, opts.idle_timeout_ms);
  return support::Result<TlsConnection>::success(TlsConnection{ssl, client_fd, peer_id});
}

}  // namespace spoink::transport
