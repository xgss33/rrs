cmake_minimum_required(VERSION 3.20)

project(rrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(third_party/spdlog)

add_executable(rrs
    src/main.cpp
    src/config/ServerConfig.cpp
    src/game/FoodSpatialIndex.cpp
    src/game/Room.cpp
    src/log/Logger.cpp
    src/metrics/MetricsRegistry.cpp
    src/metrics/MetricsReporter.cpp
    src/runtime/WorkerToIoMessage.cpp
    src/net/Acceptor.cpp
    src/net/BinaryProtocol.cpp
    src/net/IoSessionRouter.cpp
    src/net/IOThread.cpp
    src/runtime/SessionRegistry.cpp
    src/runtime/WorkerRoomRegistry.cpp
    src/runtime/WorkerThread.cpp
    src/base/Threading.cpp
)

target_include_directories(rrs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rrs PRIVATE spdlog::spdlog Threads::Threads)

if (MSVC)
    target_compile_options(rrs PRIVATE /W4)
else()
    target_compile_options(rrs PRIVATE -Wall -Wextra -Wpedantic)
endif()
