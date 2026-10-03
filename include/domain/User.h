#pragma once
#include <string>

#include "utils/TimeUtil.h"

namespace wallet {

enum class Role { Customer, Merchant, Admin };
enum class AccountStatus { Active, Suspended, Locked };
enum class AccountTier { Basic, Premium };

const char* toString(Role r);
const char* toString(AccountStatus s);
const char* toString(AccountTier t);

// Stored secrets only; hashing itself lives in infrastructure (PasswordHasher).
class Credentials {
public:
    Credentials() = default;
    Credentials(std::string salt, std::string hash, int failedAttempts = 0)
        : salt_(std::move(salt)), hash_(std::move(hash)), failedAttempts_(failedAttempts) {}
    const std::string& salt() const noexcept { return salt_; }
    const std::string& hash() const noexcept { return hash_; }
    int failedAttempts() const noexcept { return failedAttempts_; }
    void recordFailure() { ++failedAttempts_; }
    void reset() { failedAttempts_ = 0; }
private:
    std::string salt_;
    std::string hash_;
    int failedAttempts_ = 0;
};

// Base class for everyone who can log in. Customer/Merchant/Admin are genuine "is-a User"
// specialisations: AuthService, AccessControl and the repositories treat them uniformly,
// while role-specific data lives in the subclasses.
class User {
public:
    User(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
         Credentials credentials, AccountStatus status = AccountStatus::Active, std::string walletId = "");
    virtual ~User() = default;

    virtual Role role() const noexcept = 0;
    virtual std::string describe() const = 0;  // polymorphic one-line summary

    const std::string& id() const noexcept { return id_; }
    const std::string& name() const noexcept { return name_; }
    const std::string& email() const noexcept { return email_; }
    const std::string& phone() const noexcept { return phone_; }
    const std::string& walletId() const noexcept { return walletId_; }
    Timestamp createdAt() const noexcept { return createdAt_; }
    AccountStatus status() const noexcept { return status_; }
    const Credentials& credentials() const noexcept { return credentials_; }

    void attachWallet(std::string walletId) { walletId_ = std::move(walletId); }
    void setStatus(AccountStatus s);  // admin action; re-activating also clears failed attempts
    // Returns true if this failure just locked the account.
    bool recordFailedLogin(int maxAttempts);
    void recordSuccessfulLogin() { credentials_.reset(); }
    bool canLogin() const noexcept { return status_ == AccountStatus::Active; }

private:
    std::string id_, name_, email_, phone_;
    Timestamp createdAt_;
    Credentials credentials_;
    AccountStatus status_;
    std::string walletId_;
};

class Customer : public User {
public:
    Customer(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
             Credentials credentials, AccountTier tier = AccountTier::Basic,
             AccountStatus status = AccountStatus::Active, std::string walletId = "");
    Role role() const noexcept override { return Role::Customer; }
    std::string describe() const override;
    AccountTier tier() const noexcept { return tier_; }
    void setTier(AccountTier t) { tier_ = t; }
private:
    AccountTier tier_;
};

class Merchant : public User {
public:
    Merchant(std::string id, std::string name, std::string email, std::string phone, Timestamp createdAt,
             Credentials credentials, std::string businessName, std::string category, std::string paymentId,
             AccountStatus status = AccountStatus::Active, std::string walletId = "");
    Role role() const noexcept override { return Role::Merchant; }
    std::string describe() const override;
    const std::string& businessName() const noexcept { return businessName_; }
    const std::string& category() const noexcept { return category_; }
    const std::string& paymentId() const noexcept { return paymentId_; }  // used in QR / pay-merchant flows
private:
    std::string businessName_, category_, paymentId_;
};

class Admin : public User {
public:
    using User::User;
    Role role() const noexcept override { return Role::Admin; }
    std::string describe() const override;
};

} // namespace wallet
