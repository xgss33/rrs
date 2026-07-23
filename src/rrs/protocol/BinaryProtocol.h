#pragma once

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerInput.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rrs {

struct SnapshotUpdate;
struct FoodSnapshotUpdate;

enum class ClientMessageType : std::uint8_t {
    kJoin = 1,
    kReconnect = 2,
    kInput = 3,
    kLeave = 4,
};

enum class ServerMessageType : std::uint8_t {
    kFullSnapshot = 101,
    kDeltaSnapshot = 102,
    kError = 103,
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
[[nodiscard]] std::string EncodeFullSnapshotPayload(SessionId session_id, std::string_view snapshot_payload);
[[nodiscard]] std::string EncodeSnapshotPayload(
    const SnapshotUpdate& update,
    std::span<const FoodSnapshotUpdate> food_updates);

} // namespace rrs
