#include "rrs/spatial/UniformGrid.h"

#include "rrs/math/Vector2.h"

#include <algorithm>
#include <cmath>

namespace rrs {

Aabb AabbForCircle(Vector2 center, float radius)
{
    const auto extent = Vector2{.x = radius, .y = radius};
    return Aabb{
        .min = center - extent,
        .max = center + extent,
    };
}

UniformGridLayout::UniformGridLayout(Aabb bounds, float cell_size)
    : bounds_(bounds)
    , cell_size_(cell_size)
    , column_count_(static_cast<std::uint32_t>(std::ceil((bounds.max.x - bounds.min.x) / cell_size)))
    , row_count_(static_cast<std::uint32_t>(std::ceil((bounds.max.y - bounds.min.y) / cell_size)))
{
}

std::size_t UniformGridLayout::cell_count() const
{
    return static_cast<std::size_t>(column_count_) * row_count_;
}

GridCellCoord UniformGridLayout::CellForPosition(Vector2 position) const
{
    return GridCellCoord{
        .x = CellCoordinate(position.x, bounds_.min.x, column_count_),
        .y = CellCoordinate(position.y, bounds_.min.y, row_count_),
    };
}

std::optional<GridCellRange> UniformGridLayout::CellRangeForBounds(Aabb bounds) const
{
    if (bounds.max.x < bounds_.min.x || bounds.min.x > bounds_.max.x ||
        bounds.max.y < bounds_.min.y || bounds.min.y > bounds_.max.y) {
        return std::nullopt;
    }

    return GridCellRange{
        .min = GridCellCoord{
            .x = CellCoordinate(bounds.min.x, bounds_.min.x, column_count_),
            .y = CellCoordinate(bounds.min.y, bounds_.min.y, row_count_),
        },
        .max = GridCellCoord{
            .x = CellCoordinate(bounds.max.x, bounds_.min.x, column_count_),
            .y = CellCoordinate(bounds.max.y, bounds_.min.y, row_count_),
        },
    };
}

std::size_t UniformGridLayout::CellIndex(GridCellCoord cell) const
{
    return static_cast<std::size_t>(cell.y) * column_count_ + cell.x;
}

std::uint32_t UniformGridLayout::CellCoordinate(
    float position,
    float minimum,
    std::uint32_t cell_count) const
{
    const auto cell = static_cast<std::int64_t>(std::floor((position - minimum) / cell_size_));
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cell, 0, cell_count - 1));
}

} // namespace rrs
