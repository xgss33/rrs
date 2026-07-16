#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rrs {

// 后续可以耦合vector2
struct Aabb {
    float min_x{0.0F};
    float min_y{0.0F};
    float max_x{0.0F};
    float max_y{0.0F};
};

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
    UniformGridLayout(Aabb bounds, float cell_size);

    const Aabb& bounds() const { return bounds_; }
    float cell_size() const { return cell_size_; }
    std::uint32_t column_count() const { return column_count_; }
    std::uint32_t row_count() const { return row_count_; }
    std::size_t cell_count() const;

    // 将AABB转换成覆盖的单元格范围
    [[nodiscard]] std::optional<GridCellRange> CellRangeForBounds(Aabb bounds) const noexcept;
    // 将二维坐标转换成一维数组下标. 好像是缓存考虑
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

    const UniformGridLayout& layout() const { return layout_; }
    std::size_t spatial_reference_count() const
    {
        return cell_record_indices_.size();
    }
    [[nodiscard]] std::span<const std::uint32_t> RecordIndicesInCell(GridCellCoord cell) const noexcept;

private:
    UniformGridLayout layout_;
    std::vector<std::size_t> cell_offsets_;     // = 单元数量 + 1 加1是为了左闭右开
    std::vector<std::uint32_t> cell_record_indices_;    // 记录单元格
    std::vector<std::size_t> rebuild_offsets_;  // 不明白, 和重建有关
};

} // namespace rrs
