cmake_minimum_required(VERSION 3.20)

project(rrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(third_party/spdlog)

add_library(rrs_spatial STATIC
    src/rrs/spatial/UniformGrid.cpp
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
    src/rrs/simulation/spatial/FoodSpatialIndex.cpp
    src/rrs/simulation/Room.cpp
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
endif()
