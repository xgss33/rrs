#include "rrs/simulation/Room.h"

#include "rrs/core/Identifiers.h"
#include "rrs/runtime/Session.h"
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

rrs::Session MakeSession()
{
    return rrs::Session{
        .session_id = rrs::SessionId{1},
        .generation = 1,
        .player_id = rrs::PlayerId{1},
        .io_thread_id = rrs::IoThreadId{1},
        .worker_id = rrs::WorkerId{1},
    };
}

void TestFullFoodBaselineUsesZeroBasedIndices()
{
    constexpr auto tick_interval = std::chrono::nanoseconds{33'333'333};
    const auto first_tick_time = rrs::Room::Clock::now() + tick_interval;
    auto room = rrs::Room{rrs::RoomId{1}, first_tick_time, tick_interval};
    const auto session = MakeSession();
    room.EnqueueCommand(rrs::Room::Command{
        .type = rrs::Room::CommandType::kJoin,
        .session = session,
        .input = {},
        .entered_at = first_tick_time - std::chrono::nanoseconds{1},
    });

    const auto result = room.Tick();
    Expect(result.snapshot_updates.size() == 1 && result.snapshot_updates[0].update.full_reset,
           "joining observer receives a full snapshot");
    Expect(result.food_baseline.size() == rrs::room_rules::kFoodCount,
           "full snapshot carries every room food");
    for (std::size_t food_index = 0; food_index < result.food_baseline.size(); ++food_index) {
        Expect(result.food_baseline[food_index].food_index == food_index,
               "food baseline index matches authoritative storage index");
    }

    room.EnqueueCommand(rrs::Room::Command{
        .type = rrs::Room::CommandType::kPlayerInput,
        .session = session,
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
        Expect(tick_result.snapshot_updates.size() == 1,
               "room food changes are delivered to the observer");
        for (const auto& food : tick_result.food_updates) {
            Expect(food.food_index < rrs::room_rules::kFoodCount,
                   "food update keeps the authoritative zero-based index");
        }
    }
    Expect(observed_food_update, "deterministic room movement eventually produces a food update");
}

} // namespace

int main()
{
    TestFullFoodBaselineUsesZeroBasedIndices();
    std::cout << "Room food synchronization tests passed\n";
    return EXIT_SUCCESS;
}
