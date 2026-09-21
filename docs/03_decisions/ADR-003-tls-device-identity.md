# ADR-003 — TLS Device Identity and Pairing

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

Encryption without peer authentication permits interception. Custom per-chunk AES-GCM would require secure key exchange, nonce uniqueness, replay rules, and authenticated metadata. Direct peers need a practical way to establish trust without accounts or a central certificate authority.

## Decision

All connections use TLS 1.3. Each device generates and stores a persistent private key and self-signed certificate. The stable peer ID is the SHA-256 fingerprint of the certificate public key's canonical DER SubjectPublicKeyInfo.

First pairing runs in an explicit restricted mode. Both peers present certificates, but provisional certificates are not trusted for application operations. After the handshake, both display a 20-bit SAS derived from TLS exporter material and the two ordered peer IDs. Users confirm the same SAS on both endpoints. Only then are peer IDs pinned. Pairing permits one handshake per explicit attempt; retry requires a new attempt.

Normal connections require mutual TLS and a pinned, non-revoked peer ID. A new key requires explicit re-pairing. Custom chunk encryption is not used in v1; receiver-local chunks are plaintext under the trusted-device assumption.

## Alternatives considered

- **Plain TCP:** rejected; no confidentiality or authentication.
- **Server-only TLS plus password/token:** requires credential distribution and preserves asymmetric server roles.
- **Public CA certificates:** poor fit for local devices addressed by IP and no external service.
- **Trust on first use without comparison:** vulnerable to first-connection interception.
- **Custom AES-GCM protocol:** duplicates solved transport-security problems and increases cryptographic risk.

## Consequences

- OpenSSL-compatible TLS 1.3 and exporter APIs are required.
- Pairing is interactive and needs both terminals visible.
- Loss of identity key requires re-pairing.
- Private-key permissions, revocation, fingerprint display, and clock/certificate policy must be documented.
- SAS reduces but does not remove human comparison error; one explicit attempt and telemetry limit online guessing.

## Migration implications

A managed CA, account system, hardware key, or non-interactive enrollment requires a superseding ADR. At-rest or end-to-end encryption against the receiver is a separate data model, not a TLS configuration toggle.
