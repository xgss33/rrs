#pragma once

#include "rrs/base/Types.h"
#include "rrs/game/FoodEntity.h"
#include "rrs/game/MathTypes.h"
#include "rrs/game/RoomSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rrs {

class FoodSpatialIndex {
public:
    void Reset(std::size_t food_count);
    void Add(FoodEntity food);
    void MoveTo(FoodId food_id, Vector2 position);

    [[nodiscard]] std::size_t food_count() const noexcept { return food_slots_.size(); }
    [[nodiscard]] std::size_t dirty_count() const noexcept { return dirty_food_indices_.size(); }
    [[nodiscard]] std::span<const FoodEntity> Query(Vector2 center, float radius);

    void AppendFullSnapshot(std::vector<FoodStateSnapshot>& output) const;
    void AppendDeltaSnapshot(std::vector<FoodStateSnapshot>& output);

private:
    struct Cell {
        std::vector<FoodEntity> foods;
    };

    struct FoodSlot {
        std::size_t cell_index{0};
        std::size_t index_in_cell{0};
    };

    [[nodiscard]] const FoodEntity& FoodByIndex(std::size_t food_index) const;
    [[nodiscard]] static std::size_t FoodIndex(FoodId food_id) noexcept;
    [[nodiscard]] static std::size_t CellCoordForPosition(float value) noexcept;
    [[nodiscard]] static std::size_t CellIndexForPosition(Vector2 position) noexcept;
    [[nodiscard]] static std::size_t CellIndexForCoord(std::size_t x, std::size_t y) noexcept;

    std::vector<Cell> cells_;
    std::vector<FoodSlot> food_slots_;
    std::vector<std::size_t> dirty_food_indices_;
    std::vector<std::uint8_t> dirty_food_flags_;
    std::vector<FoodEntity> query_results_;
};

} // namespace rrs
