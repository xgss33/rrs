#pragma once

#include "rrs/game/FoodEntity.h"
#include "rrs/math/Vector2.h"
#include "rrs/spatial/UniformGrid.h"

#include <cstdint>
#include <span>
#include <vector>

namespace rrs {

class FoodSpatialIndex {
public:
    explicit FoodSpatialIndex(UniformGridLayout layout);

    void Rebuild(std::span<const FoodEntity> foods);
    [[nodiscard]] std::span<const std::uint32_t> QueryCandidates(Vector2 center, float radius);

private:
    UniformGridIndex grid_;
    std::vector<Aabb> food_bounds_;
};

} // namespace rrs
