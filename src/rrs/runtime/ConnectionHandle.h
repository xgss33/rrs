#pragma once

#include "rrs/core/Identifiers.h"

#include <cstddef>
#include <functional>

namespace rrs {

struct ConnectionHandle {
    IoThreadId io_thread_id;
    ConnectionId connection_id;

    friend bool operator==(const ConnectionHandle&, const ConnectionHandle&) = default;
};

} // namespace rrs

namespace std {

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
