-- Spoink M1 schema 1. Metadata only: payload bytes remain in chunks/.
-- Applied as one SQLite transaction by MetadataStore::open.
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS schema_migrations (
  version INTEGER PRIMARY KEY CHECK(version > 0), applied_at_ms INTEGER NOT NULL,
  application_version TEXT NOT NULL, checksum BLOB NOT NULL CHECK(length(checksum) = 32)
);
CREATE TABLE IF NOT EXISTS trusted_peers (
  peer_id BLOB PRIMARY KEY CHECK(length(peer_id) = 32), display_name TEXT NOT NULL,
  spki_der BLOB NOT NULL, last_certificate_der BLOB NOT NULL, paired_at_ms INTEGER NOT NULL,
  last_seen_at_ms INTEGER, revoked_at_ms INTEGER, notes TEXT
);
CREATE TABLE IF NOT EXISTS transfer_sessions (
  transfer_id BLOB PRIMARY KEY CHECK(length(transfer_id) = 16),
  peer_id BLOB NOT NULL REFERENCES trusted_peers(peer_id), local_role TEXT NOT NULL CHECK(local_role IN ('SENDER','RECEIVER')),
  protocol_major INTEGER NOT NULL, protocol_minor INTEGER NOT NULL, manifest_schema INTEGER NOT NULL,
  manifest_digest BLOB NOT NULL CHECK(length(manifest_digest) = 32), manifest_cbor BLOB,
  destination_root TEXT, overwrite_allowed INTEGER NOT NULL CHECK(overwrite_allowed IN (0,1)),
  state TEXT NOT NULL, created_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT NULL,
  retained_until_ms INTEGER, terminal_error_code TEXT
);
CREATE TABLE IF NOT EXISTS chunk_catalog (
  chunk_hash BLOB PRIMARY KEY CHECK(length(chunk_hash) = 32), plaintext_length INTEGER NOT NULL CHECK(plaintext_length > 0),
  state TEXT NOT NULL CHECK(state IN ('MISSING','INSTALLING','VERIFIED','QUARANTINED')),
  store_relpath TEXT UNIQUE, manifest_ref_count INTEGER NOT NULL DEFAULT 0 CHECK(manifest_ref_count >= 0),
  created_at_ms INTEGER NOT NULL, verified_at_ms INTEGER, last_access_at_ms INTEGER, quarantine_reason TEXT
);
CREATE TABLE IF NOT EXISTS transfer_files (
  transfer_id BLOB NOT NULL REFERENCES transfer_sessions(transfer_id) ON DELETE CASCADE,
  file_id INTEGER NOT NULL CHECK(file_id >= 0), relative_path TEXT NOT NULL, logical_size INTEGER NOT NULL CHECK(logical_size >= 0),
  file_sha256 BLOB NOT NULL CHECK(length(file_sha256) = 32), accepted INTEGER NOT NULL CHECK(accepted IN (0,1)),
  state TEXT NOT NULL, staging_path TEXT, final_path TEXT, terminal_error_code TEXT, completed_at_ms INTEGER,
  PRIMARY KEY(transfer_id,file_id), UNIQUE(transfer_id,relative_path)
);
CREATE TABLE IF NOT EXISTS file_chunk_refs (
  transfer_id BLOB NOT NULL, file_id INTEGER NOT NULL, ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
  chunk_hash BLOB NOT NULL REFERENCES chunk_catalog(chunk_hash), file_offset INTEGER NOT NULL CHECK(file_offset >= 0),
  chunk_length INTEGER NOT NULL CHECK(chunk_length > 0), PRIMARY KEY(transfer_id,file_id,ordinal),
  UNIQUE(transfer_id,file_id,file_offset), FOREIGN KEY(transfer_id,file_id) REFERENCES transfer_files(transfer_id,file_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS session_chunks (
  transfer_id BLOB NOT NULL REFERENCES transfer_sessions(transfer_id) ON DELETE CASCADE,
  chunk_hash BLOB NOT NULL REFERENCES chunk_catalog(chunk_hash), state TEXT NOT NULL CHECK(state IN ('NEEDED','IN_FLIGHT','VERIFIED','FAILED')),
  attempt_count INTEGER NOT NULL DEFAULT 0 CHECK(attempt_count >= 0), last_error_code TEXT, verified_at_ms INTEGER,
  PRIMARY KEY(transfer_id,chunk_hash)
);
CREATE TABLE IF NOT EXISTS chunk_leases (
  lease_id BLOB PRIMARY KEY CHECK(length(lease_id) = 16), chunk_hash BLOB NOT NULL REFERENCES chunk_catalog(chunk_hash),
  owner_transfer_id BLOB REFERENCES transfer_sessions(transfer_id), purpose TEXT NOT NULL CHECK(purpose IN ('RECONSTRUCT','SEND','VERIFY','GC')),
  process_instance_id BLOB NOT NULL, expires_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS chunk_catalog_gc ON chunk_catalog(manifest_ref_count,state);
CREATE INDEX IF NOT EXISTS file_chunk_refs_hash ON file_chunk_refs(chunk_hash);
CREATE INDEX IF NOT EXISTS chunk_leases_expiry ON chunk_leases(chunk_hash,expires_at_ms);
INSERT OR IGNORE INTO schema_migrations(version,applied_at_ms,application_version,checksum)
VALUES(1,0,'m1',X'0000000000000000000000000000000000000000000000000000000000000000');
