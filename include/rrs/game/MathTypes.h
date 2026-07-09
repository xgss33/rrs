#pragma once

#include <cmath>

namespace rrs {

struct Vector2 {
    float x{0.0F};
    float y{0.0F};
};

[[nodiscard]] inline float LengthSquared(Vector2 value) noexcept
{
    return value.x * value.x + value.y * value.y;
}

[[nodiscard]] inline float Length(Vector2 value) noexcept
{
    return std::sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vector2 NormalizeOrZero(Vector2 value) noexcept
{
    const auto length = Length(value);
    if (length <= 0.0001F) {
        return {};
    }

    return Vector2{
        .x = value.x / length,
        .y = value.y / length,
    };
}

[[nodiscard]] inline float DistanceSquared(Vector2 left, Vector2 right) noexcept
{
    const auto dx = left.x - right.x;
    const auto dy = left.y - right.y;
    return dx * dx + dy * dy;
}

} // namespace rrs
