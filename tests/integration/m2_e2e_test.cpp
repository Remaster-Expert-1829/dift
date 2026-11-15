// TEST-E2E-001: Two in-process TLS endpoints on loopback, pair, transfer one
// small file, verify byte-identical output.

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <sqlite3.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
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

namespace {

static constexpr std::uint16_t kE2EPort = 19878U;

static constexpr const char* kSchema = R"(
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

sqlite3* make_db() {
  sqlite3* db = nullptr;
  sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (db) sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr);
  return db;
}

std::vector<std::byte> spki_der_from_cert(x509_st* cert) {
  int len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), nullptr);
  if (len <= 0) return {};
  std::vector<std::byte> buf(static_cast<std::size_t>(len));
  auto* p = reinterpret_cast<unsigned char*>(buf.data());
  i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), &p);
  return buf;
}

std::vector<std::byte> cert_der_from_cert(x509_st* cert) {
  int len = i2d_X509(cert, nullptr);
  if (len <= 0) return {};
  std::vector<std::byte> buf(static_cast<std::size_t>(len));
  auto* p = reinterpret_cast<unsigned char*>(buf.data());
  i2d_X509(cert, &p);
  return buf;
}

void split_body(const spoink::protocol::FramePrefix& pfx, const std::vector<std::byte>& body,
                std::vector<std::byte>& hdr, std::vector<std::byte>& pld) {
  hdr.assign(body.begin(), body.begin() + pfx.header_length);
  pld.assign(body.begin() + pfx.header_length, body.end());
}

bool flush_frames(const std::vector<spoink::session::Effect>& effs, spoink::transport::TlsConnection& conn) {
  for (const auto& eff : effs) {
    if (const auto* sf = std::get_if<spoink::session::EffSendFrame>(&eff)) {
      if (!conn.send_frame(sf->prefix, sf->header, sf->payload).has_value()) return false;
    }
  }
  return true;
}

// Feed one frame to the SM and flush send effects.
bool feed_frame(spoink::session::SessionStateMachine& sm, spoink::transport::TlsConnection& conn,
                std::vector<spoink::session::Effect>& out_effects) {
  auto fr = conn.read_frame();
  if (!fr.has_value()) return false;
  auto [pfx, body] = std::move(*fr.value_if());
  std::vector<std::byte> hdr;
  std::vector<std::byte> pld;
  split_body(pfx, body, hdr, pld);
  out_effects = sm.process(spoink::session::MsgIn{pfx, std::move(hdr), std::move(pld)});
  return flush_frames(out_effects, conn);
}

}  // namespace

TEST_CASE("TEST-E2E-001: pair + transfer one small file, byte-identical output", "[e2e]") {
  const auto root_sender = std::filesystem::temp_directory_path() / "spoink_e2e001_s";
  const auto root_receiver = std::filesystem::temp_directory_path() / "spoink_e2e001_r";
  std::filesystem::remove_all(root_sender);
  std::filesystem::remove_all(root_receiver);

  auto id_sender_r = spoink::identity::load_or_create_identity(root_sender, "E2ESender");
  REQUIRE(id_sender_r.has_value());
  auto id_receiver_r = spoink::identity::load_or_create_identity(root_receiver, "E2EReceiver");
  REQUIRE(id_receiver_r.has_value());

  const auto& id_sender = *id_sender_r.value_if();
  const auto& id_receiver = *id_receiver_r.value_if();

  sqlite3* db_sender = make_db();
  sqlite3* db_receiver = make_db();
  REQUIRE(db_sender != nullptr);
  REQUIRE(db_receiver != nullptr);

  // Simulate pairing by inserting each peer into the other's trust store.
  {
    spoink::pairing::PairedPeer peer_sender{id_sender.peer_id(), id_sender.display_name(),
                                            spki_der_from_cert(id_sender.cert()), cert_der_from_cert(id_sender.cert())};
    REQUIRE(!peer_sender.spki_der.empty());
    REQUIRE(spoink::pairing::save_trusted_peer(db_receiver, peer_sender).has_value());

    spoink::pairing::PairedPeer peer_receiver{id_receiver.peer_id(), id_receiver.display_name(),
                                              spki_der_from_cert(id_receiver.cert()),
                                              cert_der_from_cert(id_receiver.cert())};
    REQUIRE(!peer_receiver.spki_der.empty());
    REQUIRE(spoink::pairing::save_trusted_peer(db_sender, peer_receiver).has_value());
  }

  // Test file content.
  std::vector<std::byte> file_content(100);
  for (std::size_t i = 0; i < file_content.size(); ++i) file_content[i] = static_cast<std::byte>(i % 256);

  // Build manifest.
  const auto profile = spoink::offline::rabin_v1_profile();
  auto sha_r = spoink::offline::sha256(file_content);
  REQUIRE(sha_r.has_value());
  auto chunks_r = spoink::offline::chunk_bytes(file_content, profile);
  REQUIRE(chunks_r.has_value());

  spoink::offline::ManifestFile mf{"test.txt", file_content.size(), *sha_r.value_if(), *chunks_r.value_if()};

  auto manifest_r = spoink::offline::build_manifest({mf}, profile, 0);
  REQUIRE(manifest_r.has_value());
  const auto& canonical = *manifest_r.value_if();

  // Install chunks into sender store.
  const auto srk_s = root_sender / "store";
  std::filesystem::create_directories(srk_s);
  for (auto& chunk : mf.chunks) {
    REQUIRE(spoink::offline::install_verified_chunk(srk_s, chunk).has_value());
  }

  const auto srk_r = root_receiver / "store";
  const auto out_dir = root_receiver / "output";
  std::filesystem::create_directories(srk_r);
  std::filesystem::create_directories(out_dir);

  auto meta_r = spoink::offline::MetadataStore::open(root_receiver);
  REQUIRE(meta_r.has_value());
  auto& meta = *meta_r.value_if();

  std::string recv_err;
  bool recv_ok = false;
  std::string send_err;
  bool send_ok = false;

  // ── Receiver thread ──────────────────────────────────────────────────────────
  std::thread recv_thread([&]() {
    spoink::transport::ListenOptions lo;
    lo.port = kE2EPort;
    lo.handshake_timeout_ms = 10000;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

    auto conn_r2 = spoink::transport::tls_accept_one(id_receiver, lo, db_receiver);
    if (!conn_r2.has_value()) {
      recv_err = "tls_accept_one failed";
      return;
    }
    auto& conn = *conn_r2.value_if();

    spoink::session::SessionStateMachine sm(spoink::session::Role::kReceiver, id_receiver.peer_id(), 2U);
    if (!flush_frames(sm.start(), conn)) {
      recv_err = "HELLO send failed";
      return;
    }

    std::vector<std::byte> manifest_cbor;
    std::optional<std::array<std::byte, 16>> transfer_id;
    std::optional<spoink::protocol::TransferPlanMsg> sent_plan;
    std::size_t total_missing = 0;
    std::size_t stored_count = 0;
    bool results_sent = false;
    bool done = false;

    auto send_results = [&]() -> bool {
      if (results_sent || !transfer_id.has_value() || manifest_cbor.empty()) return true;
      results_sent = true;
      auto dec = spoink::offline::decode_manifest(manifest_cbor);
      if (!dec.has_value()) {
        recv_err = "decode_manifest failed";
        return false;
      }
      bool file_ok = false;
      for (const auto& f : dec.value_if()->files) {
        auto staged = spoink::offline::reconstruct_verified_file(srk_r, out_dir, f);
        if (staged.has_value()) file_ok = spoink::offline::finalize_verified_file(*staged.value_if()).has_value();
      }
      spoink::session::SendFileResult sfr;
      sfr.transfer_id = *transfer_id;
      sfr.file_id = 0;
      sfr.state = file_ok ? "ok" : "error";
      if (!flush_frames(sm.process(sfr), conn)) {
        recv_err = "FILE_RESULT send failed";
        return false;
      }
      spoink::session::SendSessionResult ssr;
      ssr.transfer_id = *transfer_id;
      ssr.state = file_ok ? "ok" : "error";
      ssr.files_completed = file_ok ? 1U : 0U;
      ssr.files_failed = file_ok ? 0U : 1U;
      if (!flush_frames(sm.process(ssr), conn)) {
        recv_err = "SESSION_RESULT send failed";
        return false;
      }
      recv_ok = file_ok;
      done = true;
      return true;
    };

    while (!done) {
      auto fr2 = conn.read_frame();
      if (!fr2.has_value()) {
        recv_err = "read_frame failed";
        return;
      }
      auto [pfx, body] = std::move(*fr2.value_if());
      std::vector<std::byte> hdr;
      std::vector<std::byte> pld;
      split_body(pfx, body, hdr, pld);

      // Intercept DATA_BIND before SM.
      if (pfx.message_type == spoink::protocol::MessageType::kDataBind) {
        if (!sent_plan.has_value()) {
          recv_err = "DATA_BIND before TRANSFER_PLAN";
          return;
        }
        auto bind = spoink::protocol::decode_data_bind(hdr);
        bool match = bind.has_value() && (std::memcmp(bind.value_if()->session_capability.data(),
                                                      sent_plan->session_capability.data(), 32) == 0);
        spoink::protocol::DataBindResultMsg res;
        res.accepted = match;
        if (!match) res.error_code = "m2.bind.capability_mismatch";
        auto res_hdr = spoink::protocol::encode_data_bind_result(res);
        spoink::protocol::FramePrefix res_pfx;
        res_pfx.channel = spoink::protocol::Channel::kData;
        res_pfx.message_type = spoink::protocol::MessageType::kDataBindResult;
        res_pfx.header_length = static_cast<std::uint32_t>(res_hdr.size());
        res_pfx.payload_length = 0;
        res_pfx.request_id = 0;
        (void)conn.send_frame(res_pfx, res_hdr, {});
        if (!match) {
          recv_err = "session_capability mismatch";
          return;
        }
        continue;
      }

      auto effects = sm.process(spoink::session::MsgIn{pfx, std::move(hdr), std::move(pld)});
      if (!flush_frames(effects, conn)) {
        recv_err = "send effects failed";
        return;
      }

      for (const auto& eff : effects) {
        if (const auto* eo = std::get_if<spoink::session::EffOfferReceived>(&eff)) {
          spoink::protocol::SessionAcceptMsg acc;
          std::array<std::byte, 16> tid{};
          RAND_bytes(reinterpret_cast<unsigned char*>(tid.data()), 16);
          acc.transfer_id = tid;
          acc.overwrite_allowed = false;
          for (const auto& fss : eo->offer.files) acc.accepted_file_ids.push_back(fss.file_id);
          if (!flush_frames(sm.process(spoink::session::LocalAccept{acc}), conn)) {
            recv_err = "SESSION_ACCEPT failed";
            return;
          }
        } else if (const auto* em = std::get_if<spoink::session::EffManifestData>(&eff)) {
          if (!em->part.empty()) {
            manifest_cbor.insert(manifest_cbor.end(), em->part.begin(), em->part.end());
            transfer_id = em->transfer_id;
          } else {
            // Manifest complete — build and send TRANSFER_PLAN.
            auto dec = spoink::offline::decode_manifest(manifest_cbor);
            if (!dec.has_value()) {
              recv_err = "decode_manifest (plan) failed";
              return;
            }
            spoink::protocol::TransferPlanMsg plan;
            plan.transfer_id = em->transfer_id;
            {
              std::array<std::byte, 32> cap{};
              RAND_bytes(reinterpret_cast<unsigned char*>(cap.data()), 32);
              plan.session_capability = cap;
            }
            plan.plan_part = 0;
            plan.plan_final = true;
            for (const auto& f : dec.value_if()->files) {
              for (const auto& c : f.chunks) {
                auto ok_r = meta.is_verified(c.digest);
                if (!ok_r.has_value() || !*ok_r.value_if()) {
                  plan.missing_chunk_hashes.push_back(c.digest.bytes());
                  ++total_missing;
                }
              }
            }
            sent_plan = plan;
            auto plan_hdr = spoink::protocol::encode_transfer_plan(plan);
            spoink::protocol::FramePrefix plan_pfx;
            plan_pfx.channel = spoink::protocol::Channel::kControl;
            plan_pfx.message_type = spoink::protocol::MessageType::kTransferPlan;
            plan_pfx.header_length = static_cast<std::uint32_t>(plan_hdr.size());
            plan_pfx.payload_length = 0;
            plan_pfx.request_id = 0;
            if (!conn.send_frame(plan_pfx, plan_hdr, {}).has_value()) {
              recv_err = "TRANSFER_PLAN send failed";
              return;
            }
            if (total_missing == 0 && !send_results()) return;
          }
        } else if (const auto* ec = std::get_if<spoink::session::EffChunkReceived>(&eff)) {
          spoink::support::Sha256Digest digest{ec->chunk_hash};
          spoink::offline::Chunk chunk_obj{0, ec->payload, digest};
          auto inst = spoink::offline::install_verified_chunk(srk_r, chunk_obj);
          if (inst.has_value()) {
            (void)meta.record_verified(digest, static_cast<std::uint64_t>(ec->payload.size()));
            spoink::session::ChunkStored cs;
            cs.chunk_hash = ec->chunk_hash;
            cs.transfer_id = ec->transfer_id;
            if (!flush_frames(sm.process(cs), conn)) {
              recv_err = "CHUNK_ACK failed";
              return;
            }
            ++stored_count;
            if (stored_count == total_missing && !send_results()) return;
          } else {
            spoink::session::ChunkStoreFailed csf;
            csf.chunk_hash = ec->chunk_hash;
            csf.transfer_id = ec->transfer_id;
            csf.error_code = "m2.store.write_failed";
            csf.retryable = false;
            (void)flush_frames(sm.process(csf), conn);
            recv_err = "install chunk failed";
            return;
          }
        } else if (std::get_if<spoink::session::EffCloseConn>(&eff)) {
          done = true;
        } else if (const auto* ep = std::get_if<spoink::session::EffProtocolError>(&eff)) {
          recv_err = "protocol error: " + ep->code;
          return;
        }
      }
    }
    conn.close();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

  // ── Sender thread ─────────────────────────────────────────────────────────────
  std::thread send_thread([&]() {
    spoink::transport::ConnectOptions co;
    co.host = "127.0.0.1";
    co.port = kE2EPort;
    co.handshake_timeout_ms = 10000;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

    auto conn_s = spoink::transport::tls_connect(id_sender, co, db_sender);
    if (!conn_s.has_value()) {
      send_err = "tls_connect failed";
      return;
    }
    auto& conn = *conn_s.value_if();

    spoink::session::SessionStateMachine sm(spoink::session::Role::kSender, id_sender.peer_id(), 1U);
    if (!flush_frames(sm.start(), conn)) {
      send_err = "HELLO send failed";
      return;
    }

    // Phase 1: HELLO exchange — read receiver's HELLO to reach kReady.
    {
      std::vector<spoink::session::Effect> hello_effects;
      if (!feed_frame(sm, conn, hello_effects)) {
        send_err = "HELLO receive failed";
        return;
      }
    }

    // Phase 2: start sender session.
    {
      spoink::protocol::SessionOfferMsg offer;
      offer.manifest_digest = canonical.digest.bytes();
      offer.manifest_schema = 1;
      offer.chunk_profile_id = "rabin-v1";
      offer.file_count = 1;
      offer.logical_bytes = static_cast<std::uint64_t>(file_content.size());
      offer.overwrite_requested = false;
      spoink::protocol::FileSummary fss;
      fss.file_id = 0;
      fss.relative_path = "test.txt";
      fss.logical_size = static_cast<std::uint64_t>(file_content.size());
      fss.file_sha256 = canonical.files[0].digest.bytes();
      offer.files.push_back(std::move(fss));

      spoink::session::StartSender start{id_receiver.peer_id(), std::move(offer), canonical.bytes};
      if (!flush_frames(sm.process(std::move(start)), conn)) {
        send_err = "SESSION_OFFER failed";
        return;
      }
    }

    // Phase 3: main loop — receive SESSION_ACCEPT, send manifest, receive TRANSFER_PLAN,
    //          DATA_BIND exchange, send chunks, receive FILE/SESSION_RESULT.
    bool done = false;
    while (!done) {
      std::vector<spoink::session::Effect> effects;
      if (!feed_frame(sm, conn, effects)) {
        send_err = "read_frame failed";
        return;
      }

      for (const auto& eff : effects) {
        if (const auto* ep = std::get_if<spoink::session::EffPlanReceived>(&eff)) {
          const auto& plan = ep->plan;
          // DATA_BIND exchange.
          spoink::protocol::DataBindMsg bind_msg;
          bind_msg.transfer_id = plan.transfer_id;
          bind_msg.session_capability = plan.session_capability;
          auto bind_hdr = spoink::protocol::encode_data_bind(bind_msg);
          spoink::protocol::FramePrefix bind_pfx;
          bind_pfx.channel = spoink::protocol::Channel::kData;
          bind_pfx.message_type = spoink::protocol::MessageType::kDataBind;
          bind_pfx.header_length = static_cast<std::uint32_t>(bind_hdr.size());
          bind_pfx.payload_length = 0;
          bind_pfx.request_id = 0;
          if (!conn.send_frame(bind_pfx, bind_hdr, {}).has_value()) {
            send_err = "DATA_BIND send failed";
            return;
          }
          auto res_fr = conn.read_frame();
          if (!res_fr.has_value()) {
            send_err = "DATA_BIND_RESULT read failed";
            return;
          }
          auto [res_pfx, res_body] = std::move(*res_fr.value_if());
          std::vector<std::byte> res_hdr2;
          std::vector<std::byte> res_pld2;
          split_body(res_pfx, res_body, res_hdr2, res_pld2);
          if (res_pfx.message_type != spoink::protocol::MessageType::kDataBindResult) {
            send_err = "expected DATA_BIND_RESULT";
            return;
          }
          auto bind_res = spoink::protocol::decode_data_bind_result(res_hdr2);
          if (!bind_res.has_value() || !bind_res.value_if()->accepted) {
            send_err = "DATA_BIND rejected";
            return;
          }
          // Send all missing chunks.
          for (const auto& chunk_hash : plan.missing_chunk_hashes) {
            spoink::support::Sha256Digest digest{chunk_hash};
            auto data = spoink::offline::read_verified_chunk(srk_s, digest);
            if (!data.has_value()) {
              send_err = "missing chunk";
              return;
            }
            const auto& bytes = *data.value_if();
            spoink::protocol::ChunkPutMsg put;
            put.transfer_id = plan.transfer_id;
            put.chunk_hash = chunk_hash;
            put.plaintext_length = static_cast<std::uint64_t>(bytes.size());
            put.codec = "raw";
            put.encoded_length = static_cast<std::uint64_t>(bytes.size());
            put.attempt = 0;
            spoink::protocol::FramePrefix put_pfx;
            put_pfx.channel = spoink::protocol::Channel::kData;
            put_pfx.message_type = spoink::protocol::MessageType::kChunkPut;
            auto put_hdr = spoink::protocol::encode_chunk_put(put);
            put_pfx.header_length = static_cast<std::uint32_t>(put_hdr.size());
            put_pfx.payload_length = static_cast<std::uint64_t>(bytes.size());
            put_pfx.request_id = 0;
            if (!conn.send_frame(put_pfx, put_hdr, bytes).has_value()) {
              send_err = "CHUNK_PUT send failed";
              return;
            }
          }
        } else if (const auto* enack = std::get_if<spoink::session::EffChunkNack>(&eff)) {
          send_err = "chunk NACK: " + enack->error_code;
          return;
        } else if (std::get_if<spoink::session::EffSessionResult>(&eff)) {
          send_ok = true;
          done = true;
        } else if (std::get_if<spoink::session::EffCloseConn>(&eff)) {
          done = true;
        } else if (const auto* eperr = std::get_if<spoink::session::EffProtocolError>(&eff)) {
          send_err = "protocol error: " + eperr->code;
          return;
        }
      }
    }
    conn.close();
  });

  send_thread.join();
  recv_thread.join();

  REQUIRE(send_err.empty());
  REQUIRE(recv_err.empty());
  REQUIRE(send_ok);
  REQUIRE(recv_ok);

  // Verify byte-identical output.
  const auto out_path = out_dir / "test.txt";
  REQUIRE(std::filesystem::exists(out_path));
  std::ifstream fin(out_path, std::ios::binary);
  REQUIRE(fin.is_open());
  std::vector<char> out_buf((std::istreambuf_iterator<char>(fin)), {});
  REQUIRE(out_buf.size() == file_content.size());
  for (std::size_t i = 0; i < file_content.size(); ++i)
    REQUIRE(static_cast<unsigned char>(out_buf[i]) == static_cast<unsigned int>(file_content[i]));

  sqlite3_close(db_sender);
  sqlite3_close(db_receiver);
  std::filesystem::remove_all(root_sender);
  std::filesystem::remove_all(root_receiver);
}
