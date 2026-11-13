#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "spoink/protocol/codec.h"
#include "spoink/protocol/framing.h"
#include "spoink/support/result.h"
#include "spoink/support/types.h"

namespace spoink::session {

// ── Roles and states ─────────────────────────────────────────────────────────

enum class Role { kSender, kReceiver };

enum class State {
  kConnecting,         // Before HELLO sent
  kHelloSent,          // HELLO sent, waiting for peer HELLO
  kReady,              // HELLO exchanged; no active session
  kOfferSent,          // Sender: SESSION_OFFER sent; waiting for ACCEPT/REJECT
  kOfferPending,       // Receiver: SESSION_OFFER received; awaiting local decision
  kManifestSending,    // Sender: sending MANIFEST_BEGIN/PART/END
  kManifestReceiving,  // Receiver: receiving manifest
  kPlanPending,        // Sender: MANIFEST_END sent; waiting for TRANSFER_PLAN
  kPlanSending,        // Receiver: manifest complete; about to send TRANSFER_PLAN
  kTransferring,       // Chunks flowing (sender sending, receiver ACKing)
  kDraining,           // Sender: all chunks sent; waiting for FILE/SESSION_RESULT
  kComplete,
  kCancelled,
  kError,
};

// ── Inputs ───────────────────────────────────────────────────────────────────

struct MsgIn {
  protocol::FramePrefix prefix;
  std::vector<std::byte> header;
  std::vector<std::byte> payload;
};

struct ConnClosed { std::string reason; };
struct TimerFired { std::uint32_t timer_id; };

// Caller signals: start a new sender session.
struct StartSender {
  support::PeerId peer_id;
  protocol::SessionOfferMsg offer;
  std::vector<std::byte> manifest_cbor;
};

// Caller signals: accept the pending offer (receiver side).
struct LocalAccept { protocol::SessionAcceptMsg accept; };

// Caller signals: reject the pending offer (receiver side).
struct LocalReject { std::string reason; };

// Caller signals: chunk blob committed to store + DB (receiver side).
// The state machine then emits EffSendFrame(CHUNK_ACK).
struct ChunkStored {
  std::array<std::byte, 32> chunk_hash;
  std::array<std::byte, 16> transfer_id;
};

// Caller signals: chunk store failed (receiver side).
// The state machine then emits EffSendFrame(CHUNK_NACK).
struct ChunkStoreFailed {
  std::array<std::byte, 32> chunk_hash;
  std::array<std::byte, 16> transfer_id;
  std::string error_code;
  bool retryable{false};
};

// Caller signals: file result ready to send (receiver side).
struct SendFileResult {
  std::array<std::byte, 16> transfer_id;
  std::uint32_t file_id{0};
  std::string state;
  std::string error_code;
};

// Caller signals: session result ready to send (receiver side).
struct SendSessionResult {
  std::array<std::byte, 16> transfer_id;
  std::string state;
  std::uint32_t files_completed{0};
  std::uint32_t files_failed{0};
};

using Input = std::variant<MsgIn, ConnClosed, TimerFired,
                           StartSender, LocalAccept, LocalReject,
                           ChunkStored, ChunkStoreFailed,
                           SendFileResult, SendSessionResult>;

// ── Effects ──────────────────────────────────────────────────────────────────

struct EffSendFrame {
  protocol::FramePrefix prefix;
  std::vector<std::byte> header;
  std::vector<std::byte> payload;
};

struct EffCloseConn { std::string reason; };
struct EffSetTimer { std::uint32_t timer_id; std::uint32_t ms; };
struct EffCancelTimer { std::uint32_t timer_id; };

// Notify the application layer of session-level events.
struct EffOfferReceived  { protocol::SessionOfferMsg offer; };
struct EffAccepted       { protocol::SessionAcceptMsg accept; };
struct EffRejected       { std::string reason; };
struct EffPlanReceived   { protocol::TransferPlanMsg plan; };
struct EffChunkAck       { std::array<std::byte, 32> chunk_hash; std::string status; };
struct EffChunkNack      { std::array<std::byte, 32> chunk_hash; std::string error_code; bool retryable; };
struct EffFileResult     { std::uint32_t file_id; std::string state; std::string error_code; };
struct EffSessionResult  { std::array<std::byte, 16> transfer_id; std::string state; };
struct EffManifestData   { std::array<std::byte, 16> transfer_id; std::vector<std::byte> part; };
// Receiver: a complete chunk has arrived; caller must commit to store then call ChunkStored/ChunkStoreFailed.
struct EffChunkReceived  { std::array<std::byte, 16> transfer_id; std::array<std::byte, 32> chunk_hash;
                           std::vector<std::byte> payload; std::string codec; std::uint64_t plaintext_length; };
struct EffProtocolError  { std::string code; };

using Effect = std::variant<EffSendFrame, EffCloseConn, EffSetTimer, EffCancelTimer,
                            EffOfferReceived, EffAccepted, EffRejected, EffPlanReceived,
                            EffChunkAck, EffChunkNack, EffFileResult, EffSessionResult,
                            EffManifestData, EffChunkReceived, EffProtocolError>;

// ── State machine ─────────────────────────────────────────────────────────────
//
// IC-007: pure, deterministic. No I/O, no side effects. All actions returned
// as effects. Callers must execute effects in order before calling process()
// again with the next input.

class SessionStateMachine {
 public:
  // Create machine. `start()` must be called once before any `process()` call.
  explicit SessionStateMachine(Role role, support::PeerId local_peer_id,
                               std::uint64_t instance_nonce);

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] Role role() const noexcept { return role_; }
  [[nodiscard]] const std::optional<support::PeerId>& peer_id() const noexcept { return peer_id_; }

  // Emit initial HELLO frame and transition to kHelloSent. Call exactly once.
  [[nodiscard]] std::vector<Effect> start();

  // Feed one input; returns zero or more effects. Order of effects is significant.
  [[nodiscard]] std::vector<Effect> process(Input input);

 private:
  std::vector<Effect> handle_msg(const MsgIn& msg);
  std::vector<Effect> handle_hello(const protocol::HelloMsg& msg);
  std::vector<Effect> handle_session_offer(const protocol::SessionOfferMsg& msg);
  std::vector<Effect> handle_session_accept(const protocol::SessionAcceptMsg& msg);
  std::vector<Effect> handle_session_reject(const protocol::SessionRejectMsg& msg);
  std::vector<Effect> handle_manifest_begin(const protocol::ManifestBeginMsg& msg);
  std::vector<Effect> handle_manifest_part(const protocol::ManifestPartMsg& msg,
                                           std::vector<std::byte> payload);
  std::vector<Effect> handle_manifest_end(const protocol::ManifestEndMsg& msg);
  std::vector<Effect> handle_transfer_plan(const protocol::TransferPlanMsg& msg);
  std::vector<Effect> handle_chunk_put(const protocol::ChunkPutMsg& msg,
                                       std::vector<std::byte> payload);  // payload by value (moved)
  std::vector<Effect> handle_chunk_ack(const protocol::ChunkAckMsg& msg);
  std::vector<Effect> handle_chunk_nack(const protocol::ChunkNackMsg& msg);
  std::vector<Effect> handle_file_result(const protocol::FileResultMsg& msg);
  std::vector<Effect> handle_session_result(const protocol::SessionResultMsg& msg);
  std::vector<Effect> handle_cancel(const protocol::CancelMsg& msg);
  std::vector<Effect> handle_error(const protocol::ErrorMsg& msg);
  std::vector<Effect> handle_ping(const protocol::PingMsg& msg);
  std::vector<Effect> handle_start_sender(const StartSender& cmd);
  std::vector<Effect> handle_local_accept(const LocalAccept& cmd);
  std::vector<Effect> handle_local_reject(const LocalReject& cmd);
  std::vector<Effect> handle_chunk_stored(const ChunkStored& cmd);
  std::vector<Effect> handle_chunk_store_failed(const ChunkStoreFailed& cmd);
  std::vector<Effect> handle_send_file_result(const SendFileResult& cmd);
  std::vector<Effect> handle_send_session_result(const SendSessionResult& cmd);

  std::vector<Effect> protocol_error(const char* code);
  EffSendFrame make_send(protocol::Channel ch, protocol::MessageType mt,
                         std::vector<std::byte> header,
                         std::vector<std::byte> payload = {});

  Role role_;
  State state_{State::kConnecting};
  support::PeerId local_peer_id_;
  std::uint64_t instance_nonce_;
  std::uint64_t next_request_id_{1};

  std::optional<support::PeerId> peer_id_;
  std::uint64_t peer_instance_nonce_{0};

  // Pending offer (receiver holds this between kOfferPending and decision)
  std::optional<protocol::SessionOfferMsg> pending_offer_;

  // Active transfer
  std::optional<std::array<std::byte, 16>> transfer_id_;
  std::optional<protocol::SessionOfferMsg> active_offer_;
  std::optional<protocol::SessionAcceptMsg> active_accept_;

  // Manifest assembly (receiver)
  std::vector<std::byte> manifest_buffer_;
  std::optional<protocol::ManifestBeginMsg> manifest_begin_;
  std::uint32_t manifest_parts_received_{0};

  // Pending manifest (sender)
  std::vector<std::byte> pending_manifest_cbor_;
  std::uint32_t manifest_parts_sent_{0};

  static constexpr std::uint32_t kTimerHandshake = 1;
  static constexpr std::uint32_t kTimerIdle = 2;
};

}  // namespace spoink::session
