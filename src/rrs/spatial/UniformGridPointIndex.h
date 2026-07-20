#pragma once

#include "rrs/math/Vector2.h"
#include "rrs/spatial/UniformGrid.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rrs {

class UniformGridPointIndex {
public:
    explicit UniformGridPointIndex(UniformGridLayout layout);

    void Rebuild(std::span<const Vector2> record_positions);
    void Relocate(std::uint32_t record_index, Vector2 position);
    [[nodiscard]] std::span<const std::uint32_t> QueryCandidates(Aabb query_bounds);

private:
    struct RecordLocation {
        std::size_t cell_index{0};
        std::size_t slot_index{0};
    };

    UniformGridLayout layout_;
    std::vector<std::vector<std::uint32_t>> records_by_cell_;
    std::vector<RecordLocation> record_locations_;
    std::vector<std::uint32_t> candidate_record_indices_;
};

} // namespace rrs
