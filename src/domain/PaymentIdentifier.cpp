#include "domain/PaymentIdentifier.h"

#include <algorithm>
#include <cctype>

namespace wallet {

namespace {
const std::string kPrefix = "NETRAPAY://merchant/";

bool validToken(const std::string& s) {
    return !s.empty() && s.size() <= 40 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return std::isalnum(c) != 0 || c == '-' || c == '_';
           });
}
}  // namespace

std::string PaymentIdentifier::generate(const std::string& merchantPaymentId, const std::string& orderId,
                                        std::optional<Money> amount) {
    std::string out = kPrefix + merchantPaymentId + "/order/" + orderId;
    if (amount) out += "?amount=" + std::to_string(amount->minor());
    return out;
}

Result<ParsedPaymentIdentifier> PaymentIdentifier::parse(const std::string& text) {
    if (text.compare(0, kPrefix.size(), kPrefix) != 0)
        return fail(ErrorCode::InvalidIdentifier, "Not a NETRAPAY merchant identifier");
    std::string rest = text.substr(kPrefix.size());

    std::string query;
    if (auto q = rest.find('?'); q != std::string::npos) {
        query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    auto sep = rest.find("/order/");
    if (sep == std::string::npos) return fail(ErrorCode::InvalidIdentifier, "Missing /order/ segment");

    ParsedPaymentIdentifier out;
    out.merchantPaymentId = rest.substr(0, sep);
    out.orderId = rest.substr(sep + 7);
    if (!validToken(out.merchantPaymentId) || !validToken(out.orderId))
        return fail(ErrorCode::InvalidIdentifier, "Merchant or order id contains invalid characters");

    if (!query.empty()) {
        if (query.compare(0, 7, "amount=") != 0)
            return fail(ErrorCode::InvalidIdentifier, "Unknown query parameter");
        std::string digits = query.substr(7);
        if (digits.empty() || digits.size() > 13 ||
            !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return fail(ErrorCode::InvalidIdentifier, "Invalid amount in identifier");
        std::int64_t minor = std::stoll(digits);
        if (minor <= 0 || minor > Money::kMaxMinor) return fail(ErrorCode::InvalidIdentifier, "Amount out of range");
        out.amount = Money::fromMinor(minor);
    }
    return out;
}

} // namespace wallet
