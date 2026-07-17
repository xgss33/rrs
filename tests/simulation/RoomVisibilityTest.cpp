#include "rrs/simulation/RoomVisibility.h"

#include "rrs/simulation/RoomRules.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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

rrs::UniformGridLayout MakeLayout()
{
    return rrs::UniformGridLayout{kRoomBounds, rrs::room_rules::kSpatialGridCellSize};
}

const rrs::VisiblePlayerBalls* FindVisiblePlayer(const rrs::VisibleEntitySet& visible, rrs::PlayerId player_id)
{
    const auto iterator = std::find_if(
        visible.players.begin(),
        visible.players.end(),
        [player_id](const rrs::VisiblePlayerBalls& player) {
            return player.player_id == player_id;
        });
    return iterator != visible.players.end() ? &(*iterator) : nullptr;
}

bool ContainsFood(const rrs::VisibleEntitySet& visible, rrs::FoodId food_id)
{
    return std::find(visible.food_ids.begin(), visible.food_ids.end(), food_id) != visible.food_ids.end();
}

void RebuildSpatialIndexes(
    const std::vector<rrs::PlayerEntity>& players,
    const std::vector<rrs::FoodEntity>& foods,
    rrs::PlayerBallSpatialIndex& player_index,
    rrs::FoodSpatialIndex& food_index)
{
    player_index.Rebuild(players);
    food_index.Rebuild(foods);
}

void TestMultiBallUnionAndPerBallFiltering()
{
    auto players = std::vector<rrs::PlayerEntity>(2);
    players[0].player_id = rrs::PlayerId{1};
    players[0].active_ball_mask = static_cast<std::uint16_t>((1U << 0U) | (1U << 1U));
    players[0].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -700.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    players[0].balls[1] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = 700.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    players[1].player_id = rrs::PlayerId{2};
    players[1].active_ball_mask = static_cast<std::uint16_t>((1U << 0U) | (1U << 1U) | (1U << 2U));
    players[1].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = -300.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    players[1].balls[1] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = 300.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    players[1].balls[2] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = 0.0F, .y = 700.0F},
        .radius = 12.0F,
    };

    const auto foods = std::vector<rrs::FoodEntity>{
        rrs::FoodEntity{.food_id = rrs::FoodId{1}, .position = rrs::Vector2{.x = -310.0F, .y = 0.0F}},
        rrs::FoodEntity{.food_id = rrs::FoodId{2}, .position = rrs::Vector2{.x = 310.0F, .y = 0.0F}},
        rrs::FoodEntity{.food_id = rrs::FoodId{3}, .position = rrs::Vector2{.x = 0.0F, .y = 700.0F}},
    };

    auto player_index = rrs::PlayerBallSpatialIndex{MakeLayout()};
    auto food_index = rrs::FoodSpatialIndex{MakeLayout()};
    RebuildSpatialIndexes(players, foods, player_index, food_index);

    auto visibility = rrs::RoomVisibility{};
    const auto& visible = visibility.Update(0, players, foods, player_index, food_index);
    const auto* self = FindVisiblePlayer(visible, rrs::PlayerId{1});
    const auto* target = FindVisiblePlayer(visible, rrs::PlayerId{2});
    Expect(self != nullptr && self->ball_mask == players[0].active_ball_mask, "all observer balls stay visible");
    Expect(target != nullptr && target->ball_mask == static_cast<std::uint16_t>((1U << 0U) | (1U << 1U)),
           "visible target balls are the union of observer ball views");
    Expect(ContainsFood(visible, rrs::FoodId{1}), "food in first observer ball view");
    Expect(ContainsFood(visible, rrs::FoodId{2}), "food in second observer ball view");
    Expect(!ContainsFood(visible, rrs::FoodId{3}), "food outside the view union");
}

void TestEnterLeaveHysteresisAndObserverReset()
{
    auto players = std::vector<rrs::PlayerEntity>(2);
    players[0].player_id = rrs::PlayerId{1};
    players[0].active_ball_mask = 1;
    players[0].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{},
        .radius = 12.0F,
    };
    players[1].player_id = rrs::PlayerId{2};
    players[1].active_ball_mask = 1;
    players[1].balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = 400.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    auto foods = std::vector<rrs::FoodEntity>{
        rrs::FoodEntity{.food_id = rrs::FoodId{1}, .position = rrs::Vector2{.x = 390.0F, .y = 0.0F}},
    };

    auto player_index = rrs::PlayerBallSpatialIndex{MakeLayout()};
    auto food_index = rrs::FoodSpatialIndex{MakeLayout()};
    auto visibility = rrs::RoomVisibility{};

    RebuildSpatialIndexes(players, foods, player_index, food_index);
    const auto& entered = visibility.Update(0, players, foods, player_index, food_index);
    Expect(FindVisiblePlayer(entered, rrs::PlayerId{2}) != nullptr, "target enters at enter distance");
    Expect(ContainsFood(entered, rrs::FoodId{1}), "food enters at enter distance");

    players[1].balls[0].position.x = 430.0F;
    foods[0].position.x = 420.0F;
    RebuildSpatialIndexes(players, foods, player_index, food_index);
    const auto& retained = visibility.Update(0, players, foods, player_index, food_index);
    Expect(FindVisiblePlayer(retained, rrs::PlayerId{2}) != nullptr, "target remains inside leave distance");
    Expect(ContainsFood(retained, rrs::FoodId{1}), "food remains inside leave distance");

    visibility.RemoveObserver(rrs::PlayerId{1});
    const auto& reset = visibility.Update(0, players, foods, player_index, food_index);
    Expect(FindVisiblePlayer(reset, rrs::PlayerId{2}) == nullptr, "reset target must satisfy enter distance again");
    Expect(!ContainsFood(reset, rrs::FoodId{1}), "reset food must satisfy enter distance again");
}

void TestDeadObserverStillHasOwnPlayerEntry()
{
    auto players = std::vector<rrs::PlayerEntity>(1);
    players[0].player_id = rrs::PlayerId{1};
    const auto foods = std::vector<rrs::FoodEntity>{};

    auto player_index = rrs::PlayerBallSpatialIndex{MakeLayout()};
    auto food_index = rrs::FoodSpatialIndex{MakeLayout()};
    RebuildSpatialIndexes(players, foods, player_index, food_index);

    auto visibility = rrs::RoomVisibility{};
    const auto& visible = visibility.Update(0, players, foods, player_index, food_index);
    const auto* self = FindVisiblePlayer(visible, rrs::PlayerId{1});
    Expect(self != nullptr && self->ball_mask == 0, "dead observer keeps an empty self entry");
}

} // namespace

int main()
{
    TestMultiBallUnionAndPerBallFiltering();
    TestEnterLeaveHysteresisAndObserverReset();
    TestDeadObserverStillHasOwnPlayerEntry();
    std::cout << "RoomVisibility tests passed\n";
    return EXIT_SUCCESS;
}
