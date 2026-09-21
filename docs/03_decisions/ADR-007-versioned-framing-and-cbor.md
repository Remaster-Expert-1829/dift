# ADR-007 — Versioned Framing and Deterministic CBOR

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

The protocol must carry bounded control metadata, canonical manifests, and raw/compressed chunk payloads. Ad-hoc native structs are not portable across compilers or byte order. Plain JSON is inspectable but makes binary hashes/payloads and canonical manifest hashing awkward. A large schema compiler dependency is not otherwise needed.

## Decision

Every application message uses a fixed 32-byte network-byte-order prefix, a deterministic CBOR header map, and an optional raw payload. Protocol major/minor fields and message types are explicit. Manifests use RFC 8949 deterministic CBOR, and their SHA-256 digest covers the exact canonical bytes.

Decoding is schema-aware and limit-first. Unknown required fields/messages fail; explicitly optional minor-version extensions can be ignored only under the wire rules. Native C++ memory layout is never placed on the wire.

## Alternatives considered

- **Native binary structs:** rejected for padding, endianness, and compatibility risk.
- **JSON plus base64:** easier debugging but larger, slower, and requires a separate canonicalization rule.
- **Protocol Buffers/FlatBuffers/Cap'n Proto:** strong schemas but add code generation/runtime/toolchain surface not otherwise justified for v1.
- **Custom TLV for every message:** avoids a CBOR dependency but increases parser and canonicalization code.

## Consequences

- A vetted CBOR implementation or narrowly scoped internal encoder/decoder is required and fuzzed.
- Deterministic encoding rules and golden vectors become compatibility tests.
- Debug tooling should decode frames to redacted JSON-like output rather than changing the wire format.
- Parser safety ceilings and negotiated limits are mandatory.

## Migration implications

Breaking prefix or semantic changes require protocol major increment. Additive messages/fields can use a negotiated minor version only under the compatibility rules. Manifest schema versions are independent but cannot exceed the negotiated protocol's supported range.
