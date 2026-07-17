#include "rrs/simulation/Room.h"

#include "rrs/simulation/RoomRules.h"
#include "rrs/spatial/UniformGrid.h"

#include <algorithm>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

namespace rrs {

namespace {

[[nodiscard]] constexpr std::uint16_t BallMask(std::size_t ball_index) noexcept
{
    return static_cast<std::uint16_t>(1U << ball_index);
}

[[nodiscard]] bool IsBallActive(const PlayerEntity& player, std::size_t ball_index) noexcept
{
    return (player.active_ball_mask & BallMask(ball_index)) != 0;
}

void ClampBallPosition(PlayerBall& ball)
{
    ball.position.x = std::clamp(
        ball.position.x,
        -room_rules::kRoomHalfExtent + ball.radius,
        room_rules::kRoomHalfExtent - ball.radius);
    ball.position.y = std::clamp(
        ball.position.y,
        -room_rules::kRoomHalfExtent + ball.radius,
        room_rules::kRoomHalfExtent - ball.radius);
}

UniformGridLayout MakeRoomSpatialGridLayout()
{
    return UniformGridLayout{
        Aabb{
            .min = Vector2{
                .x = -room_rules::kRoomHalfExtent,
                .y = -room_rules::kRoomHalfExtent,
            },
            .max = Vector2{
                .x = room_rules::kRoomHalfExtent,
                .y = room_rules::kRoomHalfExtent,
            },
        },
        room_rules::kSpatialGridCellSize,
    };
}

} // namespace

Room::Room(RoomId room_id, Clock::time_point first_tick_time, std::chrono::nanoseconds tick_interval)
    : room_id_(room_id)
    , next_tick_time_(first_tick_time)
    , tick_interval_(tick_interval)
    , rng_(static_cast<std::mt19937::result_type>(room_id.value() * 2654435761ULL))
    , food_spatial_index_(MakeRoomSpatialGridLayout())
    , player_ball_spatial_index_(MakeRoomSpatialGridLayout())
{
    const auto tick_interval_ns = tick_interval_.count();
    const auto respawn_delay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(room_rules::kRespawnDelay).count();
    const auto match_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(room_rules::kMatchDuration).count();

    respawn_delay_ticks_ = static_cast<TickSeq>(std::max<std::int64_t>(1, respawn_delay_ns / tick_interval_ns));
    match_duration_ticks_ = static_cast<TickSeq>(std::max<std::int64_t>(1, match_duration_ns / tick_interval_ns));
    InitializeFoods();
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
    const auto player_inputs = ProcessCommands(TakeCommandsForTick(tick_start), result);
    ApplyPlayerInputs(player_inputs);

    if (!match_over_) {
        SplitPlayers(player_inputs);
        MovePlayers();
        ResolveFoodEating();
        ResolvePlayerBallEating();
        RespawnDuePlayers();
        UpdateMatchState();
    }

    result.broadcast_snapshot = BuildBroadcastSnapshot();
    if (std::any_of(result.events.begin(), result.events.end(), [](const Event& event) {
            return event.type == EventType::kJoinAccepted || event.type == EventType::kReconnectAccepted;
        })) {
        result.full_snapshot = BuildFullSnapshot();
    }

    next_tick_time_ += tick_interval_;
    return result;
}

void Room::InitializeFoods()
{
    foods_.clear();
    foods_.reserve(room_rules::kFoodCount);
    consumed_food_indices_.reserve(room_rules::kFoodCount);

    std::uniform_real_distribution<float> distribution{
        -room_rules::kRoomHalfExtent + room_rules::kFoodRadius,
        room_rules::kRoomHalfExtent - room_rules::kFoodRadius,
    };

    for (std::size_t food_index = 0; food_index < room_rules::kFoodCount; ++food_index) {
        foods_.push_back(FoodEntity{
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

std::vector<Room::AggregatedPlayerInput> Room::ProcessCommands(const std::vector<Command>& commands, TickResult& result)
{
    auto player_inputs = std::vector<AggregatedPlayerInput>{};
    player_inputs.reserve(commands.size());

    for (const auto& command : commands) {
        switch (command.type) {
        case CommandType::kJoin:
            JoinPlayer(command.session, result);
            break;
        case CommandType::kReconnect:
            ReconnectPlayer(command.session, result);
            break;
        case CommandType::kPlayerInput: {
            const auto iterator = std::find_if(
                player_inputs.begin(),
                player_inputs.end(),
                [player_id = command.session.player_id](const AggregatedPlayerInput& input) {
                    return input.player_id == player_id;
                });
            if (iterator == player_inputs.end()) {
                player_inputs.push_back(AggregatedPlayerInput{
                    .player_id = command.session.player_id,
                    .input = command.input,
                });
            } else {
                iterator->input.move_x = command.input.move_x;
                iterator->input.move_y = command.input.move_y;
                iterator->input.input_flags |= command.input.input_flags;
            }
            break;
        }
        case CommandType::kLeave:
            LeavePlayer(command.session, result);
            break;
        }
    }

    return player_inputs;
}

void Room::JoinPlayer(const Session& session, TickResult& result)
{
    if (FindPlayer(session.player_id) == nullptr) {
        auto player = PlayerEntity{
            .player_id = session.player_id,
            .input_direction = {},
            .respawn_tick = 0,
            .active_ball_mask = BallMask(0),
            .balls = {},
        };
        player.balls[0] = PlayerBall{
            .position = FindSpawnPosition(),
            .radius = room_rules::kInitialPlayerRadius,
        };
        players_.push_back(player);
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

void Room::ApplyPlayerInputs(const std::vector<AggregatedPlayerInput>& inputs)
{
    for (const auto& input : inputs) {
        auto* player = FindPlayer(input.player_id);
        if (player == nullptr || !IsAlive(*player)) {
            continue;
        }

        player->input_direction = NormalizeOrZero(Vector2{
            .x = static_cast<float>(input.input.move_x),
            .y = static_cast<float>(input.input.move_y),
        });
    }
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

void Room::SplitPlayers(const std::vector<AggregatedPlayerInput>& inputs)
{
    for (const auto& input : inputs) {
        if ((input.input.input_flags & PlayerInput::kSplitFlag) == 0) {
            continue;
        }

        auto* player = FindPlayer(input.player_id);
        if (player != nullptr && IsAlive(*player)) {
            SplitPlayer(*player);
        }
    }
}

void Room::SplitPlayer(PlayerEntity& player)
{
    const auto source_mask = player.active_ball_mask;
    auto free_mask = static_cast<std::uint16_t>(~player.active_ball_mask);
    auto split_direction = NormalizeOrZero(player.input_direction);
    if (LengthSquared(split_direction) <= 0.0001F) {
        split_direction = Vector2{.x = 1.0F, .y = 0.0F};
    }

    for (std::size_t parent_index = 0; parent_index < kMaxBallsPerPlayer; ++parent_index) {
        if ((source_mask & BallMask(parent_index)) == 0) {
            continue;
        }
        if (free_mask == 0) {
            break;
        }

        auto& parent_ball = player.balls[parent_index];
        const auto child_radius = parent_ball.radius / std::sqrt(2.0F);
        if (child_radius < room_rules::kInitialPlayerRadius) {
            continue;
        }

        const auto child_index = static_cast<std::size_t>(std::countr_zero(free_mask));
        const auto child_mask = BallMask(child_index);
        parent_ball.radius = child_radius;

        auto& child_ball = player.balls[child_index];
        child_ball = PlayerBall{
            .position = Vector2{
                .x = parent_ball.position.x + split_direction.x * child_radius * room_rules::kSplitSpawnDistanceRatio,
                .y = parent_ball.position.y + split_direction.y * child_radius * room_rules::kSplitSpawnDistanceRatio,
            },
            .radius = child_radius,
        };
        ClampBallPosition(child_ball);

        free_mask = static_cast<std::uint16_t>(free_mask & static_cast<std::uint16_t>(~child_mask));
        player.active_ball_mask = static_cast<std::uint16_t>(player.active_ball_mask | child_mask);
    }
}

void Room::ResolveFoodEating()
{
    food_spatial_index_.Rebuild(foods_);

    auto consumed_food_flags = std::bitset<room_rules::kFoodCount>{};
    std::uniform_real_distribution<float> food_position_distribution{
        -room_rules::kRoomHalfExtent + room_rules::kFoodRadius,
        room_rules::kRoomHalfExtent - room_rules::kFoodRadius,
    };

    for (auto& player : players_) {
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            if (!IsBallActive(player, ball_index)) {
                continue;
            }

            auto& ball = player.balls[ball_index];
            const auto eat_distance = ball.radius + room_rules::kFoodRadius;
            for (const auto food_index : food_spatial_index_.QueryCandidates(ball.position, ball.radius)) {
                if (consumed_food_flags.test(food_index)) {
                    continue;
                }

                const auto& food = foods_[food_index];
                if (DistanceSquared(ball.position, food.position) > eat_distance * eat_distance) {
                    continue;
                }

                ball.radius = std::sqrt(
                    ball.radius * ball.radius + room_rules::kFoodRadius * room_rules::kFoodRadius * room_rules::kFoodGrowthRatio);

                consumed_food_flags.set(food_index);
                consumed_food_indices_.push_back(food_index);
            }
        }
    }

    for (const auto food_index : consumed_food_indices_) {
        foods_[food_index].position = Vector2{
            .x = food_position_distribution(rng_),
            .y = food_position_distribution(rng_),
        };
    }
}

void Room::MovePlayers()
{
    const auto delta_seconds = static_cast<float>(tick_interval_.count()) / 1'000'000'000.0F;
    for (auto& player : players_) {
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            if (!IsBallActive(player, ball_index)) {
                continue;
            }

            auto& ball = player.balls[ball_index];
            const auto speed = CalculateBallSpeed(ball.radius);
            ball.position.x += player.input_direction.x * speed * delta_seconds;
            ball.position.y += player.input_direction.y * speed * delta_seconds;
            ClampBallPosition(ball);
        }
    }
}

void Room::ResolvePlayerBallEating()
{
    player_ball_spatial_index_.Rebuild(players_);

    for (std::size_t left_player_index = 0; left_player_index < players_.size(); ++left_player_index) {
        for (std::size_t left_ball_index = 0; left_ball_index < kMaxBallsPerPlayer; ++left_ball_index) {
            if (!IsBallActive(players_[left_player_index], left_ball_index)) {
                continue;
            }

            auto next_candidate_order = left_player_index * kMaxBallsPerPlayer + left_ball_index + 1;
            while (IsBallActive(players_[left_player_index], left_ball_index)) {
                const auto query_ball = players_[left_player_index].balls[left_ball_index];
                auto ball_eaten = false;
                {
                    const auto candidates = player_ball_spatial_index_.QueryCandidates(query_ball.position, query_ball.radius);
                    for (const auto candidate : candidates) {
                        const auto right_player_index = static_cast<std::size_t>(candidate.player_index);
                        const auto right_ball_index = static_cast<std::size_t>(candidate.ball_index);
                        const auto candidate_order = right_player_index * kMaxBallsPerPlayer + right_ball_index;
                        if (candidate_order < next_candidate_order) {
                            continue;
                        }
                        next_candidate_order = candidate_order + 1;

                        auto& left_player = players_[left_player_index];
                        auto& right_player = players_[right_player_index];
                        if (!IsBallActive(left_player, left_ball_index)
                            || !IsBallActive(right_player, right_ball_index)) {
                            continue;
                        }

                        const auto left_radius = left_player.balls[left_ball_index].radius;
                        const auto right_radius = right_player.balls[right_ball_index].radius;
                        if (left_radius >= right_radius) {
                            ball_eaten = TryEatPlayerBall(left_player, left_ball_index, right_player, right_ball_index);
                        } else {
                            ball_eaten = TryEatPlayerBall(right_player, right_ball_index, left_player, left_ball_index);
                        }

                        if (ball_eaten) {
                            break;
                        }
                    }
                }

                if (!ball_eaten) {
                    break;
                }
                player_ball_spatial_index_.Rebuild(players_);
            }
        }
    }
}

bool Room::TryEatPlayerBall(PlayerEntity& attacker,
                            std::size_t attacker_ball_index,
                            PlayerEntity& victim,
                            std::size_t victim_ball_index)
{
    auto& attacker_ball = attacker.balls[attacker_ball_index];
    const auto& victim_ball = victim.balls[victim_ball_index];
    const auto radius_ok = attacker_ball.radius >= victim_ball.radius * room_rules::kEatRadiusRatio;
    const auto center_distance = attacker_ball.radius * room_rules::kEatCenterRatio;
    const auto center_ok = DistanceSquared(attacker_ball.position, victim_ball.position) <= center_distance * center_distance;
    if (!radius_ok || !center_ok) {
        return false;
    }

    attacker_ball.radius = std::sqrt(
        attacker_ball.radius * attacker_ball.radius
        + victim_ball.radius * victim_ball.radius * room_rules::kPlayerGrowthRatio);
    victim.active_ball_mask = static_cast<std::uint16_t>(
        victim.active_ball_mask & static_cast<std::uint16_t>(~BallMask(victim_ball_index)));
    if (!IsAlive(victim)) {
        victim.input_direction = {};
        victim.respawn_tick = tick_seq_ + respawn_delay_ticks_;
    }
    return true;
}

void Room::RespawnDuePlayers()
{
    for (auto& player : players_) {
        if (IsAlive(player) || player.respawn_tick == 0 || player.respawn_tick > tick_seq_) {
            continue;
        }

        player.input_direction = {};
        player.balls[0] = PlayerBall{
            .position = FindSpawnPosition(),
            .radius = room_rules::kInitialPlayerRadius,
        };
        player.active_ball_mask = BallMask(0);
        player.respawn_tick = 0;
    }
}

void Room::UpdateMatchState()
{
    if (match_over_ || tick_seq_ < match_duration_ticks_) {
        return;
    }

    match_over_ = true;
    float best_area = -std::numeric_limits<float>::max();
    for (const auto& player : players_) {
        auto area = 0.0F;
        for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
            if (!IsBallActive(player, ball_index)) {
                continue;
            }

            const auto radius = player.balls[ball_index].radius;
            area += radius * radius;
        }

        if (area <= best_area) {
            continue;
        }

        best_area = area;
        winner_player_id_ = player.player_id;
    }
}

RoomSnapshot Room::BuildBroadcastSnapshot()
{
    auto snapshot = BuildSnapshotWithPlayers();
    snapshot.foods.reserve(consumed_food_indices_.size());
    for (const auto food_index : consumed_food_indices_) {
        snapshot.foods.push_back(foods_[food_index]);
    }
    consumed_food_indices_.clear();
    return snapshot;
}

RoomSnapshot Room::BuildFullSnapshot() const
{
    auto snapshot = BuildSnapshotWithPlayers();
    snapshot.foods = foods_;
    return snapshot;
}

RoomSnapshot Room::BuildSnapshotWithPlayers() const
{
    RoomSnapshot snapshot{
        .tick_seq = tick_seq_,
        .players = {},
        .foods = {},
        .winner_player_id = match_over_ ? winner_player_id_ : PlayerId{},
    };
    snapshot.players.reserve(players_.size());

    for (const auto& player : players_) {
        snapshot.players.push_back(PlayerStateSnapshot{
            .player_id = player.player_id,
            .active_ball_mask = player.active_ball_mask,
            .balls = player.balls,
        });
    }
    return snapshot;
}

float Room::CalculateBallSpeed(float radius) const
{
    const auto ratio = room_rules::kSpeedReferenceRadius / radius;
    const auto speed = room_rules::kBaseSpeed * std::pow(ratio, room_rules::kSpeedRadiusExponent);
    return std::clamp(speed, room_rules::kMinSpeed, room_rules::kMaxSpeed);
}

Vector2 Room::FindSpawnPosition()
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
            for (std::size_t ball_index = 0; ball_index < kMaxBallsPerPlayer; ++ball_index) {
                if (!IsBallActive(player, ball_index)) {
                    continue;
                }

                const auto& ball = player.balls[ball_index];
                const auto distance_squared = DistanceSquared(candidate, ball.position);
                candidate_distance_squared = std::min(candidate_distance_squared, distance_squared);
                if (distance_squared <= ball.radius * ball.radius) {
                    inside_alive_player = true;
                    break;
                }
            }

            if (inside_alive_player) {
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

} // namespace rrs
