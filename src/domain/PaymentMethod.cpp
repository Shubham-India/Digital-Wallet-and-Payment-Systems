#include "domain/PaymentMethod.h"

#include <algorithm>
#include <cctype>

namespace wallet {

namespace {
const Money kSimulatedFunding = Money::fromMinor(100000000);  // INR 10,00,000.00 of pretend funds
const Money kCashDepositCap = Money::fromMinor(5000000);       // INR 50,000.00 per cash deposit

std::string maskTail(const std::string& s) {
    if (s.size() <= 4) return std::string(s.size(), '*');
    return std::string(s.size() - 4, '*') + s.substr(s.size() - 4);
}

bool digitsOnly(const std::string& s, std::size_t minLen, std::size_t maxLen) {
    return s.size() >= minLen && s.size() <= maxLen &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

Result<void> chargeAgainst(Money& available, const Money& amount, const char* what) {
    if (amount > available) return fail(ErrorCode::InvalidPaymentMethod, std::string(what) + " has insufficient funds");
    available = available - amount;
    return {};
}
}  // namespace

BankAccount::BankAccount(std::string accountNumber, Money simulatedBalance)
    : accountNumber_(std::move(accountNumber)), balance_(simulatedBalance) {}
std::string BankAccount::describe() const { return "Bank a/c " + maskTail(accountNumber_); }
Result<void> BankAccount::authorize(const Money& amount) { return chargeAgainst(balance_, amount, "Bank account"); }

DebitCard::DebitCard(std::string cardNumber, Money simulatedBalance)
    : cardNumber_(std::move(cardNumber)), balance_(simulatedBalance) {}
std::string DebitCard::describe() const { return "Debit card " + maskTail(cardNumber_); }
Result<void> DebitCard::authorize(const Money& amount) { return chargeAgainst(balance_, amount, "Debit card"); }

CreditCard::CreditCard(std::string cardNumber, Money creditLimit)
    : cardNumber_(std::move(cardNumber)), available_(creditLimit) {}
std::string CreditCard::describe() const { return "Credit card " + maskTail(cardNumber_); }
Result<void> CreditCard::authorize(const Money& amount) { return chargeAgainst(available_, amount, "Credit card"); }

Result<void> CashDeposit::authorize(const Money& amount) {
    if (amount > kCashDepositCap) return fail(ErrorCode::InvalidPaymentMethod, "Cash deposits are capped at " + kCashDepositCap.toString());
    return {};
}

Result<std::unique_ptr<PaymentMethod>> PaymentMethodFactory::create(const std::string& kind, const std::string& reference) {
    std::unique_ptr<PaymentMethod> method;
    if (kind == "BANK_ACCOUNT") {
        if (!digitsOnly(reference, 6, 18)) return fail(ErrorCode::InvalidPaymentMethod, "Account number must be 6-18 digits");
        method = std::make_unique<BankAccount>(reference, kSimulatedFunding);
    } else if (kind == "DEBIT_CARD") {
        if (!digitsOnly(reference, 12, 19)) return fail(ErrorCode::InvalidPaymentMethod, "Card number must be 12-19 digits");
        method = std::make_unique<DebitCard>(reference, kSimulatedFunding);
    } else if (kind == "CREDIT_CARD") {
        if (!digitsOnly(reference, 12, 19)) return fail(ErrorCode::InvalidPaymentMethod, "Card number must be 12-19 digits");
        method = std::make_unique<CreditCard>(reference, kSimulatedFunding);
    } else if (kind == "CASH_DEPOSIT") {
        if (reference.empty()) return fail(ErrorCode::InvalidPaymentMethod, "Agent code is required");
        method = std::make_unique<CashDeposit>(reference);
    } else {
        return fail(ErrorCode::InvalidPaymentMethod, "Unknown payment method: " + kind);
    }
    return Result<std::unique_ptr<PaymentMethod>>(std::move(method));
}

} // namespace wallet
