#include "services/AccessControl.h"

#include <random>
#include <set>

namespace wallet {

const char* toString(Permission p) {
    switch (p) {
        case Permission::ViewOwnWallet: return "ViewOwnWallet";
        case Permission::AddMoney: return "AddMoney";
        case Permission::Withdraw: return "Withdraw";
        case Permission::Transfer: return "Transfer";
        case Permission::PayMerchant: return "PayMerchant";
        case Permission::ViewOwnTransactions: return "ViewOwnTransactions";
        case Permission::RequestRefund: return "RequestRefund";
        case Permission::IssueRefund: return "IssueRefund";
        case Permission::ReceivePayments: return "ReceivePayments";
        case Permission::ViewAllUsers: return "ViewAllUsers";
        case Permission::ManageUsers: return "ManageUsers";
        case Permission::FreezeWallets: return "FreezeWallets";
        case Permission::ViewAllTransactions: return "ViewAllTransactions";
        case Permission::ReviewFraud: return "ReviewFraud";
        case Permission::ViewAudit: return "ViewAudit";
        case Permission::ConfigureLimits: return "ConfigureLimits";
        case Permission::ViewReports: return "ViewReports";
    }
    return "?";
}

Session SessionManager::create(const User& user) {
    std::random_device rd;
    Session s;
    s.token = ids_.next("SES", 4) + "-" + std::to_string(rd());
    s.userId = user.id();
    s.role = user.role();
    active_[s.token] = s;
    return s;
}

bool SessionManager::valid(const Session& s) const {
    auto it = active_.find(s.token);
    return it != active_.end() && it->second.userId == s.userId && it->second.role == s.role;
}

bool AccessControl::roleHas(Role role, Permission p) {
    static const std::set<Permission> customer = {
        Permission::ViewOwnWallet, Permission::AddMoney, Permission::Withdraw, Permission::Transfer,
        Permission::PayMerchant, Permission::ViewOwnTransactions, Permission::RequestRefund};
    static const std::set<Permission> merchant = {
        Permission::ViewOwnWallet, Permission::ReceivePayments, Permission::ViewOwnTransactions,
        Permission::IssueRefund, Permission::Withdraw};
    static const std::set<Permission> admin = {
        Permission::ViewAllUsers, Permission::ManageUsers, Permission::FreezeWallets,
        Permission::ViewAllTransactions, Permission::ReviewFraud, Permission::ViewAudit,
        Permission::ConfigureLimits, Permission::ViewReports, Permission::IssueRefund};
    switch (role) {
        case Role::Customer: return customer.count(p) > 0;
        case Role::Merchant: return merchant.count(p) > 0;
        case Role::Admin: return admin.count(p) > 0;
    }
    return false;
}

Result<void> AccessControl::authorize(const Session& s, Permission p) const {
    if (!sessions_.valid(s)) return fail(ErrorCode::NotAuthenticated, "Not logged in or session expired");
    if (!roleHas(s.role, p))
        return fail(ErrorCode::Unauthorized, std::string("Role ") + toString(s.role) + " is not allowed to " + toString(p));
    return {};
}

} // namespace wallet
