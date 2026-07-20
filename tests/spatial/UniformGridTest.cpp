#include "rrs/spatial/UniformGrid.h"
#include "rrs/spatial/UniformGridAabbIndex.h"

#include "rrs/math/Vector2.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr rrs::Aabb MakeAabb(float min_x, float min_y, float max_x, float max_y)
{
    return rrs::Aabb{
        .min = rrs::Vector2{.x = min_x, .y = min_y},
        .max = rrs::Vector2{.x = max_x, .y = max_y},
    };
}

constexpr auto kRoomBounds = MakeAabb(-1024.0F, -1024.0F, 1024.0F, 1024.0F);

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
    Expect(layout.cell_count() == 1024, "layout cell count");

    const auto single_cell = layout.CellRangeForBounds(MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F));
    Expect(single_cell == rrs::GridCellRange{{0, 0}, {0, 0}}, "single-cell AABB mapping");

    const auto crossing = layout.CellRangeForBounds(MakeAabb(-970.0F, -970.0F, -950.0F, -950.0F));
    Expect(crossing == rrs::GridCellRange{{0, 0}, {1, 1}}, "cross-cell AABB mapping");

    const auto closed_boundary = layout.CellRangeForBounds(MakeAabb(-1000.0F, -1000.0F, -960.0F, -960.0F));
    Expect(closed_boundary == rrs::GridCellRange{{0, 0}, {1, 1}}, "closed AABB boundary mapping");
}

void TestRoomEdgeClipping()
{
    const auto layout = MakeLayout();
    const auto lower_edge = layout.CellRangeForBounds(MakeAabb(-1100.0F, -1100.0F, -1000.0F, -1000.0F));
    Expect(lower_edge == rrs::GridCellRange{{0, 0}, {0, 0}}, "lower room edge clipping");

    const auto upper_edge = layout.CellRangeForBounds(MakeAabb(1000.0F, 1000.0F, 1100.0F, 1100.0F));
    Expect(upper_edge == rrs::GridCellRange{{31, 31}, {31, 31}}, "upper room edge clipping");

    const auto outside = layout.CellRangeForBounds(MakeAabb(1100.0F, 1100.0F, 1200.0F, 1200.0F));
    Expect(!outside.has_value(), "fully outside AABB exclusion");
}

void TestStableRecordOrder()
{
    rrs::UniformGridAabbIndex grid{MakeLayout()};
    const std::array records{
        MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F),
        MakeAabb(-970.0F, -1000.0F, -950.0F, -990.0F),
        MakeAabb(-1005.0F, -1000.0F, -995.0F, -990.0F),
    };

    grid.Rebuild(records);

    ExpectIndices(
        grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -961.0F, -961.0F)),
        std::array<std::uint32_t, 3>{0, 1, 2},
        "stable record order in first cell");
    ExpectIndices(
        grid.QueryCandidates(MakeAabb(-959.0F, -1020.0F, -900.0F, -961.0F)),
        std::array<std::uint32_t, 1>{1},
        "multi-cell record reference");
}

void TestRebuildReplacesOldIndex()
{
    rrs::UniformGridAabbIndex grid{MakeLayout()};
    const std::array first_records{
        MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F),
    };
    grid.Rebuild(first_records);

    const std::array second_records{
        MakeAabb(1000.0F, 1000.0F, 1010.0F, 1010.0F),
    };
    grid.Rebuild(second_records);

    Expect(
        grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -970.0F, -970.0F)).empty(),
        "rebuild clears old cell references");
    ExpectIndices(
        grid.QueryCandidates(MakeAabb(1000.0F, 1000.0F, 1010.0F, 1010.0F)),
        std::array<std::uint32_t, 1>{0},
        "rebuild writes new cell references");
}

void TestDeterministicRebuild()
{
    rrs::UniformGridAabbIndex grid{MakeLayout()};
    const std::array records{
        MakeAabb(-970.0F, -970.0F, -950.0F, -950.0F),
        MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F),
        MakeAabb(-965.0F, -965.0F, -955.0F, -955.0F),
    };
    grid.Rebuild(records);

    const auto first_span = grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -961.0F, -961.0F));
    const auto first_result = std::vector<std::uint32_t>{
        first_span.begin(),
        first_span.end(),
    };
    grid.Rebuild(records);
    const auto second_result = grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -961.0F, -961.0F));

    Expect(std::ranges::equal(first_result, second_result), "deterministic rebuild output");
}

void TestCandidateQueries()
{
    rrs::UniformGridAabbIndex grid{MakeLayout()};
    const std::array records{
        MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F),
        MakeAabb(-950.0F, -1000.0F, -940.0F, -990.0F),
        MakeAabb(-970.0F, -1000.0F, -950.0F, -990.0F),
    };
    grid.Rebuild(records);

    ExpectIndices(
        grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -961.0F, -961.0F)),
        std::array<std::uint32_t, 2>{0, 2},
        "first cell candidate query");
    ExpectIndices(
        grid.QueryCandidates(MakeAabb(-959.0F, -1020.0F, -900.0F, -961.0F)),
        std::array<std::uint32_t, 2>{1, 2},
        "consecutive candidate query");
    ExpectIndices(
        grid.QueryCandidates(MakeAabb(-1020.0F, -1020.0F, -900.0F, -961.0F)),
        std::array<std::uint32_t, 3>{0, 2, 1},
        "multi-cell candidate deduplication");
    Expect(
        grid.QueryCandidates(MakeAabb(1100.0F, 1100.0F, 1200.0F, 1200.0F)).empty(),
        "outside candidate query");
}

void TestMultipleQueryBoundsShareDeduplication()
{
    rrs::UniformGridAabbIndex grid{MakeLayout()};
    const std::array records{
        MakeAabb(-1000.0F, -1000.0F, -990.0F, -990.0F),
        MakeAabb(-950.0F, -1000.0F, -940.0F, -990.0F),
        MakeAabb(-970.0F, -1000.0F, -950.0F, -990.0F),
    };
    grid.Rebuild(records);

    const std::array query_bounds{
        MakeAabb(-1020.0F, -1020.0F, -961.0F, -961.0F),
        MakeAabb(-959.0F, -1020.0F, -900.0F, -961.0F),
    };
    ExpectIndices(
        grid.QueryCandidates(query_bounds),
        std::array<std::uint32_t, 3>{0, 2, 1},
        "multiple query bounds share record deduplication");
}

} // namespace

int main()
{
    TestCellRangeMapping();
    TestRoomEdgeClipping();
    TestStableRecordOrder();
    TestRebuildReplacesOldIndex();
    TestDeterministicRebuild();
    TestCandidateQueries();
    TestMultipleQueryBoundsShareDeduplication();
    std::cout << "UniformGrid tests passed\n";
    return EXIT_SUCCESS;
}
