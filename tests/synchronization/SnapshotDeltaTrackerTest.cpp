#include "rrs/synchronization/SnapshotDeltaTracker.h"

#include "rrs/core/Identifiers.h"
#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/simulation/PlayerEntity.h"
#include "rrs/simulation/RoomVisibility.h"
#include "rrs/synchronization/SnapshotUpdate.h"

#include <cstdlib>
#include <iostream>
#include <optional>
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

rrs::PlayerEntity MakePlayer(rrs::PlayerId player_id, float x, std::uint16_t ball_mask = 1)
{
    auto player = rrs::PlayerEntity{};
    player.player_id = player_id;
    player.active_ball_mask = ball_mask;
    player.balls[0] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = x, .y = 0.0F},
        .radius = 12.0F,
    };
    player.balls[1] = rrs::PlayerBall{
        .position = rrs::Vector2{.x = x + 20.0F, .y = 0.0F},
        .radius = 12.0F,
    };
    return player;
}

rrs::VisibleEntitySet MakeVisible(std::uint16_t target_ball_mask = 1)
{
    return rrs::VisibleEntitySet{
        .players = {
            rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{1}, .ball_mask = 1},
            rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{2}, .ball_mask = target_ball_mask},
        },
        .food_ids = {rrs::FoodId{1}, rrs::FoodId{2}},
    };
}

void TestFullResetAndUnchangedDelta()
{
    auto players = std::vector<rrs::PlayerEntity>{
        MakePlayer(rrs::PlayerId{1}, 0.0F),
        MakePlayer(rrs::PlayerId{2}, 100.0F),
    };
    auto foods = std::vector<rrs::FoodEntity>{
        rrs::FoodEntity{.food_id = rrs::FoodId{1}, .position = rrs::Vector2{.x = 10.0F, .y = 20.0F}},
        rrs::FoodEntity{.food_id = rrs::FoodId{2}, .position = rrs::Vector2{.x = 30.0F, .y = 40.0F}},
    };
    const auto visible = MakeVisible();
    auto tracker = rrs::SnapshotDeltaTracker{};

    const auto full = tracker.BuildUpdate(rrs::PlayerId{1}, 1, visible, players, foods, std::nullopt, true);
    Expect(full.has_value() && full->full_reset, "full reset is always emitted");
    Expect(full->player_updates.size() == 2, "full reset contains every visible player");
    Expect(full->player_updates[0].changed_ball_mask == full->player_updates[0].visible_ball_mask,
           "full reset carries every visible player ball");
    Expect(full->food_updates.size() == 2, "full reset contains every visible food");

    const auto unchanged = tracker.BuildUpdate(rrs::PlayerId{1}, 2, visible, players, foods, std::nullopt, false);
    Expect(!unchanged.has_value(), "unchanged quantized state suppresses snapshot");

    players[0].balls[0].position.x += 0.0001F;
    const auto same_quantized = tracker.BuildUpdate(rrs::PlayerId{1}, 3, visible, players, foods, std::nullopt, false);
    Expect(!same_quantized.has_value(), "sub-quantization movement suppresses snapshot");
}

void TestPlayerBallChangesAndRemoval()
{
    auto players = std::vector<rrs::PlayerEntity>{
        MakePlayer(rrs::PlayerId{1}, 0.0F),
        MakePlayer(rrs::PlayerId{2}, 100.0F, 3),
    };
    const auto foods = std::vector<rrs::FoodEntity>{};
    auto visible = rrs::VisibleEntitySet{
        .players = {
            rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{1}, .ball_mask = 1},
            rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{2}, .ball_mask = 3},
        },
        .food_ids = {},
    };
    auto tracker = rrs::SnapshotDeltaTracker{};
    const auto initial = tracker.BuildUpdate(rrs::PlayerId{1}, 1, visible, players, foods, std::nullopt, true);
    Expect(initial.has_value(), "initial player baseline is created");

    players[1].balls[1].position.x += 5.0F;
    const auto changed = tracker.BuildUpdate(rrs::PlayerId{1}, 2, visible, players, foods, std::nullopt, false);
    Expect(changed.has_value() && changed->player_updates.size() == 1, "one changed player is emitted");
    Expect(changed->player_updates[0].changed_ball_mask == 2, "only changed ball slot carries data");

    visible.players[1].ball_mask = 1;
    const auto ball_removed = tracker.BuildUpdate(rrs::PlayerId{1}, 3, visible, players, foods, std::nullopt, false);
    Expect(ball_removed.has_value() && ball_removed->player_updates.size() == 1, "visible mask removal emits player update");
    Expect(ball_removed->player_updates[0].visible_ball_mask == 1
               && ball_removed->player_updates[0].changed_ball_mask == 0,
           "removed ball needs no payload body");

    visible.players.pop_back();
    const auto player_removed = tracker.BuildUpdate(rrs::PlayerId{1}, 4, visible, players, foods, std::nullopt, false);
    Expect(player_removed.has_value() && player_removed->removed_player_ids == std::vector{rrs::PlayerId{2}},
           "player leaving AOI is emitted separately");
}

void TestFoodWinnerAndObserverReset()
{
    const auto players = std::vector<rrs::PlayerEntity>{MakePlayer(rrs::PlayerId{1}, 0.0F)};
    auto foods = std::vector<rrs::FoodEntity>{
        rrs::FoodEntity{.food_id = rrs::FoodId{1}, .position = rrs::Vector2{.x = 10.0F, .y = 20.0F}},
        rrs::FoodEntity{.food_id = rrs::FoodId{2}, .position = rrs::Vector2{.x = 30.0F, .y = 40.0F}},
    };
    auto visible = rrs::VisibleEntitySet{
        .players = {rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{1}, .ball_mask = 1}},
        .food_ids = {rrs::FoodId{1}, rrs::FoodId{2}},
    };
    auto tracker = rrs::SnapshotDeltaTracker{};
    const auto initial = tracker.BuildUpdate(rrs::PlayerId{1}, 1, visible, players, foods, std::nullopt, true);
    Expect(initial.has_value(), "initial food baseline is created");

    foods[0].position.x += 5.0F;
    visible.food_ids.pop_back();
    const auto delta = tracker.BuildUpdate(rrs::PlayerId{1}, 2, visible, players, foods, rrs::PlayerId{1}, false);
    Expect(delta.has_value() && delta->food_updates.size() == 1, "changed visible food is emitted");
    Expect(delta->removed_food_ids == std::vector{rrs::FoodId{2}}, "food leaving AOI is removed");
    Expect(delta->winner_player_id == rrs::PlayerId{1}, "winner transition is emitted once");

    const auto unchanged = tracker.BuildUpdate(rrs::PlayerId{1}, 3, visible, players, foods, rrs::PlayerId{1}, false);
    Expect(!unchanged.has_value(), "unchanged winner is not repeated");

    tracker.RemoveObserver(rrs::PlayerId{1});
    const auto reset = tracker.BuildUpdate(rrs::PlayerId{1}, 4, visible, players, foods, rrs::PlayerId{1}, false);
    Expect(reset.has_value() && reset->full_reset, "missing observer baseline forces full reset");
}

void TestDeadObserverEntry()
{
    const auto players = std::vector<rrs::PlayerEntity>{MakePlayer(rrs::PlayerId{1}, 0.0F, 0)};
    const auto foods = std::vector<rrs::FoodEntity>{};
    const auto visible = rrs::VisibleEntitySet{
        .players = {rrs::VisiblePlayerBalls{.player_id = rrs::PlayerId{1}, .ball_mask = 0}},
        .food_ids = {},
    };
    auto tracker = rrs::SnapshotDeltaTracker{};
    const auto full = tracker.BuildUpdate(rrs::PlayerId{1}, 1, visible, players, foods, std::nullopt, true);
    Expect(full.has_value() && full->player_updates.size() == 1, "dead observer remains an existing player");
    Expect(full->player_updates[0].visible_ball_mask == 0 && full->player_updates[0].changed_ball_mask == 0,
           "dead observer carries an empty ball mask");
}

} // namespace

int main()
{
    TestFullResetAndUnchangedDelta();
    TestPlayerBallChangesAndRemoval();
    TestFoodWinnerAndObserverReset();
    TestDeadObserverEntry();
    std::cout << "SnapshotDeltaTracker tests passed\n";
    return EXIT_SUCCESS;
}
