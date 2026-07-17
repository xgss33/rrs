#include "rrs/game/FoodSpatialIndex.h"

#include "rrs/game/RoomRules.h"

namespace rrs {

namespace {

Aabb AabbForCircle(Vector2 center, float radius)
{
    const auto extent = Vector2{.x = radius, .y = radius};
    return Aabb{
        .min = center - extent,
        .max = center + extent,
    };
}

} // namespace

FoodSpatialIndex::FoodSpatialIndex(UniformGridLayout layout)
    : grid_(layout)
{
}

void FoodSpatialIndex::Rebuild(std::span<const FoodEntity> foods)
{
    food_bounds_.clear();
    food_bounds_.reserve(foods.size());
    for (const auto& food : foods) {
        food_bounds_.push_back(AabbForCircle(food.position, room_rules::kFoodRadius));
    }

    grid_.Rebuild(food_bounds_);
}

std::span<const std::uint32_t> FoodSpatialIndex::QueryCandidates(Vector2 center, float radius)
{
    return grid_.QueryCandidates(AabbForCircle(center, radius));
}

} // namespace rrs
