#include "rrs/game/Room.h"
#include "rrs/game/RoomRules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

namespace rrs {

Room::Room(RoomId room_id, Clock::time_point first_tick_time, std::chrono::nanoseconds tick_interval)
    : room_id_(room_id)
    , next_tick_time_(first_tick_time)
    , tick_interval_(tick_interval)
    , rng_(static_cast<std::mt19937::result_type>(room_id.value() * 2654435761ULL))
{
    const auto tick_interval_ns = tick_interval_.count();
    const auto respawn_delay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(room_rules::kRespawnDelay).count();
    const auto match_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(room_rules::kMatchDuration).count();

    respawn_delay_ticks_ = static_cast<TickSeq>(std::max<std::int64_t>(1, respawn_delay_ns / tick_interval_ns));
    match_duration_ticks_ = static_cast<TickSeq>(std::max<std::int64_t>(1, match_duration_ns / tick_interval_ns));
    InitializeFood();
}

void Room::EnqueueCommand(Command command)
{
    pending_commands_.push_back(std::move(command));
}

Room::TickResult Room::Tick()
{
    const auto tick_start = next_tick_time_;
    auto result = TickResult{};

    ++tick_seq_;
    ProcessCommands(TakeCommandsForTick(tick_start), result);

    if (!match_over_) {
        MovePlayers();
        ResolveFoodCollisions();
        ResolvePlayerCollisions();
        RespawnDuePlayers();
        UpdateMatchState();
    }

    result.broadcast_snapshot = BuildDeltaSnapshot();
    if (std::any_of(result.events.begin(), result.events.end(), [](const Event& event) {
            return event.type == EventType::kJoinAccepted || event.type == EventType::kReconnectAccepted;
        })) {
        result.full_snapshot = BuildFullSnapshot();
    }

    next_tick_time_ += tick_interval_;
    return result;
}

void Room::InitializeFood()
{
    foods_.Reset(room_rules::kFoodCount);

    std::uniform_real_distribution<float> distribution{
        -room_rules::kRoomHalfExtent + room_rules::kFoodRadius,
        room_rules::kRoomHalfExtent - room_rules::kFoodRadius,
    };

    for (std::size_t food_index = 0; food_index < room_rules::kFoodCount; ++food_index) {
        foods_.Add(FoodEntity{
            .food_id = FoodId{food_index + 1},
            .position = Vector2{
                .x = distribution(rng_),
                .y = distribution(rng_),
            },
        });
    }
}

std::vector<Room::Command> Room::TakeCommandsForTick(Clock::time_point tick_start)
{
    auto ready_commands = std::vector<Command>{};
    auto deferred_commands = std::vector<Command>{};
    ready_commands.reserve(pending_commands_.size());
    deferred_commands.reserve(pending_commands_.size());

    for (auto& command : pending_commands_) {
        if (command.entered_at < tick_start) {
            ready_commands.push_back(std::move(command));
        } else {
            deferred_commands.push_back(std::move(command));
        }
    }

    pending_commands_ = std::move(deferred_commands);
    return ready_commands;
}

void Room::ProcessCommands(const std::vector<Command>& commands, TickResult& result)
{
    for (const auto& command : commands) {
        switch (command.type) {
        case CommandType::kJoin:
            JoinPlayer(command.session, result);
            break;
        case CommandType::kReconnect:
            ReconnectPlayer(command.session, result);
            break;
        case CommandType::kPlayerInput:
            ApplyPlayerInput(command.session, command.input);
            break;
        case CommandType::kLeave:
            LeavePlayer(command.session, result);
            break;
        }
    }
}

void Room::JoinPlayer(const Session& session, TickResult& result)
{
    if (FindPlayer(session.player_id) == nullptr) {
        players_.push_back(PlayerEntity{
            .player_id = session.player_id,
            .position = FindRespawnPosition(),
            .input_direction = {},
            .radius = room_rules::kInitialPlayerRadius,
            .respawn_tick = 0,
        });
    }

    result.events.push_back(Event{
        .type = EventType::kJoinAccepted,
        .session = session,
    });
}

void Room::ReconnectPlayer(const Session& session, TickResult& result)
{
    result.events.push_back(Event{
        .type = FindPlayer(session.player_id) != nullptr ? EventType::kReconnectAccepted : EventType::kReconnectRejected,
        .session = session,
    });
}

void Room::ApplyPlayerInput(const Session& session, const PlayerInput& input)
{
    auto* player = FindPlayer(session.player_id);
    if (player == nullptr || !IsAlive(*player)) {
        return;
    }

    player->input_direction = NormalizeOrZero(Vector2{
        .x = static_cast<float>(input.move_x),
        .y = static_cast<float>(input.move_y),
    });
}

void Room::LeavePlayer(const Session& session, TickResult& result)
{
    std::erase_if(pending_commands_, [session_id = session.session_id](const Command& command) {
        return command.session.session_id == session_id;
    });
    std::erase_if(players_, [player_id = session.player_id](const PlayerEntity& player) {
        return player.player_id == player_id;
    });

    result.events.push_back(Event{
        .type = EventType::kPlayerLeft,
        .session = session,
    });
}

void Room::ResolveFoodCollisions()
{
    std::uniform_real_distribution<float> food_position_distribution{
        -room_rules::kRoomHalfExtent + room_rules::kFoodRadius,
        room_rules::kRoomHalfExtent - room_rules::kFoodRadius,
    };

    for (auto& player : players_) {
        if (!IsAlive(player)) {
            continue;
        }

        const auto query_radius = player.radius + room_rules::kFoodRadius;
        for (const auto food : foods_.Query(player.position, query_radius)) {
            const auto eat_distance = player.radius + room_rules::kFoodRadius;
            if (DistanceSquared(player.position, food.position) > eat_distance * eat_distance) {
                continue;
            }

            player.radius = std::sqrt(
                player.radius * player.radius + room_rules::kFoodRadius * room_rules::kFoodRadius * room_rules::kFoodGrowthRatio);

            foods_.MoveTo(food.food_id, Vector2{
                                           .x = food_position_distribution(rng_),
                                           .y = food_position_distribution(rng_),
                                       });
        }
    }
}

void Room::MovePlayers()
{
    const auto delta_seconds = static_cast<float>(tick_interval_.count()) / 1'000'000'000.0F;
    for (auto& player : players_) {
        if (!IsAlive(player)) {
            continue;
        }

        const auto speed = CalcSpeed(player.radius);
        player.position.x += player.input_direction.x * speed * delta_seconds;
        player.position.y += player.input_direction.y * speed * delta_seconds;
        player.position.x = std::clamp(
            player.position.x,
            -room_rules::kRoomHalfExtent + player.radius,
            room_rules::kRoomHalfExtent - player.radius);
        player.position.y = std::clamp(
            player.position.y,
            -room_rules::kRoomHalfExtent + player.radius,
            room_rules::kRoomHalfExtent - player.radius);
    }
}

void Room::ResolvePlayerCollisions()
{
    for (std::size_t left_index = 0; left_index < players_.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1; right_index < players_.size(); ++right_index) {
            auto& left = players_[left_index];
            auto& right = players_[right_index];
            if (!IsAlive(left) || !IsAlive(right)) {
                continue;
            }

            if (left.radius >= right.radius) {
                TryEatPlayer(left, right);
            } else {
                TryEatPlayer(right, left);
            }
        }
    }
}

void Room::TryEatPlayer(PlayerEntity& attacker, PlayerEntity& victim)
{
    if (!IsAlive(attacker) || !IsAlive(victim)) {
        return;
    }

    const auto radius_ok = attacker.radius >= victim.radius * room_rules::kEatRadiusRatio;
    const auto center_distance = attacker.radius * room_rules::kEatCenterRatio;
    const auto center_ok = DistanceSquared(attacker.position, victim.position) <= center_distance * center_distance;
    if (!radius_ok || !center_ok) {
        return;
    }

    attacker.radius = std::sqrt(attacker.radius * attacker.radius + victim.radius * victim.radius * room_rules::kPlayerGrowthRatio);
    victim.radius = room_rules::kInitialPlayerRadius;
    victim.input_direction = {};
    victim.respawn_tick = tick_seq_ + respawn_delay_ticks_;
}

void Room::RespawnDuePlayers()
{
    for (auto& player : players_) {
        if (player.respawn_tick == 0 || player.respawn_tick > tick_seq_) {
            continue;
        }

        player.position = FindRespawnPosition();
        player.input_direction = {};
        player.radius = room_rules::kInitialPlayerRadius;
        player.respawn_tick = 0;
    }
}

void Room::UpdateMatchState()
{
    if (match_over_ || tick_seq_ < match_duration_ticks_) {
        return;
    }

    match_over_ = true;
    float best_radius = -std::numeric_limits<float>::max();
    for (const auto& player : players_) {
        if (player.radius <= best_radius) {
            continue;
        }

        best_radius = player.radius;
        winner_player_id_ = player.player_id;
    }
}

RoomSnapshot Room::BuildDeltaSnapshot()
{
    RoomSnapshot snapshot{
        .tick_seq = tick_seq_,
        .players = {},
        .foods = {},
        .winner_player_id = match_over_ ? winner_player_id_ : PlayerId{},
    };
    snapshot.players.reserve(players_.size());
    snapshot.foods.reserve(foods_.dirty_count());

    for (const auto& player : players_) {
        snapshot.players.push_back(PlayerStateSnapshot{
            .player_id = player.player_id,
            .position = player.position,
            .radius = player.radius,
            .alive = IsAlive(player),
        });
    }

    foods_.AppendDeltaSnapshot(snapshot.foods);
    return snapshot;
}

RoomSnapshot Room::BuildFullSnapshot() const
{
    RoomSnapshot snapshot{
        .tick_seq = tick_seq_,
        .players = {},
        .foods = {},
        .winner_player_id = match_over_ ? winner_player_id_ : PlayerId{},
    };
    snapshot.players.reserve(players_.size());
    snapshot.foods.reserve(foods_.food_count());

    for (const auto& player : players_) {
        snapshot.players.push_back(PlayerStateSnapshot{
            .player_id = player.player_id,
            .position = player.position,
            .radius = player.radius,
            .alive = IsAlive(player),
        });
    }

    foods_.AppendFullSnapshot(snapshot.foods);
    return snapshot;
}

float Room::CalcSpeed(float radius) const
{
    const auto ratio = room_rules::kSpeedReferenceRadius / radius;
    const auto speed = room_rules::kBaseSpeed * std::pow(ratio, room_rules::kSpeedRadiusExponent);
    return std::clamp(speed, room_rules::kMinSpeed, room_rules::kMaxSpeed);
}

Vector2 Room::FindRespawnPosition()
{
    std::uniform_real_distribution<float> distribution{
        -room_rules::kRoomHalfExtent + room_rules::kInitialPlayerRadius,
        room_rules::kRoomHalfExtent - room_rules::kInitialPlayerRadius,
    };

    auto best_position = Vector2{
        .x = distribution(rng_),
        .y = distribution(rng_),
    };
    float best_distance_squared = -1.0F;

    for (std::size_t attempt = 0; attempt < room_rules::kRespawnSearchAttempts; ++attempt) {
        const auto candidate = Vector2{
            .x = distribution(rng_),
            .y = distribution(rng_),
        };
        auto candidate_distance_squared = std::numeric_limits<float>::max();
        bool inside_alive_player = false;

        for (const auto& player : players_) {
            if (!IsAlive(player)) {
                continue;
            }

            const auto distance_squared = DistanceSquared(candidate, player.position);
            candidate_distance_squared = std::min(candidate_distance_squared, distance_squared);
            if (distance_squared <= player.radius * player.radius) {
                inside_alive_player = true;
                break;
            }
        }

        if (!inside_alive_player) {
            return candidate;
        }

        if (candidate_distance_squared > best_distance_squared) {
            best_distance_squared = candidate_distance_squared;
            best_position = candidate;
        }
    }

    return best_position;
}

PlayerEntity* Room::FindPlayer(PlayerId player_id)
{
    const auto iterator = std::find_if(players_.begin(), players_.end(), [player_id](const PlayerEntity& player) {
        return player.player_id == player_id;
    });

    return iterator != players_.end() ? &(*iterator) : nullptr;
}

const PlayerEntity* Room::FindPlayer(PlayerId player_id) const
{
    const auto iterator = std::find_if(players_.begin(), players_.end(), [player_id](const PlayerEntity& player) {
        return player.player_id == player_id;
    });

    return iterator != players_.end() ? &(*iterator) : nullptr;
}

} // namespace rrs
