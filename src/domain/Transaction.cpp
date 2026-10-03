#include "domain/Transaction.h"

namespace wallet {

static const std::pair<TxType, const char*> kTypes[] = {
    {TxType::TopUp, "TOP_UP"},           {TxType::Withdrawal, "WITHDRAWAL"},
    {TxType::Transfer, "TRANSFER"},      {TxType::MerchantPayment, "MERCHANT_PAYMENT"},
    {TxType::QrPayment, "QR_PAYMENT"},   {TxType::BillPayment, "BILL_PAYMENT"},
    {TxType::Refund, "REFUND"}};

static const std::pair<TxStatus, const char*> kStatuses[] = {
    {TxStatus::Pending, "PENDING"},       {TxStatus::PendingReview, "PENDING_REVIEW"},
    {TxStatus::Processing, "PROCESSING"}, {TxStatus::Success, "SUCCESS"},
    {TxStatus::Failed, "FAILED"},         {TxStatus::Cancelled, "CANCELLED"},
    {TxStatus::PartiallyRefunded, "PARTIALLY_REFUNDED"}, {TxStatus::Refunded, "REFUNDED"}};

const char* toString(TxType t) {
    for (auto& [v, n] : kTypes) if (v == t) return n;
    return "?";
}
const char* toString(TxStatus s) {
    for (auto& [v, n] : kStatuses) if (v == s) return n;
    return "?";
}
bool parseTxType(const std::string& s, TxType& out) {
    for (auto& [v, n] : kTypes) if (s == n) { out = v; return true; }
    return false;
}
bool parseTxStatus(const std::string& s, TxStatus& out) {
    for (auto& [v, n] : kStatuses) if (s == n) { out = v; return true; }
    return false;
}

Money Transaction::refundable() const { return d_.amount - d_.refundedAmount; }

std::string Transaction::meta(const std::string& key) const {
    auto it = d_.metadata.find(key);
    return it == d_.metadata.end() ? std::string() : it->second;
}

bool Transaction::isOutgoingFor(const std::string& userId) const {
    return d_.payerUserId == userId && d_.type != TxType::TopUp && d_.type != TxType::Refund;
}

bool Transaction::canTransition(TxStatus from, TxStatus to) {
    switch (from) {
        case TxStatus::Pending:
            return to == TxStatus::PendingReview || to == TxStatus::Processing || to == TxStatus::Failed ||
                   to == TxStatus::Cancelled;
        case TxStatus::PendingReview:
            return to == TxStatus::Processing || to == TxStatus::Failed || to == TxStatus::Cancelled;
        case TxStatus::Processing:
            return to == TxStatus::Success || to == TxStatus::Failed;
        case TxStatus::Success:
            return to == TxStatus::PartiallyRefunded || to == TxStatus::Refunded;
        case TxStatus::PartiallyRefunded:
            return to == TxStatus::PartiallyRefunded || to == TxStatus::Refunded;
        case TxStatus::Failed:
        case TxStatus::Cancelled:
        case TxStatus::Refunded:
            return false;
    }
    return false;
}

Result<void> Transaction::transitionTo(TxStatus next, Timestamp now) {
    if (!canTransition(d_.status, next))
        return fail(ErrorCode::InvalidTransition, std::string("Illegal transition ") + toString(d_.status) +
                                                      " -> " + toString(next) + " for " + d_.id);
    d_.status = next;
    d_.updatedAt = now;
    return {};
}

void Transaction::markFailed(ErrorCode code, const std::string& reason, Timestamp now) {
    d_.failureCode = toString(code);
    d_.failureReason = reason;
    if (canTransition(d_.status, TxStatus::Failed)) {
        d_.status = TxStatus::Failed;
    }
    d_.updatedAt = now;
}

Result<void> Transaction::applyRefund(const Money& amount, Timestamp now) {
    if (d_.status != TxStatus::Success && d_.status != TxStatus::PartiallyRefunded)
        return fail(ErrorCode::RefundNotAllowed, "Transaction " + d_.id + " is " + toString(d_.status) +
                                                     " and cannot be refunded");
    if (amount.isZero()) return fail(ErrorCode::InvalidAmount, "Refund amount must be greater than zero");
    if (amount > refundable())
        return fail(ErrorCode::RefundExceedsOriginal,
                    "Refund " + amount.toString() + " exceeds refundable " + refundable().toString());
    d_.refundedAmount = d_.refundedAmount + amount;
    d_.status = d_.refundedAmount == d_.amount ? TxStatus::Refunded : TxStatus::PartiallyRefunded;
    d_.updatedAt = now;
    return {};
}

void Transaction::setRouting(std::string payerUserId, std::string payeeUserId, std::string payerWalletId,
                             std::string payeeWalletId, std::string merchantId) {
    d_.payerUserId = std::move(payerUserId);
    d_.payeeUserId = std::move(payeeUserId);
    d_.payerWalletId = std::move(payerWalletId);
    d_.payeeWalletId = std::move(payeeWalletId);
    d_.merchantId = std::move(merchantId);
}

} // namespace wallet
