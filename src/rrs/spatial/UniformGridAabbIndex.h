#pragma once

#include "rrs/spatial/UniformGrid.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rrs {

class UniformGridAabbIndex {
public:
    explicit UniformGridAabbIndex(UniformGridLayout layout);

    void Rebuild(std::span<const Aabb> record_bounds);
    [[nodiscard]] std::span<const std::uint32_t> QueryCandidates(Aabb query_bounds);

private:
    std::span<const std::uint32_t> RecordIndicesInCell(std::size_t cell_index) const;

    UniformGridLayout layout_;
    std::vector<std::size_t> cell_offsets_;
    std::vector<std::uint32_t> cell_record_indices_;
    std::vector<std::size_t> cell_write_offsets_;
    std::vector<std::uint32_t> candidate_record_indices_;
    std::vector<std::uint64_t> record_query_stamps_;
    std::uint64_t query_stamp_{0};
};

} // namespace rrs
