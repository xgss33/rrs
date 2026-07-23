#include "rrs/simulation/spatial/FoodSpatialIndex.h"

#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/spatial/UniformGrid.h"

#include <cstdint>
#include <vector>

namespace rrs {

FoodSpatialIndex::FoodSpatialIndex(UniformGridLayout layout)
    : grid_(layout)
{
}

void FoodSpatialIndex::Initialize(std::span<const FoodEntity> foods)
{
    auto food_positions = std::vector<Vector2>{};
    food_positions.reserve(foods.size());
    for (const auto& food : foods) {
        food_positions.push_back(food.position);
    }

    grid_.Rebuild(food_positions);
}

void FoodSpatialIndex::Relocate(std::uint32_t food_index, Vector2 position)
{
    grid_.Relocate(food_index, position);
}

std::span<const std::uint32_t> FoodSpatialIndex::QueryCandidates(Vector2 center, float search_radius)
{
    return grid_.QueryCandidates(AabbForCircle(center, search_radius));
}

} // namespace rrs
