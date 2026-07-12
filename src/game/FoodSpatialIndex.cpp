#include "rrs/game/FoodSpatialIndex.h"
#include "rrs/game/RoomRules.h"

#include <algorithm>
#include <cmath>

namespace rrs {

void FoodSpatialIndex::Reset(std::size_t food_count)
{
    cells_.clear();
    cells_.resize(room_rules::kCellCount);

    food_slots_.clear();
    food_slots_.resize(food_count);

    dirty_food_indices_.clear();
    dirty_food_flags_.assign(food_count, 0);

    query_results_.clear();
    query_results_.reserve(food_count);
}

void FoodSpatialIndex::Add(FoodEntity food)
{
    const auto food_index = FoodIndex(food.food_id);
    const auto cell_index = CellIndexForPosition(food.position);
    auto& cell_foods = cells_[cell_index].foods;

    food_slots_[food_index] = FoodSlot{
        .cell_index = cell_index,
        .index_in_cell = cell_foods.size(),
    };
    cell_foods.push_back(food);
}

void FoodSpatialIndex::MoveTo(FoodId food_id, Vector2 position)
{
    const auto food_index = FoodIndex(food_id);
    const auto old_slot = food_slots_[food_index];
    auto& old_cell_foods = cells_[old_slot.cell_index].foods;

    auto food = old_cell_foods[old_slot.index_in_cell];
    const auto last_index = old_cell_foods.size() - 1;
    if (old_slot.index_in_cell != last_index) {
        old_cell_foods[old_slot.index_in_cell] = old_cell_foods[last_index];

        const auto swapped_food_index = FoodIndex(old_cell_foods[old_slot.index_in_cell].food_id);
        food_slots_[swapped_food_index] = FoodSlot{
            .cell_index = old_slot.cell_index,
            .index_in_cell = old_slot.index_in_cell,
        };
    }

    old_cell_foods.pop_back();
    food.position = position;
    Add(food);

    if (dirty_food_flags_[food_index] == 0) {
        dirty_food_flags_[food_index] = 1;
        dirty_food_indices_.push_back(food_index);
    }
}

const FoodEntity& FoodSpatialIndex::FoodByIndex(std::size_t food_index) const
{
    const auto slot = food_slots_[food_index];
    return cells_[slot.cell_index].foods[slot.index_in_cell];
}

std::span<const FoodEntity> FoodSpatialIndex::Query(Vector2 center, float radius)
{
    const auto min_cell_x = CellCoordForPosition(center.x - radius);
    const auto max_cell_x = CellCoordForPosition(center.x + radius);
    const auto min_cell_y = CellCoordForPosition(center.y - radius);
    const auto max_cell_y = CellCoordForPosition(center.y + radius);

    query_results_.clear();
    for (std::size_t cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y) {
        for (std::size_t cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
            const auto& cell_foods = cells_[CellIndexForCoord(cell_x, cell_y)].foods;
            for (const auto& food : cell_foods) {
                query_results_.push_back(food);
            }
        }
    }

    return query_results_;
}

void FoodSpatialIndex::AppendFullSnapshot(std::vector<FoodStateSnapshot>& output) const
{
    for (std::size_t food_index = 0; food_index < food_slots_.size(); ++food_index) {
        const auto& food = FoodByIndex(food_index);
        output.push_back(FoodStateSnapshot{
            .food_id = food.food_id,
            .position = food.position,
        });
    }
}

void FoodSpatialIndex::AppendDeltaSnapshot(std::vector<FoodStateSnapshot>& output)
{
    for (const auto food_index : dirty_food_indices_) {
        const auto& food = FoodByIndex(food_index);
        output.push_back(FoodStateSnapshot{
            .food_id = food.food_id,
            .position = food.position,
        });
        dirty_food_flags_[food_index] = 0;
    }

    dirty_food_indices_.clear();
}

std::size_t FoodSpatialIndex::FoodIndex(FoodId food_id) noexcept
{
    return static_cast<std::size_t>(food_id.value() - 1);
}

std::size_t FoodSpatialIndex::CellCoordForPosition(float value) noexcept
{
    const auto normalized = (value + room_rules::kRoomHalfExtent) / room_rules::kCellSize;
    const auto coord = std::clamp(
        static_cast<int>(std::floor(normalized)),
        0,
        static_cast<int>(room_rules::kGridSize - 1));
    return static_cast<std::size_t>(coord);
}

std::size_t FoodSpatialIndex::CellIndexForPosition(Vector2 position) noexcept
{
    return CellIndexForCoord(
        CellCoordForPosition(position.x),
        CellCoordForPosition(position.y));
}

std::size_t FoodSpatialIndex::CellIndexForCoord(std::size_t x, std::size_t y) noexcept
{
    return y * room_rules::kGridSize + x;
}

} // namespace rrs
