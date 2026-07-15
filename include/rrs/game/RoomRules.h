#pragma once

#include <chrono>
#include <cstddef>

namespace rrs::room_rules {
inline constexpr float kRoomHalfExtent = 1024.0F;

inline constexpr float kFoodCellSize = 64.0F;
inline constexpr std::size_t kFoodGridSideCellCount = 32;
inline constexpr std::size_t kFoodCellCount = kFoodGridSideCellCount * kFoodGridSideCellCount;

inline constexpr float kInitialPlayerRadius = 12.0F;
inline constexpr float kFoodRadius = 4.0F;
inline constexpr std::size_t kFoodCount = 1024;

inline constexpr float kBaseSpeed = 180.0F;
inline constexpr float kMinSpeed = 60.0F;
inline constexpr float kMaxSpeed = 220.0F;
inline constexpr float kSpeedReferenceRadius = 12.0F;
inline constexpr float kSpeedRadiusExponent = 0.4F;

inline constexpr float kFoodGrowthRatio = 0.55F;
inline constexpr float kEatRadiusRatio = 1.15F;
inline constexpr float kEatCenterRatio = 0.75F;
inline constexpr float kPlayerGrowthRatio = 0.75F;
inline constexpr float kSplitSpawnDistanceRatio = 3.0F;

inline constexpr std::chrono::seconds kRespawnDelay{5};
inline constexpr std::chrono::seconds kMatchDuration{600};
inline constexpr std::size_t kRespawnSearchAttempts = 16;

} // namespace rrs::room_rules
