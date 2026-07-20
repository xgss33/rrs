#pragma once

#include "rrs/math/Vector2.h"
#include "rrs/simulation/FoodEntity.h"
#include "rrs/spatial/UniformGrid.h"
#include "rrs/spatial/UniformGridPointIndex.h"

#include <cstdint>
#include <span>

namespace rrs {

class FoodSpatialIndex {
public:
    explicit FoodSpatialIndex(UniformGridLayout layout);

    void Initialize(std::span<const FoodEntity> foods);
    void Relocate(FoodIndex food_index, Vector2 position);
    [[nodiscard]] std::span<const std::uint32_t> QueryCandidates(Vector2 center, float search_radius);

private:
    UniformGridPointIndex grid_;
};

} // namespace rrs
