#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/MathTypes.h"

namespace rrs {

struct FoodEntity {
    FoodId food_id;
    Vector2 position;
    float radius{4.0F};
};

} // namespace rrs
