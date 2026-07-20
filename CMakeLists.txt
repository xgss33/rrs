cmake_minimum_required(VERSION 3.20)

project(rrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(third_party/spdlog)

add_library(rrs_spatial STATIC
    src/rrs/spatial/UniformGrid.cpp
    src/rrs/spatial/UniformGridAabbIndex.cpp
    src/rrs/spatial/UniformGridPointIndex.cpp
)

target_include_directories(rrs_spatial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(rrs
    src/main.cpp
    src/rrs/core/Threading.cpp
    src/rrs/config/ServerConfig.cpp
    src/rrs/network/Acceptor.cpp
    src/rrs/network/IOThread.cpp
    src/rrs/observability/Logger.cpp
    src/rrs/observability/MetricsRegistry.cpp
    src/rrs/observability/MetricsReporter.cpp
    src/rrs/protocol/BinaryProtocol.cpp
    src/rrs/runtime/SessionRegistry.cpp
    src/rrs/runtime/WorkerChannels.cpp
    src/rrs/runtime/WorkerRoomRegistry.cpp
    src/rrs/runtime/WorkerThread.cpp
    src/rrs/simulation/RoomVisibility.cpp
    src/rrs/simulation/spatial/FoodSpatialIndex.cpp
    src/rrs/simulation/spatial/PlayerBallSpatialIndex.cpp
    src/rrs/simulation/Room.cpp
    src/rrs/synchronization/SnapshotDeltaTracker.cpp
    src/rrs/synchronization/SnapshotUpdate.cpp
)

target_include_directories(rrs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rrs PRIVATE rrs_spatial spdlog::spdlog Threads::Threads)

if (MSVC)
    target_compile_options(rrs_spatial PRIVATE /W4)
    target_compile_options(rrs PRIVATE /W4)
else()
    target_compile_options(rrs_spatial PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(rrs PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if (BUILD_TESTING)
    add_executable(UniformGridTest
        tests/spatial/UniformGridTest.cpp
    )

    target_link_libraries(UniformGridTest PRIVATE rrs_spatial)

    if (MSVC)
        target_compile_options(UniformGridTest PRIVATE /W4)
    else()
        target_compile_options(UniformGridTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME UniformGridTest COMMAND UniformGridTest)

    add_executable(PlayerBallSpatialIndexTest
        tests/simulation/spatial/PlayerBallSpatialIndexTest.cpp
        src/rrs/simulation/spatial/PlayerBallSpatialIndex.cpp
    )

    target_link_libraries(PlayerBallSpatialIndexTest PRIVATE rrs_spatial)

    if (MSVC)
        target_compile_options(PlayerBallSpatialIndexTest PRIVATE /W4)
    else()
        target_compile_options(PlayerBallSpatialIndexTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME PlayerBallSpatialIndexTest COMMAND PlayerBallSpatialIndexTest)

    add_executable(RoomVisibilityTest
        tests/simulation/RoomVisibilityTest.cpp
        src/rrs/simulation/RoomVisibility.cpp
        src/rrs/simulation/spatial/PlayerBallSpatialIndex.cpp
    )

    target_link_libraries(RoomVisibilityTest PRIVATE rrs_spatial)

    if (MSVC)
        target_compile_options(RoomVisibilityTest PRIVATE /W4)
    else()
        target_compile_options(RoomVisibilityTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME RoomVisibilityTest COMMAND RoomVisibilityTest)

    add_executable(RoomFoodSynchronizationTest
        tests/simulation/RoomFoodSynchronizationTest.cpp
        src/rrs/simulation/Room.cpp
        src/rrs/simulation/RoomVisibility.cpp
        src/rrs/simulation/spatial/FoodSpatialIndex.cpp
        src/rrs/simulation/spatial/PlayerBallSpatialIndex.cpp
        src/rrs/synchronization/SnapshotDeltaTracker.cpp
        src/rrs/synchronization/SnapshotUpdate.cpp
    )

    target_link_libraries(RoomFoodSynchronizationTest PRIVATE rrs_spatial)

    if (MSVC)
        target_compile_options(RoomFoodSynchronizationTest PRIVATE /W4)
    else()
        target_compile_options(RoomFoodSynchronizationTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME RoomFoodSynchronizationTest COMMAND RoomFoodSynchronizationTest)

    add_executable(SnapshotDeltaTrackerTest
        tests/synchronization/SnapshotDeltaTrackerTest.cpp
        src/rrs/synchronization/SnapshotDeltaTracker.cpp
        src/rrs/synchronization/SnapshotUpdate.cpp
    )

    if (MSVC)
        target_compile_options(SnapshotDeltaTrackerTest PRIVATE /W4)
    else()
        target_compile_options(SnapshotDeltaTrackerTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_include_directories(SnapshotDeltaTrackerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME SnapshotDeltaTrackerTest COMMAND SnapshotDeltaTrackerTest)

    add_executable(BinaryProtocolTest
        tests/protocol/BinaryProtocolTest.cpp
        src/rrs/protocol/BinaryProtocol.cpp
    )

    target_include_directories(BinaryProtocolTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    if (MSVC)
        target_compile_options(BinaryProtocolTest PRIVATE /W4)
    else()
        target_compile_options(BinaryProtocolTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME BinaryProtocolTest COMMAND BinaryProtocolTest)
endif()
