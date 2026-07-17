#pragma once

#include <cmath>

namespace rrs {

struct Vector2 {
    float x{0.0F};
    float y{0.0F};
};

constexpr Vector2 operator+(Vector2 left, Vector2 right)
{
    return Vector2{.x = left.x + right.x, .y = left.y + right.y};
}

constexpr Vector2 operator-(Vector2 left, Vector2 right)
{
    return Vector2{.x = left.x - right.x, .y = left.y - right.y};
}

constexpr Vector2 operator*(Vector2 value, float scalar)
{
    return Vector2{.x = value.x * scalar, .y = value.y * scalar};
}

constexpr Vector2 operator*(float scalar, Vector2 value)
{
    return value * scalar;
}

constexpr float LengthSquared(Vector2 value)
{
    return value.x * value.x + value.y * value.y;
}

inline float Length(Vector2 value)
{
    return std::sqrt(LengthSquared(value));
}

inline Vector2 NormalizeOrZero(Vector2 value)
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

constexpr float DistanceSquared(Vector2 left, Vector2 right)
{
    return LengthSquared(left - right);
}

} // namespace rrs
