#include "rrs/spatial/UniformGridAabbIndex.h"

#include "rrs/spatial/UniformGrid.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>

namespace rrs {

UniformGridAabbIndex::UniformGridAabbIndex(UniformGridLayout layout)
    : layout_(layout)
    , cell_offsets_(layout.cell_count() + 1, 0)
    , cell_write_offsets_(layout.cell_count(), 0)
{
}

void UniformGridAabbIndex::Rebuild(std::span<const Aabb> record_bounds)
{
    std::fill(cell_offsets_.begin(), cell_offsets_.end(), 0);
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

std::span<const std::uint32_t> UniformGridAabbIndex::RecordIndicesInCell(std::size_t cell_index) const
{
    const auto begin = cell_offsets_[cell_index];
    const auto count = cell_offsets_[cell_index + 1] - begin;
    return std::span<const std::uint32_t>{cell_record_indices_}.subspan(begin, count);
}

std::span<const std::uint32_t> UniformGridAabbIndex::QueryCandidates(Aabb query_bounds)
{
    candidate_record_indices_.clear();
    ++query_stamp_;

    const auto cell_range = layout_.CellRangeForBounds(query_bounds);
    if (!cell_range.has_value()) {
        return candidate_record_indices_;
    }

    for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
        for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
            const auto cell_index = layout_.CellIndex({.x = x, .y = y});
            for (const auto record_index : RecordIndicesInCell(cell_index)) {
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
