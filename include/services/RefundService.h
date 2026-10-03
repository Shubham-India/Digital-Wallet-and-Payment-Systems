#pragma once
#include <string>
#include <vector>

#include "infrastructure/Clock.h"
#include "infrastructure/IdGenerator.h"
#include "infrastructure/Logger.h"
#include "repositories/Repositories.h"
#include "services/AccessControl.h"
#include "services/AuditLogger.h"
#include "services/Notifier.h"
#include "services/SettlementService.h"

namespace wallet {

// Refunds are NEW transactions of type REFUND that move money back (merchant wallet -> customer wallet)
// and point at the original via linkedTxId. The original's refundedAmount is capped at its amount,
// which also blocks duplicate/over refunds; an idempotency key blocks accidental repeats.
class RefundService {
public:
    struct Dependencies {
        Database& db;
        AccessControl& access;
        SettlementService& settlement;
        Notifier& notifier;
        AuditLogger& audit;
        IdGenerator& ids;
        const IClock& clock;
        const Logger& log;
    };
    explicit RefundService(Dependencies deps) : d_(deps) {}

    // Merchant (or admin) refunds part or all of a payment directly.
    Result<Transaction> issueRefund(const Session& actor, const std::string& originalTxId, const Money& amount,
                                    const std::string& reason, const std::string& idempotencyKey);

    // Customer asks; merchant decides.
    Result<RefundRequest> requestRefund(const Session& customer, const std::string& originalTxId, const Money& amount,
                                        const std::string& reason);
    Result<Transaction> approveRequest(const Session& merchant, const std::string& requestId);
    Result<RefundRequest> rejectRequest(const Session& merchant, const std::string& requestId);

    std::vector<RefundRequest> requestsFor(const Session& actor) const;  // customer's own or merchant's incoming
    std::vector<Transaction> refundsOf(const std::string& originalTxId) const;

private:
    Dependencies d_;
};

} // namespace wallet
