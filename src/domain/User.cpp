#include "domain/User.h"

namespace wallet {

const char* toString(Role r) {
    switch (r) {
        case Role::Customer: return "CUSTOMER";
        case Role::Merchant: return "MERCHANT";
        case Role::Admin: return "ADMIN";
    }
    return "?";
}

const char* toString(AccountStatus s) {
    switch (s) {
        case AccountStatus::Active: return "ACTIVE";
        case AccountStatus::Suspended: return "SUSPENDED";
        case AccountStatus::Locked: return "LOCKED";
    }
    return "?";
}

const char* toString(AccountTier t) { return t == AccountTier::Basic ? "BASIC" : "PREMIUM"; }

User::User(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
           Credentials credentials, AccountStatus status, std::string walletId)
    : id_(std::move(id)), name_(std::move(name)), email_(std::move(email)), phone_(std::move(phone)),
      createdAt_(createdAt), credentials_(std::move(credentials)), status_(status), walletId_(std::move(walletId)) {}

void User::setStatus(AccountStatus s) {
    status_ = s;
    if (s == AccountStatus::Active) credentials_.reset();
}

bool User::recordFailedLogin(int maxAttempts) {
    credentials_.recordFailure();
    if (credentials_.failedAttempts() >= maxAttempts && status_ == AccountStatus::Active) {
        status_ = AccountStatus::Locked;
        return true;
    }
    return false;
}

Customer::Customer(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
                   Credentials credentials, AccountTier tier, AccountStatus status, std::string walletId)
    : User(std::move(id), std::move(name), std::move(email), std::move(phone), createdAt, std::move(credentials),
           status, std::move(walletId)),
      tier_(tier) {}

std::string Customer::describe() const {
    return "Customer " + name() + " <" + email() + "> tier=" + toString(tier_);
}

Merchant::Merchant(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
                   Credentials credentials, std::string businessName, std::string category, std::string paymentId,
                   AccountStatus status, std::string walletId)
    : User(std::move(id), std::move(name), std::move(email), std::move(phone), createdAt, std::move(credentials),
           status, std::move(walletId)),
      businessName_(std::move(businessName)), category_(std::move(category)), paymentId_(std::move(paymentId)) {}

std::string Merchant::describe() const {
    return "Merchant " + businessName_ + " [" + category_ + "] payId=" + paymentId_;
}

std::string Admin::describe() const { return "Admin " + name() + " <" + email() + ">"; }

} // namespace wallet
