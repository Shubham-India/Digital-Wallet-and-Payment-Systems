#pragma once
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"

namespace wallet {

enum class WalletStatus { Active, Frozen, Closed };
const char* toString(WalletStatus s);

// Encapsulation: balance_ is private. The only way to change it is credit()/debit(),
// which enforce every rule (status, currency, sufficient funds). There is no setter.
class Wallet {
public:
    static Wallet open(std::string id, std::string ownerId, Currency currency = Currency::INR);
    // Rebuilds a persisted wallet. Used only by repositories.
    static Wallet restore(std::string id, std::string ownerId, Money balance, WalletStatus status);

    const std::string& id() const noexcept { return id_; }
    const std::string& ownerId() const noexcept { return ownerId_; }
    const Money& balance() const noexcept { return balance_; }
    WalletStatus status() const noexcept { return status_; }
    Currency currency() const noexcept { return balance_.currency(); }
    bool isActive() const noexcept { return status_ == WalletStatus::Active; }

    Result<void> credit(const Money& amount);
    Result<void> debit(const Money& amount);
    Result<void> freeze();
    Result<void> unfreeze();
    Result<void> close();  // only allowed when the balance is zero

private:
    Wallet(std::string id, std::string ownerId, Money balance, WalletStatus status)
        : id_(std::move(id)), ownerId_(std::move(ownerId)), balance_(balance), status_(status) {}
    Result<void> checkUsable(const Money& amount) const;

    std::string id_;
    std::string ownerId_;
    Money balance_;
    WalletStatus status_;
};

} // namespace wallet
