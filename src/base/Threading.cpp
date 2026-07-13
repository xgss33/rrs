#include "rrs/base/Threading.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <pthread.h>

namespace rrs {

void SetCurrentThreadName(std::string_view name)
{
    auto buffer = std::array<char, 16>{};
    const auto length = std::min(name.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), name.data(), length);
    static_cast<void>(pthread_setname_np(pthread_self(), buffer.data()));
}

} // namespace rrs
