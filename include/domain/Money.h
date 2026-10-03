#pragma once
#include <cstdint>
#include <string>

#include "domain/Result.h"

namespace wallet {

enum class Currency { INR, USD };
const char* toString(Currency c);

// Immutable value object. Stores integer minor units (paise/cents): 125.50 -> 12550.
// Why not double? 0.1 + 0.2 != 0.3 in binary floating point; money must be exact.
// Invariant: 0 <= minor <= kMaxMinor, so sums can never silently overflow int64.
class Money {
public:
    static constexpr std::int64_t kMaxMinor = 1000000000000LL;

    Money() = default;  // INR 0.00
    static Money fromMinor(std::int64_t minor, Currency c = Currency::INR);  // throws WalletException
    static Result<Money> parse(const std::string& text, Currency c = Currency::INR);

    std::int64_t minor() const noexcept { return minor_; }
    Currency currency() const noexcept { return currency_; }
    bool isZero() const noexcept { return minor_ == 0; }

    Money operator+(const Money& o) const;  // throws on currency mismatch / exceeding kMaxMinor
    Money operator-(const Money& o) const;  // throws if the result would be negative

    bool operator==(const Money& o) const;
    bool operator!=(const Money& o) const { return !(*this == o); }
    bool operator<(const Money& o) const;
    bool operator>(const Money& o) const { return o < *this; }
    bool operator<=(const Money& o) const { return !(o < *this); }
    bool operator>=(const Money& o) const { return !(*this < o); }

    std::string toString() const;  // "INR 125.50"

private:
    Money(std::int64_t minor, Currency c) : minor_(minor), currency_(c) {}
    void requireSameCurrency(const Money& o) const;

    std::int64_t minor_ = 0;
    Currency currency_ = Currency::INR;
};

} // namespace wallet
