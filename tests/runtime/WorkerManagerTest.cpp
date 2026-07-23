#include "rrs/runtime/WorkerManager.h"
#include "rrs/simulation/RoomRules.h"

#include <cstdlib>
#include <iostream>
#include <span>
#include <type_traits>

namespace {

static_assert(!std::is_convertible_v<std::uint64_t, rrs::PlayerId>);
static_assert(!std::is_convertible_v<rrs::PlayerId, rrs::SessionId>);
static_assert(sizeof(rrs::PlayerId) == sizeof(std::uint64_t));

void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestCreateAndRouting()
{
    auto manager = rrs::WorkerManager{
        rrs::WorkerId{1},
        3,
    };
    const auto now = rrs::Room::Clock::now();
    const auto session_id = manager.Join(
        rrs::PlayerId{7},
        rrs::ConnectionHandle{rrs::IoThreadId{0}, rrs::ConnectionId{1}},
        now);

    Expect(rrs::GetSessionWorker(session_id, 3) == rrs::WorkerId{1}, "session routes to owner worker");
    Expect(session_id == rrs::SessionId{2}, "first session is one-based and worker-routed");
    Expect(manager.RoomFor(session_id).id() == rrs::RoomId{2}, "first room is one-based and worker-routed");
    Expect(manager.FindSession(session_id)->player_id == rrs::PlayerId{7}, "session stores player");

    const auto second_session_id = manager.Join(
        rrs::PlayerId{8},
        rrs::ConnectionHandle{rrs::IoThreadId{0}, rrs::ConnectionId{2}},
        now);
    Expect(second_session_id == rrs::SessionId{5}, "local session sequence preserves worker routing");
}

void TestReconnectReplacesConnection()
{
    auto manager = rrs::WorkerManager{
        rrs::WorkerId{0},
        2,
    };
    const auto old_connection = rrs::ConnectionHandle{rrs::IoThreadId{0}, rrs::ConnectionId{1}};
    const auto new_connection = rrs::ConnectionHandle{rrs::IoThreadId{1}, rrs::ConnectionId{2}};
    const auto session_id = manager.Join(rrs::PlayerId{1}, old_connection, rrs::Room::Clock::now());
    manager.FindSession(session_id)->state = rrs::SessionState::kActive;

    const auto replaced = manager.Bind(*manager.FindSession(session_id), new_connection);
    Expect(replaced == old_connection, "reconnect returns replaced connection");
    Expect(manager.FindSessionByConnection(old_connection) == nullptr, "old connection loses ownership");
    Expect(manager.FindSessionByConnection(new_connection)->id == session_id, "new connection owns session");
    manager.Disconnect(session_id, rrs::Room::Clock::now());
    Expect(!manager.FindSession(session_id)->connection, "disconnect keeps the active session");
}

void TestRemoveCleansIndexes()
{
    auto manager = rrs::WorkerManager{
        rrs::WorkerId{0},
        1,
    };
    const auto connection = rrs::ConnectionHandle{rrs::IoThreadId{0}, rrs::ConnectionId{1}};
    const auto session_id = manager.Join(rrs::PlayerId{3}, connection, rrs::Room::Clock::now());

    manager.Leave(session_id, rrs::Room::Clock::now());
    Expect(manager.FindSession(session_id) == nullptr, "leave removes id index");
    Expect(manager.FindSessionByPlayer(rrs::PlayerId{3}) == nullptr, "leave removes player index");
    Expect(manager.FindSessionByConnection(connection) == nullptr, "leave removes connection index");
}

void TestMatchEndRemovesRoomSessions()
{
    auto manager = rrs::WorkerManager{
        rrs::WorkerId{0},
        1,
    };
    const auto now = rrs::Room::Clock::now();
    const auto player_id = rrs::PlayerId{9};
    const auto session_id = manager.Join(
        player_id,
        rrs::ConnectionHandle{rrs::IoThreadId{0}, rrs::ConnectionId{1}},
        now);
    const auto first_tick_time = manager.RoomFor(session_id).next_tick_time();

    auto ticked = false;
    const auto match_tick_count =
        rrs::room_rules::kMatchDuration / rrs::room_rules::kTickInterval;
    for (auto tick = decltype(match_tick_count){0}; tick < match_tick_count; ++tick) {
        const auto ticked_rooms = manager.TickDueRooms(
            first_tick_time + rrs::room_rules::kMatchDuration,
            [](rrs::Room&,
               std::span<const rrs::Session>,
               const rrs::Room::TickResult&,
               rrs::Room::Clock::time_point) {});
        ticked = ticked || ticked_rooms;
        if (manager.FindSession(session_id) == nullptr) {
            break;
        }
    }

    Expect(ticked, "match-ending room ticks");
    Expect(manager.FindSession(session_id) == nullptr, "match end removes session index");
    Expect(manager.FindSessionByPlayer(player_id) == nullptr, "match end removes player index");
}

} // namespace

int main()
{
    TestCreateAndRouting();
    TestReconnectReplacesConnection();
    TestRemoveCleansIndexes();
    TestMatchEndRemovesRoomSessions();
    return 0;
}
