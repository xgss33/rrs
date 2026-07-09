cmake_minimum_required(VERSION 3.20)

project(RealtimeRoomServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)
FetchContent_MakeAvailable(spdlog)

add_executable(realtime_room_server
    src/app/main.cpp
    src/app/AppConfig.cpp
    src/game/Room.cpp
    src/log/Logger.cpp
    src/metrics/MetricsRegistry.cpp
    src/metrics/MetricsReporter.cpp
    src/runtime/IoToWorkerMessage.cpp
    src/runtime/WorkerToIoMessage.cpp
    src/net/Acceptor.cpp
    src/net/BinaryProtocol.cpp
    src/net/IoSessionRouter.cpp
    src/net/IOThread.cpp
    src/runtime/SessionRegistry.cpp
    src/runtime/WorkerRoomRegistry.cpp
    src/runtime/WorkerThread.cpp
)

target_include_directories(realtime_room_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(realtime_room_server PRIVATE spdlog::spdlog Threads::Threads)

if (MSVC)
    target_compile_options(realtime_room_server PRIVATE /W4)
else()
    target_compile_options(realtime_room_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
