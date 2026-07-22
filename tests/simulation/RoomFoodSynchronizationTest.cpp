#include "rrs/simulation/Room.h"

#include "rrs/core/Identifiers.h"
#include "rrs/simulation/PlayerInput.h"
#include "rrs/simulation/RoomRules.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestFullFoodBaselineUsesZeroBasedIndices()
{
    constexpr auto tick_interval = std::chrono::nanoseconds{33'333'333};
    const auto first_tick_time = rrs::Room::Clock::now() + tick_interval;
    auto room = rrs::Room{rrs::RoomId{1}, first_tick_time, tick_interval};
    const auto session_id = rrs::SessionId{1};
    const auto player_id = rrs::PlayerId{1};
    room.EnqueueCommand(rrs::Room::Command{
        .type = rrs::Room::CommandType::kJoin,
        .session_id = session_id,
        .player_id = player_id,
        .input = {},
        .entered_at = first_tick_time - std::chrono::nanoseconds{1},
    });

    const auto result = room.Tick();
    const auto snapshot = room.BuildSnapshot(player_id, true, false);
    Expect(snapshot && snapshot->full_reset,
           "joining observer receives a full snapshot");
    Expect(room.visibility_observer_count() == 1, "connected player is a snapshot observer");
    room.RemoveSnapshotObserver(player_id);
    Expect(room.player_count() == 1, "removing snapshot observer keeps the simulated player");
    Expect(room.visibility_observer_count() == 0, "disconnected player is not a snapshot observer");
    Expect(room.BuildSnapshot(player_id, true, false).has_value(), "reconnect recreates a full snapshot observer");
    const auto food_baseline = room.BuildFoodSnapshotBaseline();
    Expect(food_baseline.size() == rrs::room_rules::kFoodCount,
           "full snapshot carries every room food");
    for (std::size_t food_index = 0; food_index < food_baseline.size(); ++food_index) {
        Expect(food_baseline[food_index].food_index == food_index,
               "food baseline index matches authoritative storage index");
    }

    room.EnqueueCommand(rrs::Room::Command{
        .type = rrs::Room::CommandType::kPlayerInput,
        .session_id = session_id,
        .player_id = player_id,
        .input = rrs::PlayerInput{.move_x = 32767, .move_y = 0, .input_flags = 0},
        .entered_at = room.next_tick_time() - std::chrono::nanoseconds{1},
    });

    auto observed_food_update = false;
    for (std::size_t tick = 0; tick < 1'000 && !observed_food_update; ++tick) {
        const auto tick_result = room.Tick();
        if (tick_result.food_updates.empty()) {
            continue;
        }

        observed_food_update = true;
        Expect(room.BuildSnapshot(player_id, false, true).has_value(),
               "room food changes are delivered to the observer");
        for (const auto& food : tick_result.food_updates) {
            Expect(food.food_index < rrs::room_rules::kFoodCount,
                   "food update keeps the authoritative zero-based index");
        }
    }
    Expect(observed_food_update, "deterministic room movement eventually produces a food update");
}

void TestMatchEndProducesFinalSnapshot()
{
    constexpr auto tick_interval = rrs::room_rules::kMatchDuration;
    const auto first_tick_time = rrs::Room::Clock::now() + tick_interval;
    auto room = rrs::Room{rrs::RoomId{2}, first_tick_time, tick_interval};
    const auto player_id = rrs::PlayerId{2};
    room.EnqueueCommand(rrs::Room::Command{
        .type = rrs::Room::CommandType::kJoin,
        .session_id = rrs::SessionId{2},
        .player_id = player_id,
        .input = {},
        .entered_at = first_tick_time - std::chrono::nanoseconds{1},
    });

    const auto result = room.Tick();
    Expect(result.match_ended, "room reports the match-ending tick once");
    const auto final_snapshot = room.BuildSnapshot(player_id, true, false);
    Expect(final_snapshot && final_snapshot->winner_player_id == player_id,
           "match-ending tick exposes the winner in the final snapshot");
}

} // namespace

int main()
{
    TestFullFoodBaselineUsesZeroBasedIndices();
    TestMatchEndProducesFinalSnapshot();
    std::cout << "Room food synchronization tests passed\n";
    return EXIT_SUCCESS;
}
