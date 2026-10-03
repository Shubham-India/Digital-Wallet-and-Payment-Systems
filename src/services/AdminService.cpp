#include "services/AdminService.h"

namespace wallet {

Result<std::shared_ptr<User>> AdminService::target(const std::string& userId) const {
    auto u = db_.users().findById(userId);
    if (!u) return fail(ErrorCode::UserNotFound, "No user with id " + userId);
    return u;
}

Result<std::vector<std::shared_ptr<User>>> AdminService::listUsers(const Session& admin) const {
    if (auto a = access_.authorize(admin, Permission::ViewAllUsers); !a) return fail(a.error());
    return db_.users().all();
}

Result<std::shared_ptr<User>> AdminService::viewUser(const Session& admin, const std::string& userId) const {
    if (auto a = access_.authorize(admin, Permission::ViewAllUsers); !a) return fail(a.error());
    return target(userId);
}

Result<Wallet> AdminService::viewWallet(const Session& admin, const std::string& userId) const {
    if (auto a = access_.authorize(admin, Permission::ViewAllUsers); !a) return fail(a.error());
    auto u = target(userId);
    if (!u) return fail(u.error());
    auto w = db_.wallets().find(u.value()->walletId());
    if (!w) return fail(ErrorCode::WalletNotFound, "User has no wallet");
    return *w;
}

Result<void> AdminService::freezeWallet(const Session& admin, const std::string& userId) {
    if (auto a = access_.authorize(admin, Permission::FreezeWallets); !a) return a;
    auto u = target(userId);
    if (!u) return fail(u.error());
    auto w = db_.wallets().find(u.value()->walletId());
    if (!w) return fail(ErrorCode::WalletNotFound, "User has no wallet");
    if (auto r = w->freeze(); !r) return r;
    db_.wallets().save(*w);
    audit_.record(admin.userId, "FREEZE_WALLET", w->id(), "SUCCESS", {{"user", userId}});
    notifier_.toUser(userId, "Wallet frozen", "Your wallet has been frozen by an administrator.");
    if (auto c = db_.commit(); !c) return fail(c.error());
    notifier_.flush();
    return {};
}

Result<void> AdminService::unfreezeWallet(const Session& admin, const std::string& userId) {
    if (auto a = access_.authorize(admin, Permission::FreezeWallets); !a) return a;
    auto u = target(userId);
    if (!u) return fail(u.error());
    auto w = db_.wallets().find(u.value()->walletId());
    if (!w) return fail(ErrorCode::WalletNotFound, "User has no wallet");
    if (auto r = w->unfreeze(); !r) return r;
    db_.wallets().save(*w);
    audit_.record(admin.userId, "UNFREEZE_WALLET", w->id(), "SUCCESS", {{"user", userId}});
    notifier_.toUser(userId, "Wallet unfrozen", "Your wallet is active again.");
    if (auto c = db_.commit(); !c) return fail(c.error());
    notifier_.flush();
    return {};
}

Result<void> AdminService::setUserStatus(const Session& admin, const std::string& userId, AccountStatus status) {
    if (auto a = access_.authorize(admin, Permission::ManageUsers); !a) return a;
    auto u = target(userId);
    if (!u) return fail(u.error());
    if (u.value()->id() == admin.userId && status != AccountStatus::Active)
        return fail(ErrorCode::ValidationFailed, "You cannot suspend or lock your own account");
    u.value()->setStatus(status);
    db_.users().save(u.value());
    audit_.record(admin.userId, "SET_USER_STATUS", userId, "SUCCESS", {{"status", toString(status)}});
    return db_.commit();
}

Result<std::vector<AuditEvent>> AdminService::auditLog(const Session& admin, std::size_t lastN) const {
    if (auto a = access_.authorize(admin, Permission::ViewAudit); !a) return fail(a.error());
    auto events = db_.audit().all();
    if (lastN > 0 && events.size() > lastN) events.erase(events.begin(), events.end() - static_cast<std::ptrdiff_t>(lastN));
    return events;
}

Result<std::vector<ReviewItem>> AdminService::reviewQueue(const Session& admin) const {
    if (auto a = access_.authorize(admin, Permission::ReviewFraud); !a) return fail(a.error());
    return queue_.pending();
}

Result<void> AdminService::configureLimits(const Session& admin, LimitTier tier, const LimitSettings& settings) {
    if (auto a = access_.authorize(admin, Permission::ConfigureLimits); !a) return a;
    if (settings.perTransaction.isZero() || settings.dailyAmount < settings.perTransaction || settings.dailyCount <= 0)
        return fail(ErrorCode::ValidationFailed, "Limits must be positive and daily amount >= per-transaction amount");
    config_.limits[tier] = settings;
    audit_.record(admin.userId, "CONFIGURE_LIMITS", toString(tier), "SUCCESS",
                  {{"perTransaction", settings.perTransaction.toString()}, {"daily", settings.dailyAmount.toString()},
                   {"count", std::to_string(settings.dailyCount)}});
    return db_.commit();
}

} // namespace wallet
