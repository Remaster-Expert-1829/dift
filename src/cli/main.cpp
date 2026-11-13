#include <openssl/rand.h>
#include <sqlite3.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "spoink/identity/identity.h"
#include "spoink/offline/offline.h"
#include "spoink/pairing/pairing.h"
#include "spoink/protocol/codec.h"
#include "spoink/protocol/framing.h"
#include "spoink/session/state_machine.h"
#include "spoink/support/result.h"
#include "spoink/support/types.h"
#include "spoink/transport/connection.h"

#ifndef SPOINK_VERSION
#define SPOINK_VERSION "0.0.0"
#endif

namespace {

// ── State-root helpers ────────────────────────────────────────────────────────

std::filesystem::path default_state_root() {
  const char* xdg = std::getenv("XDG_DATA_HOME");  // NOLINT(concurrency-mt-unsafe)
  if (xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path{xdg} / "spoink";
  }
  const char* home = std::getenv("HOME");  // NOLINT(concurrency-mt-unsafe)
  if (home != nullptr && home[0] != '\0') {
    return std::filesystem::path{home} / ".local" / "share" / "spoink";
  }
  return std::filesystem::current_path() / ".spoink";
}

std::filesystem::path store_root(const std::filesystem::path& state_root) { return state_root / "store"; }

// ── Peers DB helpers ──────────────────────────────────────────────────────────

static constexpr const char* kCreateTrustedPeers = R"(
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

sqlite3* open_peers_db(const std::filesystem::path& state_root) {
  std::error_code ec;
  std::filesystem::create_directories(state_root, ec);

  const auto db_path = state_root / "peers.sqlite3";
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    std::cerr << "error: cannot open peers DB: " << db_path << '\n';
    return nullptr;
  }
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  char* errmsg = nullptr;
  if (sqlite3_exec(db, kCreateTrustedPeers, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    std::cerr << "error: cannot init peers schema: " << (errmsg ? errmsg : "?") << '\n';
    sqlite3_free(errmsg);
    sqlite3_close(db);
    return nullptr;
  }
  return db;
}

// ── Hex helpers ───────────────────────────────────────────────────────────────

std::string to_hex(const std::array<std::byte, 32>& bytes) {
  static constexpr const char* hex_table = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (auto b : bytes) {
    out += hex_table[(static_cast<unsigned>(b) >> 4U) & 0xFU];
    out += hex_table[static_cast<unsigned>(b) & 0xFU];
  }
  return out;
}

// ── Current timestamp ────────────────────────────────────────────────────────

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// ── Session I/O helpers ───────────────────────────────────────────────────────

// Split read_frame body into header + payload using the prefix lengths.
void split_body(const spoink::protocol::FramePrefix& pfx, std::vector<std::byte>& body,
                std::vector<std::byte>& header_out, std::vector<std::byte>& payload_out) {
  const auto hlen = static_cast<std::size_t>(pfx.header_length);
  header_out.assign(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(hlen));
  payload_out.assign(body.begin() + static_cast<std::ptrdiff_t>(hlen), body.end());
}

// Send all EffSendFrame effects. Returns false on first send error or EffCloseConn.
bool flush_send_frames(const std::vector<spoink::session::Effect>& effects, spoink::transport::TlsConnection& conn) {
  for (const auto& eff : effects) {
    if (const auto* e = std::get_if<spoink::session::EffSendFrame>(&eff)) {
      if (!conn.send_frame(e->prefix, e->header, e->payload).has_value()) {
        return false;
      }
    } else if (std::get_if<spoink::session::EffCloseConn>(&eff)) {
      conn.close();
      return false;
    } else if (const auto* e2 = std::get_if<spoink::session::EffProtocolError>(&eff)) {
      std::cerr << "protocol error: " << e2->code << '\n';
      conn.close();
      return false;
    }
  }
  return true;
}

// ── HELLO handshake (both roles) ──────────────────────────────────────────────
// Returns true iff both HELLOs exchanged and SM is in kReady.
bool do_hello_handshake(spoink::session::SessionStateMachine& sm, spoink::transport::TlsConnection& conn) {
  auto start_effects = sm.start();
  if (!flush_send_frames(start_effects, conn)) return false;

  // Read peer HELLO
  auto frame_r = conn.read_frame();
  if (!frame_r.has_value()) {
    std::cerr << "error: failed to read HELLO frame\n";
    return false;
  }
  auto [pfx, body] = std::move(*frame_r.value_if());
  std::vector<std::byte> header;
  std::vector<std::byte> payload;
  split_body(pfx, body, header, payload);

  auto hello_effects = sm.process(spoink::session::MsgIn{pfx, std::move(header), std::move(payload)});
  if (!flush_send_frames(hello_effects, conn)) return false;

  return sm.state() == spoink::session::State::kReady;
}

// ── Sender session ────────────────────────────────────────────────────────────

int run_sender_session(spoink::transport::TlsConnection& conn, const spoink::identity::DeviceIdentity& identity,
                       const std::filesystem::path& srk, const spoink::offline::CanonicalManifest& manifest) {
  const std::uint64_t nonce = now_ms();
  spoink::session::SessionStateMachine sm(spoink::session::Role::kSender, identity.peer_id(), nonce);

  if (!do_hello_handshake(sm, conn)) {
    std::cerr << "error: HELLO handshake failed\n";
    return 1;
  }

  // Build SessionOfferMsg from manifest
  spoink::protocol::SessionOfferMsg offer;
  offer.manifest_digest = manifest.digest.bytes();
  offer.manifest_schema = 1;
  offer.chunk_profile_id = "rabin-v1";
  offer.file_count = static_cast<std::uint32_t>(manifest.files.size());
  offer.logical_bytes = 0;
  offer.overwrite_requested = false;
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(manifest.files.size()); ++i) {
    const auto& f = manifest.files[i];
    spoink::protocol::FileSummary fs;
    fs.file_id = i;
    fs.relative_path = f.path;
    fs.logical_size = f.size;
    fs.file_sha256 = f.digest.bytes();
    offer.files.push_back(std::move(fs));
    offer.logical_bytes += f.size;
  }

  spoink::session::StartSender start_cmd{*sm.peer_id(), std::move(offer), manifest.bytes};

  auto offer_effects = sm.process(std::move(start_cmd));
  if (!flush_send_frames(offer_effects, conn)) {
    std::cerr << "error: failed to send SESSION_OFFER\n";
    return 1;
  }

  // Main sender loop
  std::optional<spoink::protocol::TransferPlanMsg> plan;
  std::size_t chunks_acked = 0;
  std::size_t total_sent = 0;
  bool done = false;

  while (!done) {
    auto frame_r = conn.read_frame();
    if (!frame_r.has_value()) {
      std::cerr << "error: connection closed unexpectedly\n";
      return 1;
    }
    auto [pfx, body] = std::move(*frame_r.value_if());
    std::vector<std::byte> hdr;
    std::vector<std::byte> pld;
    split_body(pfx, body, hdr, pld);

    auto effects = sm.process(spoink::session::MsgIn{pfx, std::move(hdr), std::move(pld)});

    for (const auto& eff : effects) {
      if (const auto* esf = std::get_if<spoink::session::EffSendFrame>(&eff)) {
        if (!conn.send_frame(esf->prefix, esf->header, esf->payload).has_value()) {
          std::cerr << "error: send failed\n";
          return 1;
        }
      } else if (const auto* eplan = std::get_if<spoink::session::EffPlanReceived>(&eff)) {
        plan = eplan->plan;
        // Send all missing chunks immediately
        for (const auto& chunk_hash : plan->missing_chunk_hashes) {
          spoink::support::Sha256Digest digest{chunk_hash};
          auto data_r = spoink::offline::read_verified_chunk(srk, digest);
          if (!data_r.has_value()) {
            std::cerr << "error: missing chunk in store: " << to_hex(chunk_hash) << '\n';
            return 1;
          }
          const auto& data = *data_r.value_if();

          spoink::protocol::ChunkPutMsg put_msg;
          put_msg.transfer_id = plan->transfer_id;
          put_msg.chunk_hash = chunk_hash;
          put_msg.plaintext_length = static_cast<std::uint64_t>(data.size());
          put_msg.codec = "raw";
          put_msg.encoded_length = static_cast<std::uint64_t>(data.size());
          put_msg.attempt = 0;

          spoink::protocol::FramePrefix put_pfx;
          put_pfx.channel = spoink::protocol::Channel::kData;
          put_pfx.message_type = spoink::protocol::MessageType::kChunkPut;
          auto put_hdr = spoink::protocol::encode_chunk_put(put_msg);
          put_pfx.header_length = static_cast<std::uint32_t>(put_hdr.size());
          put_pfx.payload_length = static_cast<std::uint64_t>(data.size());
          put_pfx.request_id = 0;

          if (!conn.send_frame(put_pfx, put_hdr, data).has_value()) {
            std::cerr << "error: failed to send CHUNK_PUT\n";
            return 1;
          }
          ++total_sent;
        }
        if (total_sent == 0) {
          std::cerr << "info: receiver already has all chunks\n";
        }
      } else if (std::get_if<spoink::session::EffChunkAck>(&eff)) {
        ++chunks_acked;
        std::cerr << "  chunk " << chunks_acked << '/' << total_sent << " acked\n";
      } else if (const auto* enack = std::get_if<spoink::session::EffChunkNack>(&eff)) {
        std::cerr << "error: chunk nack: " << enack->error_code << '\n';
        return 1;
      } else if (const auto* eres = std::get_if<spoink::session::EffSessionResult>(&eff)) {
        std::cerr << "session result: " << eres->state << '\n';
        done = true;
      } else if (const auto* eerr = std::get_if<spoink::session::EffProtocolError>(&eff)) {
        std::cerr << "protocol error: " << eerr->code << '\n';
        return 1;
      } else if (std::get_if<spoink::session::EffCloseConn>(&eff)) {
        done = true;
      }
    }
  }

  conn.close();
  return 0;
}

// ── Receiver session ──────────────────────────────────────────────────────────

int run_receiver_session(spoink::transport::TlsConnection& conn, const spoink::identity::DeviceIdentity& identity,
                         spoink::offline::MetadataStore& meta, const std::filesystem::path& srk,
                         const std::filesystem::path& output_dir) {
  const std::uint64_t nonce = now_ms();
  spoink::session::SessionStateMachine sm(spoink::session::Role::kReceiver, identity.peer_id(), nonce);

  if (!do_hello_handshake(sm, conn)) {
    std::cerr << "error: HELLO handshake failed\n";
    return 1;
  }

  std::vector<std::byte> manifest_cbor;
  std::optional<spoink::offline::CanonicalManifest> manifest;
  std::optional<spoink::protocol::TransferPlanMsg> sent_plan;
  std::optional<std::array<std::byte, 16>> transfer_id;

  std::size_t total_missing = 0;
  std::size_t chunks_stored = 0;
  bool results_sent = false;
  bool done = false;

  while (!done) {
    auto frame_r = conn.read_frame();
    if (!frame_r.has_value()) {
      std::cerr << "error: connection closed unexpectedly\n";
      return 1;
    }
    auto [pfx, body] = std::move(*frame_r.value_if());
    std::vector<std::byte> hdr;
    std::vector<std::byte> pld;
    split_body(pfx, body, hdr, pld);

    auto effects = sm.process(spoink::session::MsgIn{pfx, std::move(hdr), std::move(pld)});

    // We may need to inject additional inputs into SM within this loop
    bool inject_results = false;

    for (const auto& eff : effects) {
      if (const auto* esf = std::get_if<spoink::session::EffSendFrame>(&eff)) {
        if (!conn.send_frame(esf->prefix, esf->header, esf->payload).has_value()) {
          std::cerr << "error: send failed\n";
          return 1;
        }
      } else if (const auto* eoffer = std::get_if<spoink::session::EffOfferReceived>(&eff)) {
        spoink::protocol::SessionAcceptMsg accept_msg;
        std::array<std::byte, 16> tid{};
        if (RAND_bytes(reinterpret_cast<unsigned char*>(tid.data()), 16) != 1) {
          std::cerr << "error: RAND_bytes failed\n";
          return 1;
        }
        accept_msg.transfer_id = tid;
        accept_msg.overwrite_allowed = false;
        for (const auto& fs : eoffer->offer.files) {
          accept_msg.accepted_file_ids.push_back(fs.file_id);
        }
        auto accept_effects = sm.process(spoink::session::LocalAccept{accept_msg});
        for (const auto& ae : accept_effects) {
          if (const auto* sf = std::get_if<spoink::session::EffSendFrame>(&ae)) {
            if (!conn.send_frame(sf->prefix, sf->header, sf->payload).has_value()) {
              std::cerr << "error: failed to send SESSION_ACCEPT\n";
              return 1;
            }
          }
        }
      } else if (const auto* emdata = std::get_if<spoink::session::EffManifestData>(&eff)) {
        if (!emdata->part.empty()) {
          manifest_cbor.insert(manifest_cbor.end(), emdata->part.begin(), emdata->part.end());
          transfer_id = emdata->transfer_id;
        } else {
          auto manifest_r = spoink::offline::decode_manifest(manifest_cbor);
          if (!manifest_r.has_value()) {
            std::cerr << "error: failed to decode manifest\n";
            conn.close();
            return 1;
          }
          manifest = std::move(*manifest_r.value_if());

          spoink::protocol::TransferPlanMsg plan;
          plan.transfer_id = emdata->transfer_id;
          plan.session_capability = {};
          plan.plan_part = 0;
          plan.plan_final = true;

          for (const auto& file : manifest->files) {
            for (const auto& chunk : file.chunks) {
              auto ok_r = meta.is_verified(chunk.digest);
              bool have_it = ok_r.has_value() && *ok_r.value_if();
              if (!have_it) {
                plan.missing_chunk_hashes.push_back(chunk.digest.bytes());
                ++total_missing;
              }
            }
          }

          auto plan_hdr = spoink::protocol::encode_transfer_plan(plan);
          spoink::protocol::FramePrefix plan_pfx;
          plan_pfx.channel = spoink::protocol::Channel::kControl;
          plan_pfx.message_type = spoink::protocol::MessageType::kTransferPlan;
          plan_pfx.header_length = static_cast<std::uint32_t>(plan_hdr.size());
          plan_pfx.payload_length = 0;
          plan_pfx.request_id = 0;
          if (!conn.send_frame(plan_pfx, plan_hdr, {}).has_value()) {
            std::cerr << "error: failed to send TRANSFER_PLAN\n";
            return 1;
          }
          sent_plan = std::move(plan);

          if (total_missing == 0) {
            inject_results = true;
          }
        }
      } else if (const auto* echunk = std::get_if<spoink::session::EffChunkReceived>(&eff)) {
        std::filesystem::create_directories(srk);
        spoink::support::Sha256Digest digest{echunk->chunk_hash};
        spoink::offline::Chunk chunk_obj{0, echunk->payload, digest};

        auto install_r = spoink::offline::install_verified_chunk(srk, chunk_obj);
        if (install_r.has_value()) {
          (void)meta.record_verified(digest, static_cast<std::uint64_t>(echunk->payload.size()));
        }

        spoink::session::ChunkStored cs{};
        cs.chunk_hash = echunk->chunk_hash;
        cs.transfer_id = echunk->transfer_id;
        auto ack_effects = sm.process(cs);
        for (const auto& ae : ack_effects) {
          if (const auto* sf = std::get_if<spoink::session::EffSendFrame>(&ae)) {
            if (!conn.send_frame(sf->prefix, sf->header, sf->payload).has_value()) {
              std::cerr << "error: failed to send CHUNK_ACK\n";
              return 1;
            }
          }
        }

        ++chunks_stored;
        std::cerr << "  chunk " << chunks_stored << '/' << total_missing << " stored\n";

        if (chunks_stored == total_missing) {
          inject_results = true;
        }
      } else if (std::get_if<spoink::session::EffCloseConn>(&eff)) {
        done = true;
      } else if (const auto* eerr = std::get_if<spoink::session::EffProtocolError>(&eff)) {
        std::cerr << "protocol error: " << eerr->code << '\n';
        return 1;
      }
    }

    // After processing all effects, check if we should reconstruct + send results
    if (inject_results && !results_sent && manifest.has_value() && transfer_id.has_value()) {
      results_sent = true;
      std::filesystem::create_directories(output_dir);

      std::uint32_t files_ok = 0;
      std::uint32_t files_fail = 0;

      for (std::uint32_t fid = 0; fid < static_cast<std::uint32_t>(manifest->files.size()); ++fid) {
        const auto& mf = manifest->files[fid];
        auto staging_r = spoink::offline::reconstruct_verified_file(srk, output_dir, mf);
        std::string file_state;
        std::string file_err;
        if (staging_r.has_value()) {
          auto fin_r = spoink::offline::finalize_verified_file(*staging_r.value_if());
          if (fin_r.has_value()) {
            file_state = "COMPLETE";
            ++files_ok;
            std::cerr << "received: " << mf.path << '\n';
          } else {
            file_state = "ERROR";
            file_err = "finalize_failed";
            ++files_fail;
          }
        } else {
          file_state = "ERROR";
          file_err = "reconstruct_failed";
          ++files_fail;
        }

        spoink::session::SendFileResult sfr;
        sfr.transfer_id = *transfer_id;
        sfr.file_id = fid;
        sfr.state = file_state;
        sfr.error_code = file_err;
        auto file_effects = sm.process(sfr);
        for (const auto& fe : file_effects) {
          if (const auto* sf = std::get_if<spoink::session::EffSendFrame>(&fe)) {
            (void)conn.send_frame(sf->prefix, sf->header, sf->payload);
          }
        }
      }

      spoink::session::SendSessionResult ssr;
      ssr.transfer_id = *transfer_id;
      ssr.state = (files_fail == 0) ? "COMPLETE" : "PARTIAL";
      ssr.files_completed = files_ok;
      ssr.files_failed = files_fail;
      auto sess_effects = sm.process(ssr);
      for (const auto& se : sess_effects) {
        if (const auto* sf = std::get_if<spoink::session::EffSendFrame>(&se)) {
          (void)conn.send_frame(sf->prefix, sf->header, sf->payload);
        } else if (std::get_if<spoink::session::EffCloseConn>(&se)) {
          done = true;
        }
      }
    }
  }

  conn.close();
  return 0;
}

// ── Commands ──────────────────────────────────────────────────────────────────

int cmd_version() {
  std::cout << "spoink " << SPOINK_VERSION << '\n';
  return 0;
}

int cmd_init(int argc, char* argv[]) {
  std::string name = "spoink";
  std::filesystem::path state_root = default_state_root();

  for (int i = 2; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--name" && i + 1 < argc) {
      name = argv[++i];
    } else if (arg == "--state-root" && i + 1 < argc) {
      state_root = argv[++i];
    }
  }

  auto id_r = spoink::identity::load_or_create_identity(state_root, name);
  if (!id_r.has_value()) {
    std::cerr << "error: failed to init identity\n";
    return 1;
  }
  const auto& id = *id_r.value_if();
  const auto& pid = id.peer_id().bytes();
  std::cout << "peer-id: " << to_hex(pid) << '\n';
  std::cout << "name:    " << id.display_name() << '\n';
  return 0;
}

int cmd_pair(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "usage: spoink pair listen [--port <p>]\n"
                 "       spoink pair connect <host> [--port <p>]\n";
    return 2;
  }

  std::string_view sub{argv[2]};
  std::filesystem::path state_root = default_state_root();
  std::uint16_t port = 0;
  std::string host;

  for (int i = 3; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--port" && i + 1 < argc) {
      port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
    } else if (arg == "--state-root" && i + 1 < argc) {
      state_root = argv[++i];
    } else if (sub == "connect" && host.empty() && arg[0] != '-') {
      host = std::string{arg};
    }
  }

  auto id_r = spoink::identity::load_or_create_identity(state_root, "spoink");
  if (!id_r.has_value()) {
    std::cerr << "error: failed to load identity\n";
    return 1;
  }
  auto& id = *id_r.value_if();

  sqlite3* db = open_peers_db(state_root);
  if (db == nullptr) return 1;

  auto confirm_fn = [](const spoink::pairing::Sas& sas, const std::string& peer_name) -> bool {
    std::cerr << "pairing with: " << peer_name << '\n';
    std::cerr << "SAS code:     " << sas.text << '\n';
    std::cerr << "confirm? [y/N] ";
    std::string line;
    std::getline(std::cin, line);
    return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
  };

  spoink::support::Result<spoink::pairing::PairedPeer> pair_r =
      spoink::support::Result<spoink::pairing::PairedPeer>::failure(
          spoink::support::make_error(spoink::support::ErrorCode::kInvalidArgument, "none"));

  if (sub == "listen") {
    spoink::pairing::PairListenOptions opts;
    opts.port = port;
    std::cerr << "waiting for pairing connection on port " << (port == 0 ? "(OS-assigned)" : std::to_string(port))
              << " ...\n";
    pair_r = spoink::pairing::pair_listen(id, opts, confirm_fn);
  } else if (sub == "connect") {
    if (host.empty()) {
      std::cerr << "usage: spoink pair connect <host> [--port <p>]\n";
      sqlite3_close(db);
      return 2;
    }
    spoink::pairing::PairConnectOptions opts;
    opts.host = host;
    opts.port = (port == 0) ? 7700U : port;
    pair_r = spoink::pairing::pair_connect(id, opts, confirm_fn);
  } else {
    std::cerr << "unknown pair sub-command: " << sub << '\n';
    sqlite3_close(db);
    return 2;
  }

  if (!pair_r.has_value()) {
    std::cerr << "error: pairing failed\n";
    sqlite3_close(db);
    return 1;
  }

  const auto& peer = *pair_r.value_if();
  auto save_r = spoink::pairing::save_trusted_peer(db, peer);
  sqlite3_close(db);

  if (!save_r.has_value()) {
    std::cerr << "error: failed to save peer\n";
    return 1;
  }

  std::cerr << "paired: " << peer.display_name << '\n';
  std::cerr << "id:     " << to_hex(peer.peer_id.bytes()) << '\n';
  return 0;
}

int cmd_peers(int argc, char* argv[]) {
  std::filesystem::path state_root = default_state_root();
  for (int i = 2; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-root" && i + 1 < argc) {
      state_root = argv[++i];
    }
  }

  sqlite3* db = open_peers_db(state_root);
  if (db == nullptr) return 1;

  const char* sql =
      "SELECT peer_id, display_name, paired_at_ms, revoked_at_ms "
      "FROM trusted_peers ORDER BY paired_at_ms DESC";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "error: failed to query peers\n";
    sqlite3_close(db);
    return 1;
  }

  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* pid_data = sqlite3_column_blob(stmt, 0);
    const int pid_len = sqlite3_column_bytes(stmt, 0);
    std::array<std::byte, 32> pid_bytes{};
    if (pid_data != nullptr && pid_len == 32) {
      std::memcpy(pid_bytes.data(), pid_data, 32U);
    }
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const bool revoked = sqlite3_column_type(stmt, 3) != SQLITE_NULL;
    std::cout << to_hex(pid_bytes) << "  " << (name != nullptr ? name : "?") << "  "
              << (revoked ? "[revoked]" : "[active]") << '\n';
    ++count;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  if (count == 0) std::cout << "(no paired peers)\n";
  return 0;
}

int cmd_send(int argc, char* argv[]) {
  if (argc < 4) {
    std::cerr << "usage: spoink send <file> <host> [--port <p>]\n";
    return 2;
  }

  std::filesystem::path src_path{argv[2]};
  std::string host{argv[3]};
  std::uint16_t port = 7701;
  std::filesystem::path state_root = default_state_root();

  for (int i = 4; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--port" && i + 1 < argc) {
      port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
    } else if (arg == "--state-root" && i + 1 < argc) {
      state_root = argv[++i];
    }
  }

  auto id_r = spoink::identity::load_or_create_identity(state_root, "spoink");
  if (!id_r.has_value()) {
    std::cerr << "error: failed to load identity\n";
    return 1;
  }
  auto& id = *id_r.value_if();

  sqlite3* db = open_peers_db(state_root);
  if (db == nullptr) return 1;

  auto meta_r = spoink::offline::MetadataStore::open(state_root);
  if (!meta_r.has_value()) {
    std::cerr << "error: failed to open metadata store\n";
    sqlite3_close(db);
    return 1;
  }
  auto& meta = *meta_r.value_if();
  const auto srk = store_root(state_root);

  // Read file
  std::ifstream ifs(src_path, std::ios::binary);
  if (!ifs) {
    std::cerr << "error: cannot open file: " << src_path << '\n';
    sqlite3_close(db);
    return 1;
  }
  ifs.seekg(0, std::ios::end);
  const auto file_size = static_cast<std::size_t>(ifs.tellg());
  ifs.seekg(0, std::ios::beg);
  std::vector<std::byte> file_bytes(file_size);
  ifs.read(reinterpret_cast<char*>(file_bytes.data()), static_cast<std::streamsize>(file_size));
  ifs.close();

  // Chunk the file
  const auto profile = spoink::offline::rabin_v1_profile();
  auto chunks_r = spoink::offline::chunk_bytes(file_bytes, profile);
  if (!chunks_r.has_value()) {
    std::cerr << "error: failed to chunk file\n";
    sqlite3_close(db);
    return 1;
  }
  const auto& raw_chunks = *chunks_r.value_if();

  // Build manifest
  auto file_digest_r = spoink::offline::sha256(file_bytes);
  if (!file_digest_r.has_value()) {
    std::cerr << "error: failed to hash file\n";
    sqlite3_close(db);
    return 1;
  }
  spoink::offline::ManifestFile mf{src_path.filename().string(), static_cast<std::uint64_t>(file_bytes.size()),
                                   *file_digest_r.value_if(), raw_chunks};

  const auto created_ms = now_ms();
  auto manifest_r = spoink::offline::build_manifest({std::move(mf)}, profile, created_ms);
  if (!manifest_r.has_value()) {
    std::cerr << "error: failed to build manifest\n";
    sqlite3_close(db);
    return 1;
  }
  auto& manifest = *manifest_r.value_if();

  // Install chunks to store
  std::filesystem::create_directories(srk);
  for (const auto& chunk : raw_chunks) {
    auto inst_r = spoink::offline::install_verified_chunk(srk, chunk);
    if (inst_r.has_value()) {
      (void)meta.record_verified(chunk.digest, static_cast<std::uint64_t>(chunk.bytes.size()));
    }
  }

  // Connect
  spoink::transport::ConnectOptions conn_opts;
  conn_opts.host = host;
  conn_opts.port = port;
  auto conn_r = spoink::transport::tls_connect(id, conn_opts, db);
  sqlite3_close(db);

  if (!conn_r.has_value()) {
    std::cerr << "error: failed to connect to " << host << ':' << port << '\n';
    return 1;
  }
  auto& conn = *conn_r.value_if();

  std::cerr << "connected to " << host << ':' << port << '\n';
  return run_sender_session(conn, id, srk, manifest);
}

int cmd_receive(int argc, char* argv[]) {
  std::uint16_t port = 7701;
  std::filesystem::path state_root = default_state_root();
  std::filesystem::path output_dir = std::filesystem::current_path();

  for (int i = 2; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--port" && i + 1 < argc) {
      port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
    } else if (arg == "--state-root" && i + 1 < argc) {
      state_root = argv[++i];
    } else if (arg == "--output-dir" && i + 1 < argc) {
      output_dir = argv[++i];
    }
  }

  auto id_r = spoink::identity::load_or_create_identity(state_root, "spoink");
  if (!id_r.has_value()) {
    std::cerr << "error: failed to load identity\n";
    return 1;
  }
  auto& id = *id_r.value_if();

  sqlite3* db = open_peers_db(state_root);
  if (db == nullptr) return 1;

  auto meta_r = spoink::offline::MetadataStore::open(state_root);
  if (!meta_r.has_value()) {
    std::cerr << "error: failed to open metadata store\n";
    sqlite3_close(db);
    return 1;
  }
  auto& meta = *meta_r.value_if();
  const auto srk = store_root(state_root);

  // Listen for connection
  spoink::transport::ListenOptions listen_opts;
  listen_opts.port = port;
  std::cerr << "listening on port " << port << " ...\n";

  auto conn_r = spoink::transport::tls_accept_one(id, listen_opts, db);
  sqlite3_close(db);

  if (!conn_r.has_value()) {
    std::cerr << "error: failed to accept connection\n";
    return 1;
  }
  auto& conn = *conn_r.value_if();
  std::cerr << "accepted connection from peer " << to_hex(conn.peer_id().bytes()) << '\n';

  return run_receiver_session(conn, id, meta, srk, output_dir);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: spoink <command> [args...]\n"
                 "commands: version, init, pair, peers, send, receive\n";
    return 2;
  }

  std::string_view cmd{argv[1]};

  if (cmd == "version") return cmd_version();
  if (cmd == "init") return cmd_init(argc, argv);
  if (cmd == "pair") return cmd_pair(argc, argv);
  if (cmd == "peers") return cmd_peers(argc, argv);
  if (cmd == "send") return cmd_send(argc, argv);
  if (cmd == "receive") return cmd_receive(argc, argv);

  std::cerr << "unknown command: " << cmd << '\n' << "commands: version, init, pair, peers, send, receive\n";
  return 2;
}
