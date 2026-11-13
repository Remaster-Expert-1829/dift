#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "spoink/support/openssl_raii.h"
#include "spoink/support/result.h"
#include "spoink/support/types.h"

namespace spoink::identity {

// Loaded device identity. Move-only; holds OpenSSL resources.
class DeviceIdentity {
 public:
  DeviceIdentity() = default;
  ~DeviceIdentity() = default;
  DeviceIdentity(const DeviceIdentity&) = delete;
  DeviceIdentity& operator=(const DeviceIdentity&) = delete;
  DeviceIdentity(DeviceIdentity&&) noexcept = default;
  DeviceIdentity& operator=(DeviceIdentity&&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept { return pkey_ != nullptr; }
  [[nodiscard]] const support::PeerId& peer_id() const noexcept { return peer_id_; }
  [[nodiscard]] const std::string& display_name() const noexcept { return display_name_; }
  [[nodiscard]] evp_pkey_st* pkey() const noexcept { return pkey_.get(); }
  [[nodiscard]] x509_st* cert() const noexcept { return cert_.get(); }

  // Build a fresh TLS 1.3 context (ed25519 only, no 0-RTT) for this identity.
  // alpn: "spoink-pair/1" for pairing, "spoink/1" for normal.
  // require_peer_cert: true for mTLS (normal), false for pairing mode.
  [[nodiscard]] support::Result<support::SslCtxPtr> make_tls_context(std::string_view alpn,
                                                                     bool require_peer_cert) const;

 private:
  friend support::Result<DeviceIdentity> load_or_create_identity(const std::filesystem::path& state_root,
                                                                  std::string_view display_name);

  explicit DeviceIdentity(support::EvpPkeyPtr pkey, support::X509Ptr cert, support::PeerId peer_id,
                          std::string display_name)
      : pkey_(std::move(pkey)),
        cert_(std::move(cert)),
        peer_id_(std::move(peer_id)),
        display_name_(std::move(display_name)) {}

  support::EvpPkeyPtr pkey_;
  support::X509Ptr cert_;
  support::PeerId peer_id_{std::array<std::byte, 32>{}};
  std::string display_name_;
};

// Load existing identity or create new Ed25519 identity if absent.
// state_root: directory; identity stored under state_root/identity/.
// display_name: used as X.509 CN on creation; ignored on load.
[[nodiscard]] support::Result<DeviceIdentity> load_or_create_identity(const std::filesystem::path& state_root,
                                                                       std::string_view display_name);

// Compute SHA-256 fingerprint of SPKI DER as peer ID.
[[nodiscard]] support::Result<support::PeerId> compute_peer_id(evp_pkey_st* pkey);

// Verify key file has mode 0600 (owner rw only). Returns error if not.
[[nodiscard]] support::Result<void> verify_key_permissions(const std::filesystem::path& key_path);

}  // namespace spoink::identity
