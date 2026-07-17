#include "rrs/simulation/spatial/PlayerBallSpatialIndex.h"

#include "rrs/math/Vector2.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/spatial/UniformGrid.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr auto kRoomBounds = rrs::Aabb{
    .min = rrs::Vector2{.x = -1024.0F, .y = -1024.0F},
    .max = rrs::Vector2{.x = 1024.0F, .y = 1024.0F},
};

void Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <std::size_t Size>
void ExpectLocators(
    std::span<const rrs::PlayerBallLocator> actual,
    const std::array<rrs::PlayerBallLocator, Size>& expected,
    std::string_view message)
{
    Expect(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        Expect(
            actual[index].player_index == expected[index].player_index
                && actual[index].ball_index == expected[index].ball_index,
            message);
    }
}

rrs::PlayerBallSpatialIndex MakeIndex()
{
    return rrs::PlayerBallSpatialIndex{rrs::UniformGridLayout{kRoomBounds, 64.0F}};
}

std::vector<rrs::PlayerEntity> MakePlayers()
{
    auto players = std::vector<rrs::PlayerEntity>(3);
    players[0].active_ball_mask = static_cast<std::uint16_t>((1U << 0U) | (1U << 2U));
    players[0].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -970.0F, .y = -1000.0F},
        .radius = 5.0F,
    };
    players[0].balls[2] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -950.0F, .y = -1000.0F},
        .radius = 5.0F,
    };
    players[1].active_ball_mask = static_cast<std::uint16_t>(1U << 1U);
    players[1].balls[1] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -965.0F, .y = -1000.0F},
        .radius = 10.0F,
    };
    players[2].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -960.0F, .y = -1000.0F},
        .radius = 10.0F,
    };
    return players;
}

void TestActiveBallMappingAndOrder()
{
    auto index = MakeIndex();
    const auto players = MakePlayers();
    index.Rebuild(players);

    ExpectLocators(
        index.QueryCandidates(rrs::Vector2{.x = -960.0F, .y = -1000.0F}, 64.0F),
        std::array{
            rrs::PlayerBallLocator{.player_index = 0, .ball_index = 0},
            rrs::PlayerBallLocator{.player_index = 0, .ball_index = 2},
            rrs::PlayerBallLocator{.player_index = 1, .ball_index = 1},
        },
        "active balls map back in player and ball order");
}

void TestRebuildReplacesInactiveAndMovedBalls()
{
    auto index = MakeIndex();
    auto players = MakePlayers();
    index.Rebuild(players);

    players[0].active_ball_mask = static_cast<std::uint16_t>(1U << 2U);
    players[0].balls[2].position = rrs::Vector2{.x = 900.0F, .y = 900.0F};
    players[1].active_ball_mask = 0;
    index.Rebuild(players);

    Expect(
        index.QueryCandidates(rrs::Vector2{.x = -960.0F, .y = -1000.0F}, 64.0F).empty(),
        "rebuild removes inactive and old ball positions");
    ExpectLocators(
        index.QueryCandidates(rrs::Vector2{.x = 900.0F, .y = 900.0F}, 1.0F),
        std::array{rrs::PlayerBallLocator{.player_index = 0, .ball_index = 2}},
        "rebuild indexes the moved active ball");
}

} // namespace

int main()
{
    TestActiveBallMappingAndOrder();
    TestRebuildReplacesInactiveAndMovedBalls();
    std::cout << "PlayerBallSpatialIndex tests passed\n";
    return EXIT_SUCCESS;
}
