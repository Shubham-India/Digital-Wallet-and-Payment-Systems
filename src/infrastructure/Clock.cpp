#include "infrastructure/Clock.h"

#include <chrono>

namespace wallet {

Timestamp SystemClock::now() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace wallet
