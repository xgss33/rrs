#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/PlayerInput.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

[[nodiscard]] BinaryFrameDecodeStatus TryDecodeBinaryFrame(std::string& buffer, BinaryFrame& output);

[[nodiscard]] std::optional<PlayerId> DecodeJoinRequest(const BinaryFrame& frame);
[[nodiscard]] std::optional<SessionId> DecodeReconnectRequest(const BinaryFrame& frame);
[[nodiscard]] std::optional<PlayerInput> DecodeInputRequest(const BinaryFrame& frame);
[[nodiscard]] bool IsValidLeaveRequest(const BinaryFrame& frame);

[[nodiscard]] std::string EncodeFrame(ServerMessageType message_type, std::string_view payload);
[[nodiscard]] std::string EncodeSessionPayload(SessionId session_id, Generation generation, std::string_view snapshot_payload);
[[nodiscard]] std::string EncodeSnapshotPayload(const RoomSnapshot& snapshot);

} // namespace rrs
