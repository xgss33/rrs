#include "rrs/spatial/UniformGrid.h"

#include "rrs/math/Vector2.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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

std::optional<GridCellRange> UniformGridLayout::CellRangeForBounds(Aabb bounds) const
{
    if (bounds.max.x < bounds_.min.x || bounds.min.x > bounds_.max.x ||
        bounds.max.y < bounds_.min.y || bounds.min.y > bounds_.max.y) {
        return std::nullopt;
    }

    return GridCellRange{
        .min = GridCellCoord{
            .x = CellX(bounds.min.x),
            .y = CellY(bounds.min.y),
        },
        .max = GridCellCoord{
            .x = CellX(bounds.max.x),
            .y = CellY(bounds.max.y),
        },
    };
}

std::size_t UniformGridLayout::CellIndex(GridCellCoord cell) const
{
    return static_cast<std::size_t>(cell.y) * column_count_ + cell.x;
}

std::uint32_t UniformGridLayout::CellX(float position) const
{
    const auto cell = static_cast<std::int64_t>(std::floor((position - bounds_.min.x) / cell_size_));
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cell, 0, column_count_ - 1));
}

std::uint32_t UniformGridLayout::CellY(float position) const
{
    const auto cell = static_cast<std::int64_t>(std::floor((position - bounds_.min.y) / cell_size_));
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(cell, 0, row_count_ - 1));
}

UniformGridIndex::UniformGridIndex(UniformGridLayout layout)
    : layout_(layout)
    , cell_offsets_(layout.cell_count() + 1, 0)
    , cell_write_offsets_(layout.cell_count(), 0)
{
}

void UniformGridIndex::Rebuild(std::span<const Aabb> record_bounds)
{
    std::fill(cell_offsets_.begin(), cell_offsets_.end(), 0);
    candidate_record_indices_.clear();
    candidate_record_indices_.reserve(record_bounds.size());
    record_query_stamps_.assign(record_bounds.size(), 0);
    query_stamp_ = 0;

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
    std::copy(cell_offsets_.begin(), cell_offsets_.end() - 1, cell_write_offsets_.begin());

    for (std::size_t record_index = 0; record_index < record_bounds.size(); ++record_index) {
        const auto cell_range = layout_.CellRangeForBounds(record_bounds[record_index]);
        if (!cell_range.has_value()) {
            continue;
        }

        for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
            for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
                const auto cell_index = layout_.CellIndex({.x = x, .y = y});
                cell_record_indices_[cell_write_offsets_[cell_index]++] =
                    static_cast<std::uint32_t>(record_index);
            }
        }
    }
}

std::span<const std::uint32_t> UniformGridIndex::RecordIndicesInCell(GridCellCoord cell) const
{
    const auto cell_index = layout_.CellIndex(cell);
    const auto begin = cell_offsets_[cell_index];
    const auto count = cell_offsets_[cell_index + 1] - begin;
    return std::span<const std::uint32_t>{cell_record_indices_}.subspan(begin, count);
}

std::span<const std::uint32_t> UniformGridIndex::QueryCandidates(Aabb query_bounds)
{
    candidate_record_indices_.clear();

    const auto cell_range = layout_.CellRangeForBounds(query_bounds);
    if (!cell_range.has_value()) {
        return candidate_record_indices_;
    }

    ++query_stamp_;
    for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
        for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
            for (const auto record_index : RecordIndicesInCell({.x = x, .y = y})) {
                if (record_query_stamps_[record_index] == query_stamp_) {
                    continue;
                }

                record_query_stamps_[record_index] = query_stamp_;
                candidate_record_indices_.push_back(record_index);
            }
        }
    }

    return candidate_record_indices_;
}

} // namespace rrs
