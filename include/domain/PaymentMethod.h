#pragma once
#include <memory>
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"

namespace wallet {

// Abstraction over where top-up money comes from. PaymentService depends only on this
// interface; BankAccount/DebitCard/CreditCard/CashDeposit each decide for themselves
// whether a pull of `amount` would be accepted (simulated: no real bank is contacted).
class PaymentMethod {
public:
    virtual ~PaymentMethod() = default;
    virtual std::string kind() const = 0;       // "BANK_ACCOUNT", "DEBIT_CARD", ...
    virtual std::string describe() const = 0;   // masked reference, safe to log/store
    virtual Result<void> authorize(const Money& amount) = 0;
};

class BankAccount : public PaymentMethod {
public:
    BankAccount(std::string accountNumber, Money simulatedBalance);
    std::string kind() const override { return "BANK_ACCOUNT"; }
    std::string describe() const override;
    Result<void> authorize(const Money& amount) override;
private:
    std::string accountNumber_;
    Money balance_;
};

class DebitCard : public PaymentMethod {
public:
    DebitCard(std::string cardNumber, Money simulatedBalance);
    std::string kind() const override { return "DEBIT_CARD"; }
    std::string describe() const override;
    Result<void> authorize(const Money& amount) override;
private:
    std::string cardNumber_;
    Money balance_;
};

class CreditCard : public PaymentMethod {
public:
    CreditCard(std::string cardNumber, Money creditLimit);
    std::string kind() const override { return "CREDIT_CARD"; }
    std::string describe() const override;
    Result<void> authorize(const Money& amount) override;
private:
    std::string cardNumber_;
    Money available_;
};

class CashDeposit : public PaymentMethod {
public:
    explicit CashDeposit(std::string agentCode) : agentCode_(std::move(agentCode)) {}
    std::string kind() const override { return "CASH_DEPOSIT"; }
    std::string describe() const override { return "Cash via agent " + agentCode_; }
    Result<void> authorize(const Money& amount) override;
private:
    std::string agentCode_;
};

// Factory: builds a method from user-supplied kind + reference. Simulated funding is
// generous by default so demos work; real validation would call a gateway.
class PaymentMethodFactory {
public:
    static Result<std::unique_ptr<PaymentMethod>> create(const std::string& kind, const std::string& reference);
};

} // namespace wallet
