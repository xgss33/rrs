#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace rrs {

template <typename Tag>
class StrongId {
public:
    using ValueType = std::uint64_t;

    constexpr StrongId() = default;
    explicit constexpr StrongId(ValueType value) : value_(value) {}

    constexpr ValueType value() const { return value_; }
    constexpr bool is_valid() const { return value_ != 0; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    ValueType value_{0};
};

struct RoomIdTag;
struct PlayerIdTag;
struct SessionIdTag;
struct WorkerIdTag;
struct IoThreadIdTag;

using RoomId = StrongId<RoomIdTag>;
using PlayerId = StrongId<PlayerIdTag>;
using SessionId = StrongId<SessionIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using IoThreadId = StrongId<IoThreadIdTag>;
using TickSeq = std::uint64_t;
using Generation = std::uint64_t;

} // namespace rrs

namespace std {

template <typename Tag>
struct hash<rrs::StrongId<Tag>> {
    std::size_t operator()(rrs::StrongId<Tag> id) const noexcept
    {
        return std::hash<typename rrs::StrongId<Tag>::ValueType>{}(id.value());
    }
};

} // namespace std
