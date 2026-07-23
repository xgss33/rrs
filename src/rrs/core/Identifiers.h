#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace rrs {

template <typename Tag>
class StrongId {
public:
    constexpr StrongId() = default;
    explicit constexpr StrongId(std::uint64_t value) : value_(value) {}

    constexpr std::uint64_t value() const { return value_; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    std::uint64_t value_{0};
};

struct RoomIdTag;
struct PlayerIdTag;
struct SessionIdTag;
struct ConnectionIdTag;
struct WorkerIdTag;
struct IoThreadIdTag;

using RoomId = StrongId<RoomIdTag>;
using PlayerId = StrongId<PlayerIdTag>;
using SessionId = StrongId<SessionIdTag>;
using ConnectionId = StrongId<ConnectionIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using IoThreadId = StrongId<IoThreadIdTag>;

struct ConnectionHandle {
    IoThreadId io_thread_id;
    ConnectionId connection_id;

    friend bool operator==(const ConnectionHandle&, const ConnectionHandle&) = default;
};

} // namespace rrs

namespace std {

template <typename Tag>
struct hash<rrs::StrongId<Tag>> {
    std::size_t operator()(rrs::StrongId<Tag> id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value());
    }
};

template <>
struct hash<rrs::ConnectionHandle> {
    std::size_t operator()(const rrs::ConnectionHandle& connection) const noexcept
    {
        const auto io_hash = std::hash<rrs::IoThreadId>{}(connection.io_thread_id);
        const auto connection_hash = std::hash<rrs::ConnectionId>{}(connection.connection_id);
        return io_hash ^ (connection_hash + 0x9e3779b9U + (io_hash << 6U) + (io_hash >> 2U));
    }
};

} // namespace std
