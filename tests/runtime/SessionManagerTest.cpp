#include "rrs/runtime/SessionManager.h"

#include <cstdlib>
#include <iostream>

namespace {

void Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

rrs::ConnectionHandle MakeConnection(std::uint64_t io_id, std::uint64_t connection_id)
{
    return rrs::ConnectionHandle{
        .io_thread_id = rrs::IoThreadId{io_id},
        .connection_id = rrs::ConnectionId{connection_id},
    };
}

void TestCreateAndRouting()
{
    rrs::SessionManager sessions{rrs::WorkerId{1}, 3};
    const auto session_id = sessions.Create(
        rrs::PlayerId{7},
        rrs::RoomId{9},
        MakeConnection(0, 1));

    Expect(rrs::GetSessionWorker(session_id, 3) == rrs::WorkerId{1}, "session routes to owner worker");
    Expect(sessions.Find(session_id)->player_id == rrs::PlayerId{7}, "session stores player");

    const auto second_session_id = sessions.Create(
        rrs::PlayerId{8},
        rrs::RoomId{9},
        MakeConnection(0, 2));
    Expect(second_session_id == rrs::SessionId{5}, "local session sequence preserves worker routing");
}

void TestReconnectReplacesConnection()
{
    rrs::SessionManager sessions{rrs::WorkerId{0}, 2};
    const auto old_connection = MakeConnection(0, 1);
    const auto new_connection = MakeConnection(1, 2);
    const auto session_id = sessions.Create(rrs::PlayerId{1}, rrs::RoomId{1}, old_connection);
    sessions.Activate(session_id);

    const auto replaced = sessions.Bind(session_id, new_connection);
    Expect(replaced == old_connection, "reconnect returns replaced connection");
    Expect(!sessions.FindIdByConnection(old_connection), "old connection loses ownership");
    Expect(sessions.FindIdByConnection(new_connection) == session_id, "new connection owns session");
    sessions.Unbind(new_connection);
    Expect(!sessions.Find(session_id)->connection, "disconnect keeps the session without a connection");
}

void TestRemoveCleansIndexes()
{
    rrs::SessionManager sessions{rrs::WorkerId{0}, 1};
    const auto connection = MakeConnection(0, 1);
    const auto session_id = sessions.Create(rrs::PlayerId{3}, rrs::RoomId{4}, connection);

    const auto removed = sessions.Remove(session_id);
    Expect(removed.player_id == rrs::PlayerId{3}, "remove returns session");
    Expect(sessions.Find(session_id) == nullptr, "remove id index");
    Expect(sessions.FindByPlayer(rrs::PlayerId{3}) == nullptr, "remove player index");
    Expect(!sessions.FindIdByConnection(connection), "remove connection index");
}

} // namespace

int main()
{
    TestCreateAndRouting();
    TestReconnectReplacesConnection();
    TestRemoveCleansIndexes();
    return 0;
}
