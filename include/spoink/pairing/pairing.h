#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "spoink/identity/identity.h"
#include "spoink/support/result.h"
#include "spoink/support/types.h"

struct sqlite3;

namespace spoink::pairing {

// Short Authentication String: "AA-BBB" (5 hex chars, 20 bits from TLS exporter).
struct Sas {
  char text[7];  // "AA-BBB\0"
};

// Derives SAS from the first 20 bits of the 32-byte TLS exporter output.
[[nodiscard]] Sas derive_sas(const std::array<std::byte, 32>& export_material);

// A successfully paired peer (before DB persistence).
struct PairedPeer {
  support::PeerId peer_id;
  std::string display_name;
  std::vector<std::byte> spki_der;    // SPKI DER of peer's Ed25519 public key
  std::vector<std::byte> cert_der;    // DER of peer's self-signed certificate
};

struct PairListenOptions {
  std::uint16_t port{0};          // 0 → OS-assigned
  std::uint32_t timeout_ms{60000};
};

struct PairConnectOptions {
  std::string host;
  std::uint16_t port{0};
  std::uint32_t timeout_ms{60000};
};

// Called with the derived SAS and the peer's display name so the caller can
// display it and obtain the user's confirmation. Return true to accept, false
// to reject. If this throws, pairing is treated as rejected.
using SasConfirmFn = std::function<bool(const Sas& sas, const std::string& peer_name)>;

// Open a one-shot TLS pairing listener, perform the full spoink-pair/1 protocol,
// and return the paired peer. Accepts exactly one connection.
[[nodiscard]] support::Result<PairedPeer>
pair_listen(const identity::DeviceIdentity& identity,
            const PairListenOptions& opts,
            const SasConfirmFn& confirm_fn);

// Connect to a pairing listener and perform the full spoink-pair/1 protocol.
[[nodiscard]] support::Result<PairedPeer>
pair_connect(const identity::DeviceIdentity& identity,
             const PairConnectOptions& opts,
             const SasConfirmFn& confirm_fn);

// Persist a newly paired peer into the trusted_peers table.
// Upserts (updates last_certificate_der and last_seen_at_ms on duplicate peer_id).
[[nodiscard]] support::Result<void>
save_trusted_peer(sqlite3* db, const PairedPeer& peer);

// Mark a peer revoked (sets revoked_at_ms, leaves the row for audit).
[[nodiscard]] support::Result<void>
revoke_peer(sqlite3* db, const support::PeerId& peer_id);

// Returns true iff the peer exists and has not been revoked.
[[nodiscard]] support::Result<bool>
is_trusted_peer(sqlite3* db, const support::PeerId& peer_id);

// Returns the display name of a trusted peer.
[[nodiscard]] support::Result<std::string>
get_peer_display_name(sqlite3* db, const support::PeerId& peer_id);

// Returns the SPKI DER of a trusted peer (used for mTLS pin verification).
[[nodiscard]] support::Result<std::vector<std::byte>>
get_peer_spki_der(sqlite3* db, const support::PeerId& peer_id);

}  // namespace spoink::pairing
