#include "rrs/spatial/UniformGrid.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rrs {

UniformGridLayout::UniformGridLayout(Aabb bounds, float cell_size)
    : bounds_(bounds),
      cell_size_(cell_size),
      column_count_(static_cast<std::uint32_t>(std::ceil((bounds.max_x - bounds.min_x) / cell_size))),
      row_count_(static_cast<std::uint32_t>(std::ceil((bounds.max_y - bounds.min_y) / cell_size)))
{
}

std::size_t UniformGridLayout::cell_count() const
{
    return static_cast<std::size_t>(column_count_) * row_count_;
}

std::optional<GridCellRange> UniformGridLayout::CellRangeForBounds(Aabb bounds) const noexcept
{
    if (bounds.max_x < bounds_.min_x || bounds.min_x > bounds_.max_x ||
        bounds.max_y < bounds_.min_y || bounds.min_y > bounds_.max_y) {
        return std::nullopt;
    }

    return GridCellRange{
        .min = GridCellCoord{
            .x = CellX(bounds.min_x),
            .y = CellY(bounds.min_y),
        },
        .max = GridCellCoord{
            .x = CellX(bounds.max_x),
            .y = CellY(bounds.max_y),
        },
    };
}

std::size_t UniformGridLayout::CellIndex(GridCellCoord cell) const
{
    return static_cast<std::size_t>(cell.y) * column_count_ + cell.x;
}

std::uint32_t UniformGridLayout::CellX(float position) const
{
    const auto cell = static_cast<std::int64_t>(std::floor((position - bounds_.min_x) / cell_size_));
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cell, 0, column_count_ - 1));
}

std::uint32_t UniformGridLayout::CellY(float position) const
{
    const auto cell = static_cast<std::int64_t>(std::floor((position - bounds_.min_y) / cell_size_));
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cell, 0, row_count_ - 1));
}

UniformGridIndex::UniformGridIndex(UniformGridLayout layout)
    : layout_(layout),
      cell_offsets_(layout.cell_count() + 1, 0),
      rebuild_offsets_(layout.cell_count(), 0)
{
}

void UniformGridIndex::Rebuild(std::span<const Aabb> record_bounds)
{
    std::fill(cell_offsets_.begin(), cell_offsets_.end(), 0);

    for (const auto& bounds : record_bounds) {
        const auto cell_range = layout_.CellRangeForBounds(bounds);
        if (!cell_range.has_value()) {
            continue;
        }

        for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
            for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
                ++cell_offsets_[layout_.CellIndex({.x = x, .y = y}) + 1];
            }
        }
    }

    std::partial_sum(cell_offsets_.begin(), cell_offsets_.end(), cell_offsets_.begin());
    cell_record_indices_.resize(cell_offsets_.back());
    std::copy(cell_offsets_.begin(), cell_offsets_.end() - 1, rebuild_offsets_.begin());

    for (std::size_t record_index = 0; record_index < record_bounds.size(); ++record_index) {
        const auto cell_range = layout_.CellRangeForBounds(record_bounds[record_index]);
        if (!cell_range.has_value()) {
            continue;
        }

        for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
            for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
                const auto cell_index = layout_.CellIndex({.x = x, .y = y});
                cell_record_indices_[rebuild_offsets_[cell_index]++] =
                    static_cast<std::uint32_t>(record_index);
            }
        }
    }
}

std::span<const std::uint32_t> UniformGridIndex::RecordIndicesInCell(GridCellCoord cell) const noexcept
{
    const auto cell_index = layout_.CellIndex(cell);
    const auto begin = cell_offsets_[cell_index];
    const auto count = cell_offsets_[cell_index + 1] - begin;
    return std::span<const std::uint32_t>{cell_record_indices_}.subspan(begin, count);
}

} // namespace rrs
