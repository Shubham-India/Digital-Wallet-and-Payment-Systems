#pragma once
#include <optional>
#include <string>

#include "domain/PaymentMethod.h"
#include "infrastructure/Clock.h"
#include "infrastructure/Config.h"
#include "infrastructure/IdGenerator.h"
#include "infrastructure/Logger.h"
#include "policies/FraudEngine.h"
#include "policies/LimitPolicy.h"
#include "repositories/Repositories.h"
#include "services/AccessControl.h"
#include "services/AuditLogger.h"
#include "services/FraudReviewQueue.h"
#include "services/Notifier.h"
#include "services/SettlementService.h"

namespace wallet {

// A payment request: what the caller asks for. It becomes a Transaction once accepted.
struct PaymentCommand {
    std::string idempotencyKey;  // same key + same payload => the original result, never a second charge
    TxType type = TxType::Transfer;
    std::string counterparty;    // recipient email | merchant payment id | biller name
    Money amount;
    std::string description;
    std::string reference;       // order id, bill account number, ...
    PaymentMethod* method = nullptr;  // top-ups only
};

struct PaymentOutcome {
    Transaction tx;
    bool duplicate = false;  // true when an idempotent replay returned the earlier result
    RiskAssessment risk;
};

class PaymentService {
public:
    struct Dependencies {
        Database& db;
        AccessControl& access;
        SettlementService& settlement;
        const FraudEngine& fraud;
        FraudReviewQueue& reviewQueue;
        Notifier& notifier;
        AuditLogger& audit;
        IdGenerator& ids;
        const IClock& clock;
        const AppConfig& config;
        const Logger& log;
    };
    explicit PaymentService(Dependencies deps) : d_(deps) {}

    Result<PaymentOutcome> addMoney(const Session& s, const Money& amount, PaymentMethod& method, const std::string& key);
    Result<PaymentOutcome> withdraw(const Session& s, const Money& amount, const std::string& key);
    Result<PaymentOutcome> transfer(const Session& s, const std::string& toEmail, const Money& amount,
                                    const std::string& key, const std::string& description = "");
    Result<PaymentOutcome> payMerchant(const Session& s, const std::string& merchantPaymentId, const Money& amount,
                                       const std::string& key, const std::string& description = "",
                                       const std::string& orderRef = "");
    // QR flow: the identifier carries merchant + order (+ optional fixed amount).
    Result<PaymentOutcome> payQr(const Session& s, const std::string& qrText, std::optional<Money> amount,
                                 const std::string& key);
    Result<PaymentOutcome> payBill(const Session& s, const std::string& biller, const std::string& accountNumber,
                                   const Money& amount, const std::string& key);

    // Admin resolution of transactions held by the risk engine.
    Result<Transaction> approveReview(const Session& admin, const std::string& txId);
    Result<Transaction> rejectReview(const Session& admin, const std::string& txId, const std::string& reason);

private:
    Result<PaymentOutcome> execute(const Session& s, const PaymentCommand& cmd);
    Result<void> resolveRouting(Transaction& tx, const User& payer, const PaymentCommand& cmd);
    Result<PaymentOutcome> process(Transaction& tx, const User& payer, const PaymentCommand& cmd);
    Result<PaymentOutcome> finalize(Transaction& tx, const RiskAssessment& risk);
    Failure abort(Transaction& tx, const Error& e, const std::string& auditResult = "FAILURE");
    Result<void> commitOrFail();
    static Permission permissionFor(TxType type);

    Dependencies d_;
};

} // namespace wallet
