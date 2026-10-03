#pragma once
#include <map>
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"
#include "utils/TimeUtil.h"

namespace wallet {

// Pseudo-wallet ids for the money's other side when it enters/leaves the system
// (bank, card, cash desk, biller). Keeps every ledger posting double-entry.
inline const std::string kExternalWallet = "EXTERNAL";

enum class TxType { TopUp, Withdrawal, Transfer, MerchantPayment, QrPayment, BillPayment, Refund };
enum class TxStatus { Pending, PendingReview, Processing, Success, Failed, Cancelled, PartiallyRefunded, Refunded };

const char* toString(TxType t);
const char* toString(TxStatus s);
bool parseTxType(const std::string& s, TxType& out);
bool parseTxStatus(const std::string& s, TxStatus& out);

// Plain data of a transaction; used to construct and to persist. The Transaction class
// wraps it and guards the fields that must not change freely (status, refunded amount).
struct TransactionData {
    std::string id;
    std::string scopedKey;  // "<userId>:<idempotencyKey>"
    TxType type = TxType::Transfer;
    TxStatus status = TxStatus::Pending;
    std::string payerUserId, payeeUserId;
    std::string payerWalletId, payeeWalletId;  // may be kExternalWallet
    std::string merchantId;                    // user id of the merchant involved, if any
    Money amount;
    Money refundedAmount;
    std::string method, reference, description;
    std::string failureCode, failureReason;
    std::string linkedTxId;  // for REFUND transactions: the original payment
    Timestamp createdAt = 0;
    Timestamp updatedAt = 0;
    std::map<std::string, std::string> metadata;
};

// A Transaction is the business record of one money movement attempt. It is NOT the
// balance (Wallet) and NOT the accounting posting (LedgerEntry).
// Lifecycle (enforced by canTransition):
//   Pending -> PendingReview -> Processing -> Success -> PartiallyRefunded -> Refunded
//   Pending/PendingReview/Processing -> Failed ; Pending/PendingReview -> Cancelled
class Transaction {
public:
    explicit Transaction(TransactionData data) : d_(std::move(data)) {}

    const TransactionData& data() const noexcept { return d_; }
    const std::string& id() const noexcept { return d_.id; }
    TxType type() const noexcept { return d_.type; }
    TxStatus status() const noexcept { return d_.status; }
    const Money& amount() const noexcept { return d_.amount; }
    const Money& refundedAmount() const noexcept { return d_.refundedAmount; }
    Money refundable() const;  // amount - refundedAmount
    Timestamp createdAt() const noexcept { return d_.createdAt; }
    std::string meta(const std::string& key) const;
    bool isOutgoingFor(const std::string& userId) const;  // user spent money in this tx

    static bool canTransition(TxStatus from, TxStatus to);
    Result<void> transitionTo(TxStatus next, Timestamp now);
    void markFailed(ErrorCode code, const std::string& reason, Timestamp now);
    Result<void> applyRefund(const Money& amount, Timestamp now);

    void setRouting(std::string payerUserId, std::string payeeUserId, std::string payerWalletId,
                    std::string payeeWalletId, std::string merchantId = "");
    void setMeta(const std::string& key, const std::string& value) { d_.metadata[key] = value; }

private:
    TransactionData d_;
};

} // namespace wallet
