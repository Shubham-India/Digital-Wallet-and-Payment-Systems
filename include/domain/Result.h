#pragma once
// Error model: business failures travel as values (Result<T>); programmer errors
// (e.g. building a negative Money) throw WalletException. See docs/design/error-handling.md.
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace wallet {

enum class ErrorCode {
    InvalidAmount, InsufficientFunds, WalletFrozen, WalletClosed, WalletNotFound,
    UserNotFound, MerchantNotFound, TransactionNotFound, RefundNotFound,
    NotAuthenticated, Unauthorized, InvalidCredentials, AccountLocked, AccountInactive,
    DuplicateUser, ValidationFailed, LimitExceeded, FraudRejected, IdempotencyMismatch,
    RefundExceedsOriginal, RefundNotAllowed, DuplicateRefundRequest, InvalidTransition,
    InvalidPaymentMethod, InvalidIdentifier, SelfTransfer, CurrencyMismatch,
    PersistenceFailure, Overflow
};

const char* toString(ErrorCode code);

struct Error {
    ErrorCode code;
    std::string message;
};

class WalletException : public std::runtime_error {
public:
    explicit WalletException(Error e) : std::runtime_error(e.message), error_(std::move(e)) {}
    const Error& error() const noexcept { return error_; }
private:
    Error error_;
};

// Implicitly convertible to any Result<T>, so `return fail(code, msg);` works everywhere.
struct Failure {
    Error error;
};
inline Failure fail(ErrorCode code, std::string message) {
    return Failure{Error{code, std::move(message)}};
}
inline Failure fail(const Error& e) { return Failure{e}; }

template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Failure f) : data_(std::move(f.error)) {}
    bool ok() const { return data_.index() == 0; }
    explicit operator bool() const { return ok(); }
    const T& value() const& { check(); return std::get<0>(data_); }
    T& value() & { check(); return std::get<0>(data_); }
    T value() && { check(); return std::get<0>(std::move(data_)); }  // by value: safe in range-for over a temporary
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }
    const T& operator*() const { return value(); }
    const Error& error() const { return std::get<1>(data_); }
private:
    void check() const { if (!ok()) throw WalletException(std::get<1>(data_)); }
    std::variant<T, Error> data_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Failure f) : error_(std::move(f.error)) {}
    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *error_; }
private:
    std::optional<Error> error_;
};

} // namespace wallet
