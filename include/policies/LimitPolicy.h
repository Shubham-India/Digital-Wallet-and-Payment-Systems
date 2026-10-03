#pragma once
#include <memory>
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"
#include "domain/User.h"
#include "infrastructure/Config.h"

namespace wallet {

struct LimitContext {
    Money amount;       // the amount being attempted
    Money spentToday;   // sum of successful outgoing payments so far today (UTC day)
    int countToday = 0; // number of successful outgoing payments so far today
};

// Strategy interface: how an account is limited. Services depend on this, not on a tier.
class LimitPolicy {
public:
    virtual ~LimitPolicy() = default;
    virtual Result<void> check(const LimitContext& ctx) const = 0;
    virtual std::string name() const = 0;
};

// One concrete policy parameterised by data (per-tier numbers come from AppConfig).
// A subclass per tier would only differ in constants, so inheritance would add nothing.
class TierLimitPolicy : public LimitPolicy {
public:
    TierLimitPolicy(LimitTier tier, LimitSettings settings) : tier_(tier), settings_(settings) {}
    Result<void> check(const LimitContext& ctx) const override;
    std::string name() const override { return std::string(toString(tier_)) + " limits"; }
private:
    LimitTier tier_;
    LimitSettings settings_;
};

class LimitPolicyFactory {
public:
    static LimitTier tierFor(const User& user);
    static std::unique_ptr<LimitPolicy> forTier(LimitTier tier, const AppConfig& config);
};

} // namespace wallet
