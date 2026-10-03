#include "policies/LimitPolicy.h"

namespace wallet {

Result<void> TierLimitPolicy::check(const LimitContext& ctx) const {
    if (ctx.amount > settings_.perTransaction)
        return fail(ErrorCode::LimitExceeded, "Amount " + ctx.amount.toString() + " exceeds the per-transaction limit of " +
                                                  settings_.perTransaction.toString() + " (" + name() + ")");
    if (ctx.countToday + 1 > settings_.dailyCount)
        return fail(ErrorCode::LimitExceeded, "Daily transaction count limit of " + std::to_string(settings_.dailyCount) + " reached");
    // Compare via minor units so the sum cannot throw on overflow near the cap.
    if (ctx.spentToday.minor() + ctx.amount.minor() > settings_.dailyAmount.minor())
        return fail(ErrorCode::LimitExceeded, "Daily limit of " + settings_.dailyAmount.toString() + " would be exceeded (already used " +
                                                  ctx.spentToday.toString() + ")");
    return {};
}

LimitTier LimitPolicyFactory::tierFor(const User& user) {
    switch (user.role()) {
        case Role::Merchant: return LimitTier::Merchant;
        case Role::Customer: {
            const auto* c = dynamic_cast<const Customer*>(&user);
            return (c && c->tier() == AccountTier::Premium) ? LimitTier::Premium : LimitTier::Basic;
        }
        case Role::Admin: return LimitTier::Premium;
    }
    return LimitTier::Basic;
}

std::unique_ptr<LimitPolicy> LimitPolicyFactory::forTier(LimitTier tier, const AppConfig& config) {
    return std::make_unique<TierLimitPolicy>(tier, config.limits.at(tier));
}

} // namespace wallet
