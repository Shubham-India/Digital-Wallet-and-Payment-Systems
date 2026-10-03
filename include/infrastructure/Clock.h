#pragma once
#include "utils/TimeUtil.h"

namespace wallet {

// Injected everywhere time matters (limits, velocity rules) so tests control time.
class IClock {
public:
    virtual ~IClock() = default;
    virtual Timestamp now() const = 0;
};

class SystemClock : public IClock {
public:
    Timestamp now() const override;
};

class ManualClock : public IClock {
public:
    explicit ManualClock(Timestamp start = 1700000000) : t_(start) {}
    Timestamp now() const override { return t_; }
    void set(Timestamp t) { t_ = t; }
    void advance(Timestamp seconds) { t_ += seconds; }
private:
    Timestamp t_;
};

} // namespace wallet
