#include "domain/Wallet.h"

namespace wallet {

const char* toString(WalletStatus s) {
    switch (s) {
        case WalletStatus::Active: return "ACTIVE";
        case WalletStatus::Frozen: return "FROZEN";
        case WalletStatus::Closed: return "CLOSED";
    }
    return "?";
}

Wallet Wallet::open(std::string id, std::string ownerId, Currency currency) {
    return Wallet(std::move(id), std::move(ownerId), Money::fromMinor(0, currency), WalletStatus::Active);
}

Wallet Wallet::restore(std::string id, std::string ownerId, Money balance, WalletStatus status) {
    return Wallet(std::move(id), std::move(ownerId), balance, status);
}

Result<void> Wallet::checkUsable(const Money& amount) const {
    if (status_ == WalletStatus::Closed) return fail(ErrorCode::WalletClosed, "Wallet " + id_ + " is closed");
    if (status_ == WalletStatus::Frozen) return fail(ErrorCode::WalletFrozen, "Wallet " + id_ + " is frozen");
    if (amount.isZero()) return fail(ErrorCode::InvalidAmount, "Amount must be greater than zero");
    if (amount.currency() != balance_.currency())
        return fail(ErrorCode::CurrencyMismatch, "Wallet currency differs from amount currency");
    return {};
}

Result<void> Wallet::credit(const Money& amount) {
    if (auto r = checkUsable(amount); !r) return r;
    if (amount.minor() > Money::kMaxMinor - balance_.minor())
        return fail(ErrorCode::Overflow, "Balance would exceed the maximum supported value");
    balance_ = balance_ + amount;
    return {};
}

Result<void> Wallet::debit(const Money& amount) {
    if (auto r = checkUsable(amount); !r) return r;
    if (amount > balance_)
        return fail(ErrorCode::InsufficientFunds,
                    "Insufficient funds: balance " + balance_.toString() + ", requested " + amount.toString());
    balance_ = balance_ - amount;
    return {};
}

Result<void> Wallet::freeze() {
    if (status_ == WalletStatus::Closed) return fail(ErrorCode::WalletClosed, "Cannot freeze a closed wallet");
    status_ = WalletStatus::Frozen;
    return {};
}

Result<void> Wallet::unfreeze() {
    if (status_ == WalletStatus::Closed) return fail(ErrorCode::WalletClosed, "Cannot unfreeze a closed wallet");
    status_ = WalletStatus::Active;
    return {};
}

Result<void> Wallet::close() {
    if (!balance_.isZero())
        return fail(ErrorCode::ValidationFailed, "Wallet must have a zero balance before closing");
    status_ = WalletStatus::Closed;
    return {};
}

} // namespace wallet
