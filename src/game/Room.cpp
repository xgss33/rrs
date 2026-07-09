#include "rrs/game/Room.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace rrs {

namespace {

constexpr float kRoomHalfExtent = 1000.0F;
constexpr float kInitialPlayerRadius = 12.0F;
constexpr float kFoodRadius = 4.0F;
constexpr std::size_t kFoodCount = 1024;

constexpr float kBaseSpeed = 180.0F;
constexpr float kMinSpeed = 60.0F;
constexpr float kMaxSpeed = 220.0F;
constexpr float kSpeedReferenceRadius = 12.0F;
constexpr float kSpeedRadiusExponent = 0.4F;

constexpr float kFoodGrowthRatio = 0.55F;
constexpr float kEatRadiusRatio = 1.15F;
constexpr float kEatCenterRatio = 0.75F;
constexpr float kPlayerGrowthRatio = 0.75F;

constexpr std::chrono::seconds kRespawnDelay{5};
constexpr std::chrono::seconds kMatchDuration{600};
constexpr std::size_t kRespawnSearchAttempts = 16;

} // namespace

Room::Room(RoomId room_id, Clock::time_point first_tick_time, std::chrono::nanoseconds tick_interval)
    : room_id_(room_id)
    , next_tick_time_(first_tick_time)
    , tick_interval_(tick_interval)
    , rng_(static_cast<std::mt19937::result_type>(room_id.value() * 2654435761ULL))
{
    respawn_delay_ticks_ = static_cast<TickSeq>(
        std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(kRespawnDelay).count() / tick_interval_.count()));
    match_duration_ticks_ = static_cast<TickSeq>(
        std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(kMatchDuration).count() / tick_interval_.count()));
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

    result.snapshot = BuildSnapshot();
    next_tick_time_ += tick_interval_;
    return result;
}

void Room::InitializeFood()
{
    foods_.reserve(kFoodCount);
    for (std::size_t food_index = 0; food_index < kFoodCount; ++food_index) {
        foods_.push_back(FoodEntity{
            .food_id = FoodId{food_index + 1},
            .position = RandomPosition(kFoodRadius),
            .radius = kFoodRadius,
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
            .radius = kInitialPlayerRadius,
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

void Room::RespawnDuePlayers()
{
    for (auto& player : players_) {
        if (player.respawn_tick == 0 || player.respawn_tick > tick_seq_) {
            continue;
        }

        player.position = FindRespawnPosition();
        player.input_direction = {};
        player.radius = kInitialPlayerRadius;
        player.respawn_tick = 0;
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
        player.position.x = std::clamp(player.position.x, -kRoomHalfExtent + player.radius, kRoomHalfExtent - player.radius);
        player.position.y = std::clamp(player.position.y, -kRoomHalfExtent + player.radius, kRoomHalfExtent - player.radius);
    }
}

void Room::ResolveFoodCollisions()
{
    for (auto& player : players_) {
        if (!IsAlive(player)) {
            continue;
        }

        for (auto& food : foods_) {
            const auto eat_distance = player.radius + food.radius;
            if (DistanceSquared(player.position, food.position) > eat_distance * eat_distance) {
                continue;
            }

            player.radius = std::sqrt(player.radius * player.radius + food.radius * food.radius * kFoodGrowthRatio);
            food.position = RandomPosition(kFoodRadius);
        }
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

    const auto radius_ok = attacker.radius >= victim.radius * kEatRadiusRatio;
    const auto center_distance = attacker.radius * kEatCenterRatio;
    const auto center_ok = DistanceSquared(attacker.position, victim.position) <= center_distance * center_distance;
    if (!radius_ok || !center_ok) {
        return;
    }

    attacker.radius = std::sqrt(attacker.radius * attacker.radius + victim.radius * victim.radius * kPlayerGrowthRatio);
    victim.radius = kInitialPlayerRadius;
    victim.input_direction = {};
    victim.respawn_tick = tick_seq_ + respawn_delay_ticks_;
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

float Room::CalcSpeed(float radius) const
{
    const auto ratio = kSpeedReferenceRadius / radius;
    const auto speed = kBaseSpeed * std::pow(ratio, kSpeedRadiusExponent);
    return std::clamp(speed, kMinSpeed, kMaxSpeed);
}

Vector2 Room::RandomPosition(float radius)
{
    std::uniform_real_distribution<float> distribution{-kRoomHalfExtent + radius, kRoomHalfExtent - radius};
    return Vector2{
        .x = distribution(rng_),
        .y = distribution(rng_),
    };
}

Vector2 Room::FindRespawnPosition()
{
    auto best_position = RandomPosition(kInitialPlayerRadius);
    float best_distance_squared = -1.0F;

    for (std::size_t attempt = 0; attempt < kRespawnSearchAttempts; ++attempt) {
        const auto candidate = RandomPosition(kInitialPlayerRadius);
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

RoomSnapshot Room::BuildSnapshot() const
{
    return RoomSnapshot::FromRoomState(room_id_, tick_seq_, players_, foods_, match_over_, winner_player_id_);
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
