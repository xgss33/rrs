#include "rrs/net/BinaryProtocol.h"

#include "rrs/game/RoomRules.h"
#include "rrs/game/RoomSnapshot.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rrs {

namespace {

constexpr std::size_t kLengthFieldSize = 4;
constexpr std::size_t kMessageTypeSize = 1;
constexpr std::uint32_t kMaxFrameLength = 64 * 1024;
constexpr float kEncodedPositionMin = -room_rules::kRoomHalfExtent;
constexpr float kEncodedPositionMax = room_rules::kRoomHalfExtent;
constexpr float kEncodedRadiusMin = 0.0F;
constexpr float kEncodedRadiusMax = 1024.0F;
constexpr std::uint8_t kPlayerAliveFlag = 1U << 0U;

void AppendU8(std::string& output, std::uint8_t value)
{
    output.push_back(static_cast<char>(value));
}

void AppendU16(std::string& output, std::uint16_t value)
{
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

void AppendI16(std::string& output, std::int16_t value)
{
    AppendU16(output, static_cast<std::uint16_t>(value));
}

void AppendU32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

void AppendU64(std::string& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

std::int16_t EncodePosition(float value)
{
    const auto clamped = std::clamp(value, kEncodedPositionMin, kEncodedPositionMax);
    const auto normalized = (clamped - kEncodedPositionMin) / (kEncodedPositionMax - kEncodedPositionMin);
    const auto encoded = std::lround(normalized * static_cast<float>(std::numeric_limits<std::uint16_t>::max()))
        + static_cast<long>(std::numeric_limits<std::int16_t>::min());
    return static_cast<std::int16_t>(encoded);
}

std::uint16_t EncodeRadius(float value)
{
    const auto clamped = std::clamp(value, kEncodedRadiusMin, kEncodedRadiusMax);
    const auto normalized = (clamped - kEncodedRadiusMin) / (kEncodedRadiusMax - kEncodedRadiusMin);
    return static_cast<std::uint16_t>(std::lround(normalized * static_cast<float>(std::numeric_limits<std::uint16_t>::max())));
}

std::uint32_t ReadU32At(const std::string& input, std::size_t offset)
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset])) << 24U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 1])) << 16U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 2])) << 8U)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 3]));
}

std::uint64_t ReadU64At(const std::string& input, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(static_cast<unsigned char>(input[offset + index]));
    }
    return value;
}

std::int16_t ReadI16At(const std::string& input, std::size_t offset)
{
    const auto value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset])) << 8U)
        | static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset + 1])));
    return static_cast<std::int16_t>(value);
}

bool IsFrameLengthValid(std::uint32_t length)
{
    return length >= kMessageTypeSize && length <= kMaxFrameLength;
}

} // namespace

BinaryFrameDecodeStatus TryDecodeBinaryFrame(std::string& buffer, BinaryFrame& output)
{
    if (buffer.size() < kLengthFieldSize) {
        return BinaryFrameDecodeStatus::kIncomplete;
    }

    const auto length = ReadU32At(buffer, 0);
    if (!IsFrameLengthValid(length)) {
        return BinaryFrameDecodeStatus::kInvalid;
    }

    const auto frame_size = kLengthFieldSize + static_cast<std::size_t>(length);
    if (buffer.size() < frame_size) {
        return BinaryFrameDecodeStatus::kIncomplete;
    }

    output = BinaryFrame{
        .message_type = static_cast<std::uint8_t>(buffer[kLengthFieldSize]),
        .payload = buffer.substr(kLengthFieldSize + kMessageTypeSize, static_cast<std::size_t>(length) - kMessageTypeSize),
    };
    buffer.erase(0, frame_size);
    return BinaryFrameDecodeStatus::kComplete;
}

std::optional<BinaryJoinRequest> DecodeJoinRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kJoin) || frame.payload.size() != 8) {
        return std::nullopt;
    }

    return BinaryJoinRequest{
        .player_id = PlayerId{ReadU64At(frame.payload, 0)},
    };
}

std::optional<BinaryReconnectRequest> DecodeReconnectRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kReconnect) || frame.payload.size() != 8) {
        return std::nullopt;
    }

    return BinaryReconnectRequest{
        .session_id = SessionId{ReadU64At(frame.payload, 0)},
    };
}

std::optional<BinaryInputRequest> DecodeInputRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kInput) || frame.payload.size() != 4) {
        return std::nullopt;
    }

    return BinaryInputRequest{
        .move_x = ReadI16At(frame.payload, 0),
        .move_y = ReadI16At(frame.payload, 2),
    };
}

std::optional<BinaryLeaveRequest> DecodeLeaveRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kLeave) || !frame.payload.empty()) {
        return std::nullopt;
    }

    return BinaryLeaveRequest{};
}

std::string EncodeFrame(ServerMessageType message_type, const std::string& payload)
{
    std::string output;
    const auto length = static_cast<std::uint32_t>(kMessageTypeSize + payload.size());
    output.reserve(kLengthFieldSize + length);
    AppendU32(output, length);
    AppendU8(output, static_cast<std::uint8_t>(message_type));
    output.append(payload);
    return output;
}

std::string EncodeJoinOkPayload(SessionId session_id, Generation generation, const std::string& snapshot_payload)
{
    std::string output;
    output.reserve(16 + snapshot_payload.size());
    AppendU64(output, session_id.value());
    AppendU64(output, generation);
    output.append(snapshot_payload);
    return output;
}

std::string EncodeReconnectOkPayload(SessionId session_id, Generation generation, const std::string& snapshot_payload)
{
    std::string output;
    output.reserve(16 + snapshot_payload.size());
    AppendU64(output, session_id.value());
    AppendU64(output, generation);
    output.append(snapshot_payload);
    return output;
}

std::string EncodeErrorPayload(const std::string& message)
{
    return message;
}

std::string EncodeSnapshotPayload(const RoomSnapshot& snapshot)
{
    std::string output;
    output.reserve(13 + snapshot.players.size() * 15 + snapshot.foods.size() * 6);
    AppendU16(output, static_cast<std::uint16_t>(snapshot.tick_seq));
    AppendU8(output, static_cast<std::uint8_t>(snapshot.players.size()));
    for (const auto& player : snapshot.players) {
        AppendU64(output, player.player_id.value());
        AppendI16(output, EncodePosition(player.position.x));
        AppendI16(output, EncodePosition(player.position.y));
        AppendU16(output, EncodeRadius(player.radius));
        AppendU8(output, player.alive ? kPlayerAliveFlag : 0);
    }

    AppendU16(output, static_cast<std::uint16_t>(snapshot.foods.size()));
    for (const auto& food : snapshot.foods) {
        AppendU16(output, static_cast<std::uint16_t>(food.food_id.value()));
        AppendI16(output, EncodePosition(food.position.x));
        AppendI16(output, EncodePosition(food.position.y));
    }

    AppendU64(output, snapshot.winner_player_id.value());
    return output;
}

} // namespace rrs
