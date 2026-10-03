#include "domain/Money.h"

#include <algorithm>
#include <cctype>

namespace wallet {

const char* toString(Currency c) { return c == Currency::INR ? "INR" : "USD"; }

Money Money::fromMinor(std::int64_t minor, Currency c) {
    if (minor < 0) throw WalletException({ErrorCode::InvalidAmount, "Amount cannot be negative"});
    if (minor > kMaxMinor) throw WalletException({ErrorCode::Overflow, "Amount exceeds maximum supported value"});
    return Money(minor, c);
}

static bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

Result<Money> Money::parse(const std::string& text, Currency c) {
    auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return fail(ErrorCode::InvalidAmount, "Amount is empty");
    auto last = text.find_last_not_of(" \t");
    std::string s = text.substr(first, last - first + 1);

    auto dot = s.find('.');
    std::string whole = s.substr(0, dot);
    std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    if (!allDigits(whole) || (dot != std::string::npos && (!allDigits(frac) || frac.size() > 2)))
        return fail(ErrorCode::InvalidAmount, "Amount must look like 125 or 125.50 (no signs or symbols)");
    if (whole.size() > 13) return fail(ErrorCode::Overflow, "Amount is too large");
    while (frac.size() < 2) frac += '0';
    std::int64_t minor = std::stoll(whole) * 100 + std::stoll(frac);
    if (minor > kMaxMinor) return fail(ErrorCode::Overflow, "Amount is too large");
    return Money(minor, c);
}

void Money::requireSameCurrency(const Money& o) const {
    if (currency_ != o.currency_)
        throw WalletException({ErrorCode::CurrencyMismatch, "Cannot combine different currencies"});
}

Money Money::operator+(const Money& o) const {
    requireSameCurrency(o);
    if (minor_ > kMaxMinor - o.minor_) throw WalletException({ErrorCode::Overflow, "Money overflow"});
    return Money(minor_ + o.minor_, currency_);
}

Money Money::operator-(const Money& o) const {
    requireSameCurrency(o);
    if (o.minor_ > minor_) throw WalletException({ErrorCode::InsufficientFunds, "Subtraction would be negative"});
    return Money(minor_ - o.minor_, currency_);
}

bool Money::operator==(const Money& o) const { return currency_ == o.currency_ && minor_ == o.minor_; }

bool Money::operator<(const Money& o) const {
    requireSameCurrency(o);
    return minor_ < o.minor_;
}

std::string Money::toString() const {
    std::string frac = std::to_string(minor_ % 100);
    if (frac.size() < 2) frac.insert(0, "0");
    return std::string(wallet::toString(currency_)) + " " + std::to_string(minor_ / 100) + "." + frac;
}

} // namespace wallet
