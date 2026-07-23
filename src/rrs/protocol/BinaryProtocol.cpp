#include "rrs/protocol/BinaryProtocol.h"

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/PlayerInput.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace rrs {

namespace {

constexpr std::size_t kLengthFieldSize = sizeof(std::uint32_t);
constexpr std::size_t kMessageTypeSize = sizeof(std::uint8_t);
constexpr std::size_t kInputPayloadSize = sizeof(std::int16_t) * 2 + sizeof(std::uint8_t);
constexpr std::size_t kSnapshotFixedPayloadSize =
    sizeof(std::uint16_t) + sizeof(std::uint8_t) * 3 + sizeof(std::uint16_t);
constexpr std::size_t kSnapshotPlayerFixedSize =
    sizeof(std::uint64_t) + sizeof(std::uint16_t) * 2;
constexpr std::size_t kSnapshotBallSize = sizeof(std::int16_t) * 2 + sizeof(std::uint16_t);
constexpr std::size_t kSnapshotFoodSize = sizeof(std::uint16_t) + sizeof(std::int16_t) * 2;
constexpr std::uint32_t kMaxClientFrameLength =
    kMessageTypeSize + sizeof(std::uint64_t);
constexpr std::uint8_t kWinnerPresentFlag = 1U << 0U;

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
    return length >= kMessageTypeSize && length <= kMaxClientFrameLength;
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

std::optional<PlayerId> DecodeJoinRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kJoin)
        || frame.payload.size() != sizeof(std::uint64_t)) {
        return std::nullopt;
    }

    return PlayerId{ReadU64At(frame.payload, 0)};
}

std::optional<SessionId> DecodeReconnectRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kReconnect)
        || frame.payload.size() != sizeof(std::uint64_t)) {
        return std::nullopt;
    }

    return SessionId{ReadU64At(frame.payload, 0)};
}

std::optional<PlayerInput> DecodeInputRequest(const BinaryFrame& frame)
{
    if (frame.message_type != static_cast<std::uint8_t>(ClientMessageType::kInput)
        || frame.payload.size() != kInputPayloadSize
        || (static_cast<std::uint8_t>(static_cast<unsigned char>(frame.payload[4]))
            & static_cast<std::uint8_t>(~PlayerInput::kSplitFlag)) != 0) {
        return std::nullopt;
    }

    return PlayerInput{
        .move_x = ReadI16At(frame.payload, 0),
        .move_y = ReadI16At(frame.payload, 2),
        .input_flags = static_cast<std::uint8_t>(static_cast<unsigned char>(frame.payload[4])),
    };
}

bool IsValidLeaveRequest(const BinaryFrame& frame)
{
    return frame.message_type == static_cast<std::uint8_t>(ClientMessageType::kLeave)
        && frame.payload.empty();
}

std::string EncodeFrame(ServerMessageType message_type, std::string_view payload)
{
    std::string output;
    const auto length = static_cast<std::uint32_t>(kMessageTypeSize + payload.size());
    output.reserve(kLengthFieldSize + length);
    AppendU32(output, length);
    AppendU8(output, static_cast<std::uint8_t>(message_type));
    output.append(payload);
    return output;
}

std::string EncodeFullSnapshotPayload(SessionId session_id, std::string_view snapshot_payload)
{
    std::string output;
    output.reserve(sizeof(std::uint64_t) + snapshot_payload.size());
    AppendU64(output, session_id.value());
    output.append(snapshot_payload);
    return output;
}

std::string EncodeSnapshotPayload(
    const SnapshotUpdate& update,
    std::span<const FoodSnapshotUpdate> food_updates)
{
    std::string output;
    auto player_payload_size = std::size_t{0};
    for (const auto& player : update.player_updates) {
        player_payload_size += kSnapshotPlayerFixedSize + kSnapshotBallSize * std::popcount(player.changed_ball_mask);
    }
    output.reserve(
        kSnapshotFixedPayloadSize
        + player_payload_size
        + update.removed_player_ids.size() * sizeof(std::uint64_t)
        + food_updates.size() * kSnapshotFoodSize
        + (update.winner_player_id.has_value() ? sizeof(std::uint64_t) : 0));

    AppendU16(output, static_cast<std::uint16_t>(update.tick_seq));
    auto flags = std::uint8_t{0};
    if (update.winner_player_id) {
        flags = static_cast<std::uint8_t>(flags | kWinnerPresentFlag);
    }
    AppendU8(output, flags);

    AppendU8(output, static_cast<std::uint8_t>(update.player_updates.size()));
    for (const auto& player : update.player_updates) {
        AppendU64(output, player.player_id.value());
        AppendU16(output, player.visible_ball_mask);
        AppendU16(output, player.changed_ball_mask);
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            const auto ball_mask = static_cast<std::uint16_t>(1U << ball_index);
            if ((player.changed_ball_mask & ball_mask) == 0) {
                continue;
            }

            const auto& ball = player.balls[ball_index];
            AppendI16(output, ball.position.x);
            AppendI16(output, ball.position.y);
            AppendU16(output, ball.radius);
        }
    }

    AppendU8(output, static_cast<std::uint8_t>(update.removed_player_ids.size()));
    for (const auto player_id : update.removed_player_ids) {
        AppendU64(output, player_id.value());
    }

    AppendU16(output, static_cast<std::uint16_t>(food_updates.size()));
    for (const auto& food : food_updates) {
        AppendU16(output, food.food_index);
        AppendI16(output, food.position.x);
        AppendI16(output, food.position.y);
    }

    if (update.winner_player_id) {
        AppendU64(output, update.winner_player_id->value());
    }
    return output;
}

} // namespace rrs
