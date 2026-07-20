#include "rrs/protocol/BinaryProtocol.h"

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerInput.h"
#include "rrs/synchronization/SnapshotQuantization.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void AppendU8(std::string& output, std::uint8_t value)
{
    output.push_back(static_cast<char>(value));
}

void AppendU16(std::string& output, std::uint16_t value)
{
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>(value & 0xFFU));
}

void AppendU64(std::string& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

void TestRequestPayloadSizes()
{
    auto join = rrs::BinaryFrame{
        .message_type = static_cast<std::uint8_t>(rrs::ClientMessageType::kJoin),
        .payload = std::string(8, '\0'),
    };
    join.payload[7] = 1;
    Expect(rrs::DecodeJoinRequest(join) == rrs::PlayerId{1}, "JOIN payload remains 8 bytes");
    join.payload.push_back('\0');
    Expect(!rrs::DecodeJoinRequest(join), "JOIN rejects non-8-byte payload");

    auto reconnect = rrs::BinaryFrame{
        .message_type = static_cast<std::uint8_t>(rrs::ClientMessageType::kReconnect),
        .payload = std::string(8, '\0'),
    };
    reconnect.payload[7] = 2;
    Expect(rrs::DecodeReconnectRequest(reconnect) == rrs::SessionId{2}, "RECONNECT payload remains 8 bytes");
    reconnect.payload.pop_back();
    Expect(!rrs::DecodeReconnectRequest(reconnect), "RECONNECT rejects non-8-byte payload");

    auto input = rrs::BinaryFrame{
        .message_type = static_cast<std::uint8_t>(rrs::ClientMessageType::kInput),
        .payload = std::string{"\x12\x34\xFE\xDC\x01", 5},
    };
    const auto decoded_input = rrs::DecodeInputRequest(input);
    Expect(decoded_input.has_value()
               && decoded_input->move_x == static_cast<std::int16_t>(0x1234)
               && decoded_input->move_y == static_cast<std::int16_t>(0xFEDC)
               && decoded_input->input_flags == 1,
           "INPUT payload remains 5 bytes and big-endian");
    input.payload.pop_back();
    Expect(!rrs::DecodeInputRequest(input), "INPUT rejects non-5-byte payload");

    const auto leave = rrs::BinaryFrame{
        .message_type = static_cast<std::uint8_t>(rrs::ClientMessageType::kLeave),
        .payload = {},
    };
    Expect(rrs::IsValidLeaveRequest(leave), "LEAVE payload remains empty");
}

void TestSessionPrefix()
{
    const auto payload = rrs::EncodeSessionPayload(
        rrs::SessionId{0x0102030405060708ULL},
        0x1112131415161718ULL,
        "snapshot");
    auto expected = std::string{};
    AppendU64(expected, 0x0102030405060708ULL);
    AppendU64(expected, 0x1112131415161718ULL);
    expected += "snapshot";
    Expect(payload == expected, "JOIN_OK and RECONNECT_OK keep the 16-byte session prefix");
}

void TestSnapshotByteLayout()
{
    auto player = rrs::PlayerSnapshotUpdate{
        .player_id = rrs::PlayerId{0x0102030405060708ULL},
        .visible_ball_mask = 3,
        .changed_ball_mask = 2,
        .balls = {},
    };
    player.balls[1] = rrs::QuantizedBallState{
        .position = rrs::QuantizedPosition{.x = -2, .y = 0x1234},
        .radius = 0xABCD,
    };

    const auto update = rrs::SnapshotUpdate{
        .tick_seq = 0x1234,
        .full_reset = true,
        .player_updates = {player},
        .removed_player_ids = {rrs::PlayerId{0x1112131415161718ULL}},
        .winner_player_id = rrs::PlayerId{0x2122232425262728ULL},
    };
    const auto food_updates = std::vector{
        rrs::FoodSnapshotUpdate{
            .food_index = 0,
            .position = rrs::QuantizedPosition{.x = -3, .y = 0x2345},
        },
    };

    auto expected = std::string{};
    AppendU16(expected, 0x1234);
    AppendU8(expected, 3);
    AppendU8(expected, 1);
    AppendU64(expected, 0x0102030405060708ULL);
    AppendU16(expected, 3);
    AppendU16(expected, 2);
    AppendU16(expected, static_cast<std::uint16_t>(-2));
    AppendU16(expected, 0x1234);
    AppendU16(expected, 0xABCD);
    AppendU8(expected, 1);
    AppendU64(expected, 0x1112131415161718ULL);
    AppendU16(expected, 1);
    AppendU16(expected, 0);
    AppendU16(expected, static_cast<std::uint16_t>(-3));
    AppendU16(expected, 0x2345);
    AppendU64(expected, 0x2122232425262728ULL);

    Expect(rrs::EncodeSnapshotPayload(update, food_updates) == expected, "Snapshot update byte layout is exact");
}

void TestFrameLayout()
{
    const auto frame = rrs::EncodeFrame(rrs::ServerMessageType::kSnapshot, "abc");
    const auto expected = std::string{"\0\0\0\4\x67" "abc", 8};
    Expect(frame == expected, "outer binary frame layout remains unchanged");
}

} // namespace

int main()
{
    TestRequestPayloadSizes();
    TestSessionPrefix();
    TestSnapshotByteLayout();
    TestFrameLayout();
    std::cout << "BinaryProtocol tests passed\n";
    return EXIT_SUCCESS;
}
