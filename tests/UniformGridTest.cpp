#include "rrs/spatial/UniformGrid.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr rrs::Aabb kRoomBounds{
    .min_x = -1024.0F,
    .min_y = -1024.0F,
    .max_x = 1024.0F,
    .max_y = 1024.0F,
};

void Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <std::size_t Size>
void ExpectIndices(
    std::span<const std::uint32_t> actual,
    const std::array<std::uint32_t, Size>& expected,
    std::string_view message)
{
    Expect(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        Expect(actual[index] == expected[index], message);
    }
}

rrs::UniformGridLayout MakeLayout()
{
    return rrs::UniformGridLayout{kRoomBounds, 64.0F};
}

void TestCellRangeMapping()
{
    const auto layout = MakeLayout();
    Expect(layout.column_count() == 32, "layout column count");
    Expect(layout.row_count() == 32, "layout row count");
    Expect(layout.cell_count() == 1024, "layout cell count");

    const auto single_cell = layout.CellRangeForBounds({
        .min_x = -1000.0F,
        .min_y = -1000.0F,
        .max_x = -990.0F,
        .max_y = -990.0F,
    });
    Expect(single_cell == rrs::GridCellRange{{0, 0}, {0, 0}}, "single-cell AABB mapping");

    const auto crossing = layout.CellRangeForBounds({
        .min_x = -970.0F,
        .min_y = -970.0F,
        .max_x = -950.0F,
        .max_y = -950.0F,
    });
    Expect(crossing == rrs::GridCellRange{{0, 0}, {1, 1}}, "cross-cell AABB mapping");

    const auto closed_boundary = layout.CellRangeForBounds({
        .min_x = -1000.0F,
        .min_y = -1000.0F,
        .max_x = -960.0F,
        .max_y = -960.0F,
    });
    Expect(closed_boundary == rrs::GridCellRange{{0, 0}, {1, 1}}, "closed AABB boundary mapping");
}

void TestRoomEdgeClipping()
{
    const auto layout = MakeLayout();
    const auto lower_edge = layout.CellRangeForBounds({
        .min_x = -1100.0F,
        .min_y = -1100.0F,
        .max_x = -1000.0F,
        .max_y = -1000.0F,
    });
    Expect(lower_edge == rrs::GridCellRange{{0, 0}, {0, 0}}, "lower room edge clipping");

    const auto upper_edge = layout.CellRangeForBounds({
        .min_x = 1000.0F,
        .min_y = 1000.0F,
        .max_x = 1100.0F,
        .max_y = 1100.0F,
    });
    Expect(upper_edge == rrs::GridCellRange{{31, 31}, {31, 31}}, "upper room edge clipping");

    const auto outside = layout.CellRangeForBounds({
        .min_x = 1100.0F,
        .min_y = 1100.0F,
        .max_x = 1200.0F,
        .max_y = 1200.0F,
    });
    Expect(!outside.has_value(), "fully outside AABB exclusion");
}

void TestStableRecordOrder()
{
    rrs::UniformGridIndex grid{MakeLayout()};
    const std::array records{
        rrs::Aabb{-1000.0F, -1000.0F, -990.0F, -990.0F},
        rrs::Aabb{-970.0F, -1000.0F, -950.0F, -990.0F},
        rrs::Aabb{-1005.0F, -1000.0F, -995.0F, -990.0F},
    };

    grid.Rebuild(records);

    ExpectIndices(grid.RecordIndicesInCell({0, 0}), std::array<std::uint32_t, 3>{0, 1, 2},
        "stable record order in first cell");
    ExpectIndices(grid.RecordIndicesInCell({1, 0}), std::array<std::uint32_t, 1>{1},
        "multi-cell record reference");
    Expect(grid.spatial_reference_count() == 4, "spatial reference count");
}

void TestRebuildReplacesOldIndex()
{
    rrs::UniformGridIndex grid{MakeLayout()};
    const std::array first_records{
        rrs::Aabb{-1000.0F, -1000.0F, -990.0F, -990.0F},
    };
    grid.Rebuild(first_records);

    const std::array second_records{
        rrs::Aabb{1000.0F, 1000.0F, 1010.0F, 1010.0F},
    };
    grid.Rebuild(second_records);

    Expect(grid.RecordIndicesInCell({0, 0}).empty(), "rebuild clears old cell references");
    ExpectIndices(grid.RecordIndicesInCell({31, 31}), std::array<std::uint32_t, 1>{0},
        "rebuild writes new cell references");
}

void TestDeterministicRebuild()
{
    rrs::UniformGridIndex grid{MakeLayout()};
    const std::array records{
        rrs::Aabb{-970.0F, -970.0F, -950.0F, -950.0F},
        rrs::Aabb{-1000.0F, -1000.0F, -990.0F, -990.0F},
        rrs::Aabb{-965.0F, -965.0F, -955.0F, -955.0F},
    };
    grid.Rebuild(records);

    const auto first_span = grid.RecordIndicesInCell({0, 0});
    const auto first_result = std::vector<std::uint32_t>{
        first_span.begin(),
        first_span.end(),
    };
    grid.Rebuild(records);
    const auto second_result = grid.RecordIndicesInCell({0, 0});

    Expect(std::ranges::equal(first_result, second_result), "deterministic rebuild output");
}

} // namespace

int main()
{
    TestCellRangeMapping();
    TestRoomEdgeClipping();
    TestStableRecordOrder();
    TestRebuildReplacesOldIndex();
    TestDeterministicRebuild();
    std::cout << "UniformGrid tests passed\n";
    return EXIT_SUCCESS;
}
