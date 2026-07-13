#pragma once

#include "rrs/base/Types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace rrs {

struct RoomSnapshot;

enum class ClientMessageType : std::uint8_t {
    kJoin = 1,
    kReconnect = 2,
    kInput = 3,
    kLeave = 4,
};

enum class ServerMessageType : std::uint8_t {
    kJoinOk = 101,
    kReconnectOk = 102,
    kSnapshot = 103,
    kError = 104,
};

struct BinaryFrame {
    std::uint8_t message_type{0};
    std::string payload;
};

enum class BinaryFrameDecodeStatus {
    kIncomplete,
    kComplete,
    kInvalid,
};

struct BinaryJoinRequest {
    PlayerId player_id;
};

struct BinaryReconnectRequest {
    SessionId session_id;
};

struct BinaryInputRequest {
    std::int16_t move_x{0};
    std::int16_t move_y{0};
};

struct BinaryLeaveRequest {
};

[[nodiscard]] BinaryFrameDecodeStatus TryDecodeBinaryFrame(std::string& buffer, BinaryFrame& output);

[[nodiscard]] std::optional<BinaryJoinRequest> DecodeJoinRequest(const BinaryFrame& frame);
[[nodiscard]] std::optional<BinaryReconnectRequest> DecodeReconnectRequest(const BinaryFrame& frame);
[[nodiscard]] std::optional<BinaryInputRequest> DecodeInputRequest(const BinaryFrame& frame);
[[nodiscard]] std::optional<BinaryLeaveRequest> DecodeLeaveRequest(const BinaryFrame& frame);

[[nodiscard]] std::string EncodeFrame(ServerMessageType message_type, const std::string& payload);
[[nodiscard]] std::string EncodeJoinOkPayload(SessionId session_id, Generation generation, const std::string& snapshot_payload);
[[nodiscard]] std::string EncodeReconnectOkPayload(SessionId session_id, Generation generation, const std::string& snapshot_payload);
[[nodiscard]] std::string EncodeErrorPayload(const std::string& message);
[[nodiscard]] std::string EncodeSnapshotPayload(const RoomSnapshot& snapshot);

} // namespace rrs
