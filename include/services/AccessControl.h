#pragma once
#include <string>
#include <unordered_map>

#include "domain/Result.h"
#include "domain/User.h"
#include "infrastructure/IdGenerator.h"

namespace wallet {

struct Session {
    std::string token;
    std::string userId;
    Role role = Role::Customer;
};

enum class Permission {
    ViewOwnWallet, AddMoney, Withdraw, Transfer, PayMerchant, ViewOwnTransactions,
    RequestRefund, IssueRefund, ReceivePayments,
    ViewAllUsers, ManageUsers, FreezeWallets, ViewAllTransactions, ReviewFraud,
    ViewAudit, ConfigureLimits, ViewReports
};
const char* toString(Permission p);

// Tracks who is logged in. Tokens are opaque, unguessable-enough for a simulation.
class SessionManager {
public:
    explicit SessionManager(IdGenerator& ids) : ids_(ids) {}
    Session create(const User& user);
    bool valid(const Session& s) const;
    void destroy(const Session& s) { active_.erase(s.token); }
    std::size_t activeCount() const noexcept { return active_.size(); }
private:
    IdGenerator& ids_;
    std::unordered_map<std::string, Session> active_;
};

// Central authorization: the ONLY place that maps roles to permissions.
// Services call authorize() at their entry points instead of scattering role checks.
class AccessControl {
public:
    explicit AccessControl(const SessionManager& sessions) : sessions_(sessions) {}
    static bool roleHas(Role role, Permission p);
    Result<void> authorize(const Session& s, Permission p) const;
private:
    const SessionManager& sessions_;
};

} // namespace wallet
