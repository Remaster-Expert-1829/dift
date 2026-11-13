#include "spoink/session/state_machine.h"

#include <cstring>
#include <variant>

#include "spoink/support/error.h"

namespace spoink::session {


SessionStateMachine::SessionStateMachine(Role role, support::PeerId local_peer_id,
                                         std::uint64_t instance_nonce)
    : role_(role),
      local_peer_id_(std::move(local_peer_id)),
      instance_nonce_(instance_nonce) {}

// ── start() ──────────────────────────────────────────────────────────────────

std::vector<Effect> SessionStateMachine::start() {
  if (state_ != State::kConnecting) return protocol_error("m2.sm.already_started");

  protocol::HelloMsg hello;
  hello.peer_id = local_peer_id_.bytes();
  hello.role = (role_ == Role::kSender) ? "SENDER" : "RECEIVER";
  hello.instance_nonce = {};
  for (std::size_t i = 0; i < 8; ++i) {
    hello.instance_nonce[i] = static_cast<std::byte>((instance_nonce_ >> (i * 8U)) & 0xFFU);
  }

  auto send = make_send(protocol::Channel::kControl, protocol::MessageType::kHello,
                        protocol::encode_hello(hello));

  state_ = State::kHelloSent;

  std::vector<Effect> effects;
  effects.emplace_back(std::move(send));
  effects.emplace_back(EffSetTimer{kTimerHandshake, protocol::Limits::kHandshakeTimeoutMs});
  return effects;
}

// ── process() ─────────────────────────────────────────────────────────────────

std::vector<Effect> SessionStateMachine::process(Input input) {
  return std::visit(
      [this](auto&& in) -> std::vector<Effect> {
        using T = std::decay_t<decltype(in)>;
        if constexpr (std::is_same_v<T, MsgIn>)         return handle_msg(in);
        if constexpr (std::is_same_v<T, ConnClosed>)    {
          state_ = State::kError;
          return {EffCloseConn{in.reason}};
        }
        if constexpr (std::is_same_v<T, TimerFired>)    {
          if (state_ != State::kComplete && state_ != State::kCancelled && state_ != State::kError) {
            state_ = State::kError;
            return protocol_error("m2.sm.timeout");
          }
          return {};
        }
        if constexpr (std::is_same_v<T, StartSender>)      return handle_start_sender(in);
        if constexpr (std::is_same_v<T, LocalAccept>)      return handle_local_accept(in);
        if constexpr (std::is_same_v<T, LocalReject>)      return handle_local_reject(in);
        if constexpr (std::is_same_v<T, ChunkStored>)      return handle_chunk_stored(in);
        if constexpr (std::is_same_v<T, ChunkStoreFailed>) return handle_chunk_store_failed(in);
        if constexpr (std::is_same_v<T, SendFileResult>)   return handle_send_file_result(in);
        if constexpr (std::is_same_v<T, SendSessionResult>) return handle_send_session_result(in);
        return {};
      },
      std::move(input));
}

// ── Message dispatch ──────────────────────────────────────────────────────────

std::vector<Effect> SessionStateMachine::handle_msg(const MsgIn& msg) {
  using MT = protocol::MessageType;
  switch (msg.prefix.message_type) {
    case MT::kHello: {
      auto r = protocol::decode_hello(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_hello");
      return handle_hello(*r.value_if());
    }
    case MT::kError: {
      auto r = protocol::decode_error(msg.header);
      if (r.has_value()) return handle_error(*r.value_if());
      return protocol_error("m2.sm.bad_error_msg");
    }
    case MT::kPing: {
      auto r = protocol::decode_ping(msg.header);
      if (r.has_value()) return handle_ping(*r.value_if());
      return protocol_error("m2.sm.bad_ping");
    }
    case MT::kPong:           return {};  // keepalive response; ignore
    case MT::kSessionOffer: {
      auto r = protocol::decode_session_offer(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_offer");
      return handle_session_offer(*r.value_if());
    }
    case MT::kSessionAccept: {
      auto r = protocol::decode_session_accept(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_accept");
      return handle_session_accept(*r.value_if());
    }
    case MT::kSessionReject: {
      auto r = protocol::decode_session_reject(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_reject");
      return handle_session_reject(*r.value_if());
    }
    case MT::kManifestBegin: {
      auto r = protocol::decode_manifest_begin(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_manifest_begin");
      return handle_manifest_begin(*r.value_if());
    }
    case MT::kManifestPart: {
      auto r = protocol::decode_manifest_part(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_manifest_part");
      return handle_manifest_part(*r.value_if(), msg.payload);
    }
    case MT::kManifestEnd: {
      auto r = protocol::decode_manifest_end(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_manifest_end");
      return handle_manifest_end(*r.value_if());
    }
    case MT::kTransferPlan: {
      auto r = protocol::decode_transfer_plan(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_plan");
      return handle_transfer_plan(*r.value_if());
    }
    case MT::kChunkPut: {
      auto r = protocol::decode_chunk_put(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_chunk_put");
      return handle_chunk_put(*r.value_if(), msg.payload);
    }
    case MT::kChunkAck: {
      auto r = protocol::decode_chunk_ack(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_chunk_ack");
      return handle_chunk_ack(*r.value_if());
    }
    case MT::kChunkNack: {
      auto r = protocol::decode_chunk_nack(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_chunk_nack");
      return handle_chunk_nack(*r.value_if());
    }
    case MT::kFileResult: {
      auto r = protocol::decode_file_result(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_file_result");
      return handle_file_result(*r.value_if());
    }
    case MT::kSessionResult: {
      auto r = protocol::decode_session_result(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_session_result");
      return handle_session_result(*r.value_if());
    }
    case MT::kCancel: {
      auto r = protocol::decode_cancel(msg.header);
      if (!r.has_value()) return protocol_error("m2.sm.bad_cancel");
      return handle_cancel(*r.value_if());
    }
    default:
      return protocol_error("m2.sm.unknown_msg_type");
  }
}

// ── Individual handlers ───────────────────────────────────────────────────────

std::vector<Effect> SessionStateMachine::handle_hello(const protocol::HelloMsg& msg) {
  if (state_ != State::kHelloSent) return protocol_error("m2.sm.hello_order");

  std::array<std::byte, 32> pid_bytes{};
  std::copy(msg.peer_id.begin(), msg.peer_id.end(), pid_bytes.begin());
  peer_id_ = support::PeerId{pid_bytes};

  for (std::size_t i = 0; i < 8 && i < msg.instance_nonce.size(); ++i) {
    peer_instance_nonce_ |= static_cast<std::uint64_t>(msg.instance_nonce[i]) << (i * 8U);
  }

  state_ = State::kReady;

  std::vector<Effect> effects;
  effects.emplace_back(EffCancelTimer{kTimerHandshake});
  effects.emplace_back(EffSetTimer{kTimerIdle, protocol::Limits::kIdleTimeoutMs});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_start_sender(const StartSender& cmd) {
  if (state_ != State::kReady || role_ != Role::kSender) {
    return protocol_error("m2.sm.start_sender_order");
  }

  auto header = protocol::encode_session_offer(cmd.offer);
  protocol::FramePrefix pfx;
  pfx.channel = protocol::Channel::kControl;
  pfx.message_type = protocol::MessageType::kSessionOffer;
  pfx.header_length = static_cast<std::uint32_t>(header.size());
  pfx.payload_length = 0;
  pfx.request_id = next_request_id_++;

  pending_manifest_cbor_ = cmd.manifest_cbor;
  active_offer_ = cmd.offer;
  state_ = State::kOfferSent;

  return {make_send(protocol::Channel::kControl, protocol::MessageType::kSessionOffer,
                    std::move(header))};
}

std::vector<Effect> SessionStateMachine::handle_session_offer(const protocol::SessionOfferMsg& msg) {
  if (state_ != State::kReady || role_ != Role::kReceiver) {
    return protocol_error("m2.sm.offer_order");
  }
  pending_offer_ = msg;
  state_ = State::kOfferPending;
  return {EffOfferReceived{msg}};
}

std::vector<Effect> SessionStateMachine::handle_local_accept(const LocalAccept& cmd) {
  if (state_ != State::kOfferPending || role_ != Role::kReceiver) {
    return protocol_error("m2.sm.accept_order");
  }

  transfer_id_ = cmd.accept.transfer_id;
  active_accept_ = cmd.accept;
  state_ = State::kManifestReceiving;

  auto header = protocol::encode_session_accept(cmd.accept);
  std::vector<Effect> effects;
  effects.emplace_back(make_send(protocol::Channel::kControl, protocol::MessageType::kSessionAccept,
                                  std::move(header)));
  effects.emplace_back(EffAccepted{cmd.accept});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_local_reject(const LocalReject& cmd) {
  if (state_ != State::kOfferPending || role_ != Role::kReceiver) {
    return protocol_error("m2.sm.reject_order");
  }
  state_ = State::kComplete;
  protocol::SessionRejectMsg reject_msg;
  reject_msg.reason = cmd.reason;
  auto header = protocol::encode_session_reject(reject_msg);
  std::vector<Effect> effects;
  effects.emplace_back(make_send(protocol::Channel::kControl, protocol::MessageType::kSessionReject,
                                  std::move(header)));
  effects.emplace_back(EffCloseConn{cmd.reason});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_session_accept(const protocol::SessionAcceptMsg& msg) {
  if (state_ != State::kOfferSent || role_ != Role::kSender) {
    return protocol_error("m2.sm.accept_order");
  }

  transfer_id_ = msg.transfer_id;
  active_accept_ = msg;

  // Begin manifest transfer: send MANIFEST_BEGIN, then parts.
  protocol::ManifestBeginMsg begin;
  begin.transfer_id = msg.transfer_id;
  begin.manifest_digest = {};  // caller should have computed this; zero for now
  begin.total_bytes = static_cast<std::uint64_t>(pending_manifest_cbor_.size());

  constexpr std::uint64_t kPartSize = protocol::Limits::kMaxPayloadBytes;
  const std::uint64_t total = begin.total_bytes;
  begin.part_count = static_cast<std::uint32_t>((total + kPartSize - 1U) / kPartSize);
  if (begin.part_count == 0) begin.part_count = 1;

  std::vector<Effect> effects;
  effects.emplace_back(make_send(protocol::Channel::kControl,
                                  protocol::MessageType::kManifestBegin,
                                  protocol::encode_manifest_begin(begin)));

  // Send each manifest part.
  std::uint64_t offset = 0;
  std::uint32_t part_index = 0;
  while (offset < total) {
    const std::uint64_t chunk_size = std::min(kPartSize, total - offset);
    protocol::ManifestPartMsg part;
    part.transfer_id = msg.transfer_id;
    part.part_index = part_index++;
    part.part_offset = offset;

    auto part_header = protocol::encode_manifest_part(part);
    std::vector<std::byte> part_payload(
        pending_manifest_cbor_.begin() + static_cast<std::ptrdiff_t>(offset),
        pending_manifest_cbor_.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));

    protocol::FramePrefix pfx;
    pfx.channel = protocol::Channel::kControl;
    pfx.message_type = protocol::MessageType::kManifestPart;
    pfx.header_length = static_cast<std::uint32_t>(part_header.size());
    pfx.payload_length = chunk_size;
    pfx.request_id = next_request_id_++;
    effects.emplace_back(EffSendFrame{pfx, std::move(part_header), std::move(part_payload)});

    offset += chunk_size;
  }

  // Send MANIFEST_END.
  protocol::ManifestEndMsg end;
  end.transfer_id = msg.transfer_id;
  end.manifest_digest = begin.manifest_digest;
  effects.emplace_back(make_send(protocol::Channel::kControl,
                                  protocol::MessageType::kManifestEnd,
                                  protocol::encode_manifest_end(end)));

  manifest_parts_sent_ = part_index;
  state_ = State::kPlanPending;
  effects.emplace_back(EffAccepted{msg});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_session_reject(const protocol::SessionRejectMsg& msg) {
  if (state_ != State::kOfferSent || role_ != Role::kSender) {
    return protocol_error("m2.sm.reject_order");
  }
  state_ = State::kComplete;
  return {EffRejected{msg.reason}, EffCloseConn{msg.reason}};
}

std::vector<Effect> SessionStateMachine::handle_manifest_begin(const protocol::ManifestBeginMsg& msg) {
  if (state_ != State::kManifestReceiving || role_ != Role::kReceiver) {
    return protocol_error("m2.sm.manifest_begin_order");
  }
  manifest_begin_ = msg;
  manifest_buffer_.clear();
  manifest_buffer_.reserve(static_cast<std::size_t>(msg.total_bytes));
  manifest_parts_received_ = 0;
  return {};
}

std::vector<Effect> SessionStateMachine::handle_manifest_part(const protocol::ManifestPartMsg& msg,
                                                               std::vector<std::byte> payload) {
  if (state_ != State::kManifestReceiving || role_ != Role::kReceiver || !manifest_begin_.has_value()) {
    return protocol_error("m2.sm.manifest_part_order");
  }

  if (msg.part_offset != manifest_buffer_.size()) {
    return protocol_error("m2.sm.manifest_offset");
  }

  if (manifest_buffer_.size() + payload.size() > protocol::Limits::kMaxManifestBytes) {
    return protocol_error("m2.sm.manifest_too_large");
  }

  manifest_buffer_.insert(manifest_buffer_.end(), payload.begin(), payload.end());
  ++manifest_parts_received_;

  std::vector<Effect> effects;
  effects.emplace_back(EffManifestData{msg.transfer_id,
                                       std::vector<std::byte>(payload.begin(), payload.end())});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_manifest_end(const protocol::ManifestEndMsg& msg) {
  if (state_ != State::kManifestReceiving || role_ != Role::kReceiver) {
    return protocol_error("m2.sm.manifest_end_order");
  }
  state_ = State::kPlanSending;
  // The application layer must now compute the TRANSFER_PLAN and call back
  // with a StartSender equivalent. For now we emit EffManifestData with
  // empty payload as a signal that manifest is complete.
  std::vector<Effect> effects;
  effects.emplace_back(EffManifestData{msg.transfer_id, {}});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_transfer_plan(const protocol::TransferPlanMsg& msg) {
  if (state_ != State::kPlanPending || role_ != Role::kSender) {
    return protocol_error("m2.sm.plan_order");
  }
  state_ = State::kTransferring;
  return {EffPlanReceived{msg}};
}

std::vector<Effect> SessionStateMachine::handle_chunk_put(const protocol::ChunkPutMsg& msg,
                                                           std::vector<std::byte> payload) {
  if (role_ != Role::kReceiver ||
      (state_ != State::kTransferring && state_ != State::kPlanSending)) {
    return protocol_error("m2.sm.chunk_put_order");
  }
  state_ = State::kTransferring;  // auto-transition from kPlanSending on first chunk
  // IC-007: do not send ACK now. Emit EffChunkReceived so the application layer
  // can commit blob+DB, then call process(ChunkStored{}) or ChunkStoreFailed{}.
  return {EffChunkReceived{msg.transfer_id, msg.chunk_hash, std::move(payload),
                           msg.codec, msg.plaintext_length}};
}

std::vector<Effect> SessionStateMachine::handle_chunk_stored(const ChunkStored& cmd) {
  if (role_ != Role::kReceiver || state_ != State::kTransferring) {
    return protocol_error("m2.sm.chunk_stored_order");
  }
  protocol::ChunkAckMsg ack;
  ack.transfer_id = cmd.transfer_id;
  ack.chunk_hash = cmd.chunk_hash;
  ack.status = "STORED";
  return {make_send(protocol::Channel::kData, protocol::MessageType::kChunkAck,
                    protocol::encode_chunk_ack(ack)),
          EffChunkAck{cmd.chunk_hash, "STORED"}};
}

std::vector<Effect> SessionStateMachine::handle_chunk_store_failed(const ChunkStoreFailed& cmd) {
  if (role_ != Role::kReceiver || state_ != State::kTransferring) {
    return protocol_error("m2.sm.chunk_store_failed_order");
  }
  protocol::ChunkNackMsg nack;
  nack.transfer_id = cmd.transfer_id;
  nack.chunk_hash = cmd.chunk_hash;
  nack.error_code = cmd.error_code;
  nack.retryable = cmd.retryable;
  return {make_send(protocol::Channel::kData, protocol::MessageType::kChunkNack,
                    protocol::encode_chunk_nack(nack)),
          EffChunkNack{cmd.chunk_hash, cmd.error_code, cmd.retryable}};
}

std::vector<Effect> SessionStateMachine::handle_send_file_result(const SendFileResult& cmd) {
  if (role_ != Role::kReceiver ||
      (state_ != State::kTransferring && state_ != State::kDraining)) {
    return protocol_error("m2.sm.file_result_order");
  }
  state_ = State::kDraining;
  protocol::FileResultMsg msg;
  msg.transfer_id = cmd.transfer_id;
  msg.file_id = cmd.file_id;
  msg.state = cmd.state;
  msg.error_code = cmd.error_code;
  return {make_send(protocol::Channel::kControl, protocol::MessageType::kFileResult,
                    protocol::encode_file_result(msg)),
          EffFileResult{cmd.file_id, cmd.state, cmd.error_code}};
}

std::vector<Effect> SessionStateMachine::handle_send_session_result(const SendSessionResult& cmd) {
  if (role_ != Role::kReceiver || state_ != State::kDraining) {
    return protocol_error("m2.sm.session_result_order");
  }
  state_ = State::kComplete;
  protocol::SessionResultMsg msg;
  msg.transfer_id = cmd.transfer_id;
  msg.state = cmd.state;
  msg.files_completed = cmd.files_completed;
  msg.files_failed = cmd.files_failed;
  return {make_send(protocol::Channel::kControl, protocol::MessageType::kSessionResult,
                    protocol::encode_session_result(msg)),
          EffSessionResult{cmd.transfer_id, cmd.state},
          EffCancelTimer{kTimerIdle},
          EffCloseConn{"session complete"}};
}

std::vector<Effect> SessionStateMachine::handle_chunk_ack(const protocol::ChunkAckMsg& msg) {
  if (state_ != State::kTransferring || role_ != Role::kSender) {
    return protocol_error("m2.sm.chunk_ack_order");
  }
  return {EffChunkAck{msg.chunk_hash, msg.status}};
}

std::vector<Effect> SessionStateMachine::handle_chunk_nack(const protocol::ChunkNackMsg& msg) {
  if (state_ != State::kTransferring || role_ != Role::kSender) {
    return protocol_error("m2.sm.chunk_nack_order");
  }
  return {EffChunkNack{msg.chunk_hash, msg.error_code, msg.retryable}};
}

std::vector<Effect> SessionStateMachine::handle_file_result(const protocol::FileResultMsg& msg) {
  if (role_ == Role::kSender && state_ != State::kDraining && state_ != State::kTransferring) {
    return protocol_error("m2.sm.file_result_order");
  }
  state_ = State::kDraining;
  return {EffFileResult{msg.file_id, msg.state, msg.error_code}};
}

std::vector<Effect> SessionStateMachine::handle_session_result(const protocol::SessionResultMsg& msg) {
  if (state_ != State::kDraining && state_ != State::kTransferring) {
    return protocol_error("m2.sm.session_result_order");
  }
  state_ = State::kComplete;
  std::vector<Effect> effects;
  effects.emplace_back(EffSessionResult{msg.transfer_id, msg.state});
  effects.emplace_back(EffCancelTimer{kTimerIdle});
  effects.emplace_back(EffCloseConn{"session complete"});
  return effects;
}

std::vector<Effect> SessionStateMachine::handle_cancel(const protocol::CancelMsg& msg) {
  state_ = State::kCancelled;
  return {EffCloseConn{msg.reason}};
}

std::vector<Effect> SessionStateMachine::handle_error(const protocol::ErrorMsg& msg) {
  state_ = State::kError;
  return {EffProtocolError{msg.error_code}, EffCloseConn{msg.error_code}};
}

std::vector<Effect> SessionStateMachine::handle_ping(const protocol::PingMsg& msg) {
  protocol::PongMsg pong;
  pong.monotonic_token = msg.monotonic_token;
  return {make_send(protocol::Channel::kControl, protocol::MessageType::kPong,
                    protocol::encode_pong(pong))};
}

// ── Helpers ───────────────────────────────────────────────────────────────────

std::vector<Effect> SessionStateMachine::protocol_error(const char* code) {
  state_ = State::kError;
  std::vector<Effect> effects;

  protocol::ErrorMsg err;
  err.error_code = code;
  err.retryable = false;
  effects.emplace_back(make_send(protocol::Channel::kControl, protocol::MessageType::kError,
                                  protocol::encode_error(err)));
  effects.emplace_back(EffProtocolError{std::string{code}});
  effects.emplace_back(EffCloseConn{std::string{code}});
  return effects;
}

EffSendFrame SessionStateMachine::make_send(protocol::Channel ch, protocol::MessageType mt,
                                             std::vector<std::byte> header,
                                             std::vector<std::byte> payload) {
  protocol::FramePrefix pfx;
  pfx.channel = ch;
  pfx.message_type = mt;
  pfx.header_length = static_cast<std::uint32_t>(header.size());
  pfx.payload_length = static_cast<std::uint64_t>(payload.size());
  pfx.request_id = next_request_id_++;
  return EffSendFrame{pfx, std::move(header), std::move(payload)};
}

}  // namespace spoink::session
