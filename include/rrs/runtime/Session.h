#pragma once

#include "rrs/base/Types.h"

namespace rrs {

struct Session {
    SessionId session_id;
    Generation generation{1};
    PlayerId player_id;
    IoThreadId io_thread_id;
    WorkerId worker_id;
};

} // namespace rrs
