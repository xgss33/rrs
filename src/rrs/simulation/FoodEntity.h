#pragma once

#include "rrs/math/Vector2.h"

#include <cstdint>

namespace rrs {

using FoodIndex = std::uint16_t;

struct FoodEntity {
    Vector2 position;
};

} // namespace rrs
