#include "services/AuthService.h"

#include <algorithm>
#include <cctype>

namespace wallet {

namespace {
bool validEmail(const std::string& e) {
    auto at = e.find('@');
    if (at == std::string::npos || at == 0 || e.size() > 120) return false;
    auto dot = e.find('.', at);
    if (dot == std::string::npos || dot == at + 1 || dot + 1 >= e.size()) return false;
    return std::none_of(e.begin(), e.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}
bool validPhone(const std::string& p) {
    return p.empty() || (p.size() >= 7 && p.size() <= 15 &&
                         std::all_of(p.begin(), p.end(), [](unsigned char c) { return std::isdigit(c) != 0; }));
}
bool strongEnough(const std::string& pw) {
    return pw.size() >= 8 && pw.size() <= 128 &&
           std::any_of(pw.begin(), pw.end(), [](unsigned char c) { return std::isalpha(c) != 0; }) &&
           std::any_of(pw.begin(), pw.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
}  // namespace

Result<void> AuthService::validateCommon(const std::string& name, const std::string& email, const std::string& phone,
                                         const std::string& password) const {
    if (name.empty() || name.size() > 80) return fail(ErrorCode::ValidationFailed, "Name must be 1-80 characters");
    if (!validEmail(email)) return fail(ErrorCode::ValidationFailed, "Invalid email address");
    if (!validPhone(phone)) return fail(ErrorCode::ValidationFailed, "Phone must be 7-15 digits");
    if (!strongEnough(password)) return fail(ErrorCode::ValidationFailed, "Password needs 8+ characters with letters and digits");
    return {};
}

Credentials AuthService::makeCredentials(const std::string& password) const {
    std::string salt = hasher_.newSalt();
    return Credentials(salt, hasher_.hash(password, salt));
}

Result<std::string> AuthService::finishRegistration(std::shared_ptr<User> user, bool withWallet) {
    if (withWallet) {
        Wallet w = Wallet::open(ids_.next("WAL", 4), user->id());
        user->attachWallet(w.id());
        db_.wallets().save(w);
    }
    if (auto r = db_.users().add(user); !r) {
        audit_.record(user->id(), "REGISTER", user->email(), "FAILURE", {{"reason", r.error().message}});
        return fail(r.error());
    }
    audit_.record(user->id(), "REGISTER", user->id(), "SUCCESS", {{"role", toString(user->role())}});
    if (auto c = db_.commit(); !c) return fail(c.error());
    log_.info("auth", "registered " + std::string(toString(user->role())) + " " + user->id());
    return user->id();
}

Result<std::string> AuthService::registerCustomer(const std::string& name, const std::string& email, const std::string& phone,
                                                  const std::string& password, AccountTier tier) {
    if (auto v = validateCommon(name, email, phone, password); !v) return fail(v.error());
    if (db_.users().findByEmail(email)) return fail(ErrorCode::DuplicateUser, "Email is already registered");
    auto user = std::make_shared<Customer>(ids_.next("USR", 4), name, email, phone, clock_.now(), makeCredentials(password), tier);
    return finishRegistration(user, true);
}

Result<std::string> AuthService::registerMerchant(const std::string& name, const std::string& email, const std::string& phone,
                                                  const std::string& password, const std::string& businessName,
                                                  const std::string& category) {
    if (auto v = validateCommon(name, email, phone, password); !v) return fail(v.error());
    if (businessName.empty() || businessName.size() > 80) return fail(ErrorCode::ValidationFailed, "Business name must be 1-80 characters");
    if (category.empty() || category.size() > 40) return fail(ErrorCode::ValidationFailed, "Category must be 1-40 characters");
    if (db_.users().findByEmail(email)) return fail(ErrorCode::DuplicateUser, "Email is already registered");
    auto user = std::make_shared<Merchant>(ids_.next("USR", 4), name, email, phone, clock_.now(), makeCredentials(password),
                                           businessName, category, ids_.next("MER", 4));
    return finishRegistration(user, true);
}

Result<std::string> AuthService::createAdmin(const std::string& name, const std::string& email, const std::string& password,
                                             const Session* creator) {
    if (creator) {  // an admin creating another admin must be authorised; bootstrap passes nullptr
        if (!sessions_.valid(*creator) || !AccessControl::roleHas(creator->role, Permission::ManageUsers))
            return fail(ErrorCode::Unauthorized, "Only an admin can create admins");
    }
    if (auto v = validateCommon(name, email, "", password); !v) return fail(v.error());
    if (db_.users().findByEmail(email)) return fail(ErrorCode::DuplicateUser, "Email is already registered");
    auto user = std::make_shared<Admin>(ids_.next("USR", 4), name, email, "", clock_.now(), makeCredentials(password));
    return finishRegistration(user, false);
}

Result<Session> AuthService::login(const std::string& email, const std::string& password) {
    auto user = db_.users().findByEmail(email);
    if (!user) {
        // Same error as a wrong password so the response does not reveal which emails exist.
        audit_.record("", "LOGIN", email, "FAILURE", {{"reason", "unknown account"}});
        return fail(ErrorCode::InvalidCredentials, "Invalid email or password");
    }
    if (user->status() == AccountStatus::Locked) {
        audit_.record(user->id(), "LOGIN", user->id(), "DENIED", {{"reason", "account locked"}});
        return fail(ErrorCode::AccountLocked, "Account is locked. Contact an administrator.");
    }
    if (user->status() == AccountStatus::Suspended) {
        audit_.record(user->id(), "LOGIN", user->id(), "DENIED", {{"reason", "account suspended"}});
        return fail(ErrorCode::AccountInactive, "Account is suspended");
    }
    if (!hasher_.verify(password, user->credentials().salt(), user->credentials().hash())) {
        bool nowLocked = user->recordFailedLogin(config_.maxFailedLogins);
        db_.users().save(user);
        audit_.record(user->id(), "LOGIN", user->id(), "FAILURE",
                      {{"reason", "bad password"}, {"attempts", std::to_string(user->credentials().failedAttempts())}});
        if (nowLocked) audit_.record(user->id(), "ACCOUNT_LOCKED", user->id(), "SUCCESS");
        if (auto c = db_.commit(); !c) log_.error("auth", c.error().message);
        return nowLocked ? fail(ErrorCode::AccountLocked, "Too many failed attempts: account locked")
                         : fail(ErrorCode::InvalidCredentials, "Invalid email or password");
    }
    user->recordSuccessfulLogin();
    db_.users().save(user);
    Session s = sessions_.create(*user);
    audit_.record(user->id(), "LOGIN", user->id(), "SUCCESS");
    if (auto c = db_.commit(); !c) log_.error("auth", c.error().message);
    log_.info("auth", "login " + user->id());
    return s;
}

void AuthService::logout(const Session& session) {
    if (!sessions_.valid(session)) return;
    sessions_.destroy(session);
    audit_.record(session.userId, "LOGOUT", session.userId, "SUCCESS");
    if (auto c = db_.commit(); !c) log_.error("auth", c.error().message);
}

} // namespace wallet
