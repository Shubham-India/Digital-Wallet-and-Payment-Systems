#pragma once
#include <optional>
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"

namespace wallet {

struct ParsedPaymentIdentifier {
    std::string merchantPaymentId;
    std::string orderId;
    std::optional<Money> amount;
};

// Simulated QR payload: NETRAPAY://merchant/<merchantPaymentId>/order/<orderId>[?amount=<paise>]
// No QR image is generated; this is the text a QR code would carry.
class PaymentIdentifier {
public:
    static std::string generate(const std::string& merchantPaymentId, const std::string& orderId,
                                std::optional<Money> amount = std::nullopt);
    static Result<ParsedPaymentIdentifier> parse(const std::string& text);
};

} // namespace wallet
