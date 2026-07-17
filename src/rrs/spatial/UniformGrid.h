#pragma once

#include "rrs/math/Vector2.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rrs {

struct Aabb {
    Vector2 min;
    Vector2 max;
};

Aabb AabbForCircle(Vector2 center, float radius);

// 二维单元格坐标, 左下角为00, 实际访问转换为1维下标
struct GridCellCoord {
    std::uint32_t x{0};
    std::uint32_t y{0};

    bool operator==(const GridCellCoord&) const = default;
};

// 表示一个aabb覆盖的单元格范围
struct GridCellRange {
    GridCellCoord min;
    GridCellCoord max;

    bool operator==(const GridCellRange&) const = default;
};

class UniformGridLayout {
public:
    // 建立网格坐标系, bounds决定网格覆盖的世界范围, cell_size决定每个正方形单元边长
    UniformGridLayout(Aabb bounds, float cell_size);
    // 返回单元格数量
    std::size_t cell_count() const;
    // 将AABB转换成覆盖的单元格范围
    [[nodiscard]] std::optional<GridCellRange> CellRangeForBounds(Aabb bounds) const;
    // 以行优先顺序将二维单元格坐标转换成连续数组下标
    std::size_t CellIndex(GridCellCoord cell) const;

private:
    std::uint32_t CellX(float position) const;
    std::uint32_t CellY(float position) const;

    Aabb bounds_;                   // 整个网格的范围, 当前应该是room的大小
    float cell_size_{0.0F};         // 单元格长宽
    std::uint32_t column_count_{0}; // 单元格列
    std::uint32_t row_count_{0};    // 行
};

class UniformGridIndex {
public:
    explicit UniformGridIndex(UniformGridLayout layout);

    void Rebuild(std::span<const Aabb> record_bounds);

    std::size_t spatial_reference_count() const
    {
        return cell_record_indices_.size();
    }
    [[nodiscard]] std::span<const std::uint32_t> QueryCandidates(Aabb query_bounds);

private:
    std::span<const std::uint32_t> RecordIndicesInCell(GridCellCoord cell) const;

    UniformGridLayout layout_;
    std::vector<std::size_t> cell_offsets_; // 单元数量 + 1，用于表示左闭右开的记录范围
    std::vector<std::uint32_t> cell_record_indices_; // 各单元格包含的记录下标
    std::vector<std::size_t> cell_write_offsets_; // 第二遍重建时每个单元格的写入位置
    std::vector<std::uint32_t> candidate_record_indices_;
    std::vector<std::uint64_t> record_query_stamps_;
    std::uint64_t query_stamp_{0};
};

} // namespace rrs
