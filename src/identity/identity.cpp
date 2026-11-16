#include "spoink/identity/identity.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "spoink/support/error.h"

namespace spoink::identity {

namespace {

support::Error make_resource(const char* key) { return support::make_error(support::ErrorCode::kResourceFailure, key); }

support::Error make_invalid(const char* key) { return support::make_error(support::ErrorCode::kInvalidArgument, key); }

// Static ALPN strings that outlive any SSL_CTX.
const std::string& alpn_pair_str() {
  static const std::string s{"spoink-pair/1"};
  return s;
}
const std::string& alpn_normal_str() {
  static const std::string s{"spoink/1"};
  return s;
}

// ALPN selection callback for the server side.
int alpn_select_cb(ssl_st* /*ssl*/, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                   unsigned int inlen, void* arg) {
  const auto* expected = static_cast<const std::string*>(arg);
  unsigned int pos = 0;
  while (pos < inlen) {
    if (pos >= inlen) break;
    const unsigned int plen = in[pos];
    ++pos;
    if (pos + plen > inlen) return SSL_TLSEXT_ERR_NOACK;
    if (plen == expected->size() && std::memcmp(in + pos, expected->data(), plen) == 0) {
      *out = in + pos;
      *outlen = static_cast<unsigned char>(plen);
      return SSL_TLSEXT_ERR_OK;
    }
    pos += plen;
  }
  return SSL_TLSEXT_ERR_NOACK;
}

// Permissive verify callback: TLS layer accepts any self-signed cert.
// Pin verification is done at application level.
int permissive_verify_cb(int /*ok*/, X509_STORE_CTX* /*ctx*/) { return 1; }

support::Result<support::EvpPkeyPtr> generate_ed25519_key() {
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (ctx == nullptr) {
    return support::Result<support::EvpPkeyPtr>::failure(make_resource("m2.identity.ctx_new"));
  }
  if (EVP_PKEY_keygen_init(ctx) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    return support::Result<support::EvpPkeyPtr>::failure(make_resource("m2.identity.keygen_init"));
  }
  EVP_PKEY* pkey = nullptr;
  if (EVP_PKEY_keygen(ctx, &pkey) <= 0 || pkey == nullptr) {
    EVP_PKEY_CTX_free(ctx);
    return support::Result<support::EvpPkeyPtr>::failure(make_resource("m2.identity.keygen"));
  }
  EVP_PKEY_CTX_free(ctx);
  return support::Result<support::EvpPkeyPtr>::success(support::EvpPkeyPtr{pkey});
}

support::Result<support::X509Ptr> create_self_signed_cert(evp_pkey_st* pkey, std::string_view display_name) {
  X509* cert = X509_new();
  if (cert == nullptr) {
    return support::Result<support::X509Ptr>::failure(make_resource("m2.identity.x509_new"));
  }
  support::X509Ptr cert_ptr{cert};

  X509_set_version(cert, 2);  // X.509v3

  ASN1_INTEGER* serial = X509_get_serialNumber(cert);
  ASN1_INTEGER_set(serial, 1);

  // Validity: 10 years from now
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), static_cast<long>(10) * 365 * 24 * 3600);  // NOLINT(runtime/int)

  X509_NAME* name = X509_get_subject_name(cert);
  const std::string dn{display_name};
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(dn.c_str()),
                             static_cast<int>(dn.size()), -1, 0);
  X509_set_issuer_name(cert, name);

  if (X509_set_pubkey(cert, pkey) <= 0) {
    return support::Result<support::X509Ptr>::failure(make_resource("m2.identity.set_pubkey"));
  }
  // Ed25519 sign: EVP_MD is nullptr for Ed25519
  if (X509_sign(cert, pkey, nullptr) <= 0) {
    return support::Result<support::X509Ptr>::failure(make_resource("m2.identity.sign"));
  }
  return support::Result<support::X509Ptr>::success(std::move(cert_ptr));
}

support::Result<support::EvpPkeyPtr> load_private_key(const std::filesystem::path& key_path) {
  FILE* fp = std::fopen(key_path.c_str(), "r");
  if (fp == nullptr) {
    return support::Result<support::EvpPkeyPtr>::failure(make_resource("m2.identity.key_open"));
  }
  EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
  (void)std::fclose(fp);
  if (pkey == nullptr) {
    return support::Result<support::EvpPkeyPtr>::failure(make_resource("m2.identity.key_read"));
  }
  if (EVP_PKEY_get_id(pkey) != EVP_PKEY_ED25519) {
    EVP_PKEY_free(pkey);
    return support::Result<support::EvpPkeyPtr>::failure(make_invalid("m2.identity.wrong_key_type"));
  }
  return support::Result<support::EvpPkeyPtr>::success(support::EvpPkeyPtr{pkey});
}

support::Result<support::X509Ptr> load_certificate(const std::filesystem::path& cert_path) {
  FILE* fp = std::fopen(cert_path.c_str(), "r");
  if (fp == nullptr) {
    return support::Result<support::X509Ptr>::failure(make_resource("m2.identity.cert_open"));
  }
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  (void)std::fclose(fp);
  if (cert == nullptr) {
    return support::Result<support::X509Ptr>::failure(make_resource("m2.identity.cert_read"));
  }
  return support::Result<support::X509Ptr>::success(support::X509Ptr{cert});
}

}  // namespace

support::Result<support::PeerId> compute_peer_id(evp_pkey_st* pkey) {
  unsigned char* der = nullptr;
  const int der_len = i2d_PUBKEY(pkey, &der);
  if (der_len <= 0 || der == nullptr) {
    return support::Result<support::PeerId>::failure(make_resource("m2.identity.spki_der"));
  }
  std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
  SHA256(der, static_cast<std::size_t>(der_len), hash.data());
  OPENSSL_free(der);

  std::array<std::byte, 32> bytes{};
  for (std::size_t i = 0; i < 32; ++i) bytes[i] = static_cast<std::byte>(hash[i]);
  return support::Result<support::PeerId>::success(support::PeerId{bytes});
}

support::Result<void> verify_key_permissions(const std::filesystem::path& key_path) {
  struct stat sb {};
  if (::stat(key_path.c_str(), &sb) != 0) {
    return support::Result<void>::failure(make_resource("m2.identity.stat"));
  }
  if ((sb.st_mode & 0177U) != 0U) {
    return support::Result<void>::failure(make_invalid("m2.identity.perm"));
  }
  return support::Result<void>::success();
}

support::Result<DeviceIdentity> load_or_create_identity(const std::filesystem::path& state_root,
                                                        std::string_view display_name) {
  const auto identity_dir = state_root / "identity";
  const auto key_path = identity_dir / "device.key";
  const auto cert_path = identity_dir / "device.crt";

  std::error_code ec;
  const bool key_exists = std::filesystem::exists(key_path, ec);
  const bool cert_exists = std::filesystem::exists(cert_path, ec);

  support::EvpPkeyPtr pkey;
  support::X509Ptr cert;

  if (!key_exists || !cert_exists) {
    std::filesystem::create_directories(identity_dir, ec);
    if (ec) return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.mkdir"));

    auto key_result = generate_ed25519_key();
    if (!key_result.has_value()) return support::Result<DeviceIdentity>::failure(key_result.error());
    pkey = std::move(*key_result.value_if());

    auto cert_result = create_self_signed_cert(pkey.get(), display_name);
    if (!cert_result.has_value()) return support::Result<DeviceIdentity>::failure(cert_result.error());
    cert = std::move(*cert_result.value_if());

    // Write private key with mode 0600 — use open(2) to set permissions atomically.
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      const int fd = ::open(key_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd < 0) return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.key_create"));
      FILE* fp = ::fdopen(fd, "w");
      if (fp == nullptr) {
        ::close(fd);
        return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.key_fdopen"));
      }
      const int rc = PEM_write_PrivateKey(fp, pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
      (void)std::fclose(fp);
      if (rc <= 0) return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.key_write"));
    }

    {
      FILE* fp = std::fopen(cert_path.c_str(), "w");
      if (fp == nullptr) return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.cert_create"));
      const int rc = PEM_write_X509(fp, cert.get());
      (void)std::fclose(fp);
      if (rc <= 0) return support::Result<DeviceIdentity>::failure(make_resource("m2.identity.cert_write"));
    }
  } else {
    auto perm_result = verify_key_permissions(key_path);
    if (!perm_result.has_value()) return support::Result<DeviceIdentity>::failure(perm_result.error());

    auto key_result = load_private_key(key_path);
    if (!key_result.has_value()) return support::Result<DeviceIdentity>::failure(key_result.error());
    pkey = std::move(*key_result.value_if());

    auto cert_result = load_certificate(cert_path);
    if (!cert_result.has_value()) return support::Result<DeviceIdentity>::failure(cert_result.error());
    cert = std::move(*cert_result.value_if());
  }

  auto peer_id_result = compute_peer_id(pkey.get());
  if (!peer_id_result.has_value()) return support::Result<DeviceIdentity>::failure(peer_id_result.error());

  std::string loaded_name{display_name};
  X509_NAME* subj = X509_get_subject_name(cert.get());
  if (subj != nullptr) {
    char buf[256] = {};
    if (X509_NAME_get_text_by_NID(subj, NID_commonName, buf, static_cast<int>(sizeof(buf))) > 0) {
      loaded_name = std::string{buf};
    }
  }

  return support::Result<DeviceIdentity>::success(
      DeviceIdentity{std::move(pkey), std::move(cert), *peer_id_result.value_if(), std::move(loaded_name)});
}

support::Result<support::SslCtxPtr> DeviceIdentity::make_tls_context(std::string_view alpn,
                                                                     bool require_peer_cert) const {
  // Map alpn to static storage so the callback pointer remains valid.
  const std::string* alpn_ptr = nullptr;
  if (alpn == alpn_pair_str()) {
    alpn_ptr = &alpn_pair_str();
  } else if (alpn == alpn_normal_str()) {
    alpn_ptr = &alpn_normal_str();
  } else {
    return support::Result<support::SslCtxPtr>::failure(make_invalid("m2.identity.alpn_unknown"));
  }

  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
  if (ctx == nullptr) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.ssl_ctx_new"));
  }
  support::SslCtxPtr ctx_ptr{ctx};

  SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_early_data(ctx, 0);
  SSL_CTX_set_num_tickets(ctx, 0);
  SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

  if (SSL_CTX_set1_sigalgs_list(ctx, "ed25519") != 1) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.sigalgs"));
  }

  if (SSL_CTX_use_certificate(ctx, cert_.get()) != 1) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.use_cert"));
  }
  if (SSL_CTX_use_PrivateKey(ctx, pkey_.get()) != 1) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.use_key"));
  }
  if (SSL_CTX_check_private_key(ctx) != 1) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.check_key"));
  }

  const int verify_flags = require_peer_cert ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_PEER;
  SSL_CTX_set_verify(ctx, verify_flags, permissive_verify_cb);

  // Client side: advertise ALPN.
  std::vector<unsigned char> alpn_wire;
  alpn_wire.push_back(static_cast<unsigned char>(alpn_ptr->size()));
  for (char c : *alpn_ptr) alpn_wire.push_back(static_cast<unsigned char>(c));
  if (SSL_CTX_set_alpn_protos(ctx, alpn_wire.data(), static_cast<unsigned int>(alpn_wire.size())) != 0) {
    return support::Result<support::SslCtxPtr>::failure(make_resource("m2.identity.alpn_client"));
  }
  // Server side: select ALPN. alpn_ptr points to a static; safe as callback arg.
  SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb,
                             // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                             const_cast<void*>(static_cast<const void*>(alpn_ptr)));

  return support::Result<support::SslCtxPtr>::success(std::move(ctx_ptr));
}

}  // namespace spoink::identity

// Deleter implementations (must be in a TU that includes openssl headers).
namespace spoink::support {
void EvpPkeyDeleter::operator()(evp_pkey_st* p) const noexcept { EVP_PKEY_free(p); }
void X509Deleter::operator()(x509_st* p) const noexcept { X509_free(p); }
void SslCtxDeleter::operator()(ssl_ctx_st* p) const noexcept { SSL_CTX_free(p); }
void SslDeleter::operator()(ssl_st* p) const noexcept { SSL_free(p); }
}  // namespace spoink::support
