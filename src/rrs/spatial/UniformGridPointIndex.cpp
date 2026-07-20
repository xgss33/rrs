#include "rrs/spatial/UniformGridPointIndex.h"

#include "rrs/math/Vector2.h"
#include "rrs/spatial/UniformGrid.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rrs {

UniformGridPointIndex::UniformGridPointIndex(UniformGridLayout layout)
    : layout_(layout)
    , records_by_cell_(layout.cell_count())
{
}

void UniformGridPointIndex::Rebuild(std::span<const Vector2> record_positions)
{
    for (auto& records : records_by_cell_) {
        records.clear();
    }

    record_locations_.resize(record_positions.size());
    candidate_record_indices_.reserve(record_positions.size());

    for (std::size_t record_index = 0; record_index < record_positions.size(); ++record_index) {
        const auto cell_index = layout_.CellIndex(layout_.CellForPosition(record_positions[record_index]));
        auto& cell_records = records_by_cell_[cell_index];
        record_locations_[record_index] = RecordLocation{
            .cell_index = cell_index,
            .slot_index = cell_records.size(),
        };
        cell_records.push_back(static_cast<std::uint32_t>(record_index));
    }
}

void UniformGridPointIndex::Relocate(std::uint32_t record_index, Vector2 position)
{
    const auto next_cell_index = layout_.CellIndex(layout_.CellForPosition(position));
    auto& location = record_locations_[record_index];
    if (location.cell_index == next_cell_index) {
        return;
    }

    auto& previous_cell_records = records_by_cell_[location.cell_index];
    const auto moved_record_index = previous_cell_records.back();
    previous_cell_records[location.slot_index] = moved_record_index;
    record_locations_[moved_record_index].slot_index = location.slot_index;
    previous_cell_records.pop_back();

    auto& next_cell_records = records_by_cell_[next_cell_index];
    location = RecordLocation{
        .cell_index = next_cell_index,
        .slot_index = next_cell_records.size(),
    };
    next_cell_records.push_back(record_index);
}

std::span<const std::uint32_t> UniformGridPointIndex::QueryCandidates(Aabb query_bounds)
{
    candidate_record_indices_.clear();

    const auto cell_range = layout_.CellRangeForBounds(query_bounds);
    if (!cell_range) {
        return candidate_record_indices_;
    }

    for (auto y = cell_range->min.y; y <= cell_range->max.y; ++y) {
        for (auto x = cell_range->min.x; x <= cell_range->max.x; ++x) {
            const auto& cell_records = records_by_cell_[layout_.CellIndex({.x = x, .y = y})];
            candidate_record_indices_.insert(
                candidate_record_indices_.end(),
                cell_records.begin(),
                cell_records.end());
        }
    }

    return candidate_record_indices_;
}

} // namespace rrs
