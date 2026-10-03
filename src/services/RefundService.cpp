#include "services/RefundService.h"

namespace wallet {

namespace {
bool refundablePaymentType(TxType t) { return t == TxType::MerchantPayment || t == TxType::QrPayment; }
}  // namespace

Result<Transaction> RefundService::issueRefund(const Session& actor, const std::string& originalTxId, const Money& amount,
                                               const std::string& reason, const std::string& key) {
    if (auto a = d_.access.authorize(actor, Permission::IssueRefund); !a) return fail(a.error());
    if (key.empty() || key.size() > 100) return fail(ErrorCode::ValidationFailed, "An idempotency key is required");
    if (amount.isZero()) return fail(ErrorCode::InvalidAmount, "Refund amount must be greater than zero");

    const std::string scoped = actor.userId + ":" + key;
    const std::string fingerprint = "REFUND|" + originalTxId + "|" + std::to_string(amount.minor());
    if (auto existing = d_.db.transactions().findByScopedKey(scoped)) {
        if (existing->meta("fingerprint") != fingerprint)
            return fail(ErrorCode::IdempotencyMismatch, "Idempotency key was already used for a different refund");
        d_.audit.record(actor.userId, "DUPLICATE_REQUEST", existing->id(), "SUCCESS", {{"key", key}});
        (void)d_.db.commit();
        if (existing->status() == TxStatus::Failed)
            return fail(ErrorCode::RefundNotAllowed, "Original refund failed: " + existing->data().failureReason);
        return *existing;
    }

    auto found = d_.db.transactions().findById(originalTxId);
    if (!found) return fail(ErrorCode::TransactionNotFound, "Original transaction not found");
    Transaction original = *found;
    const auto& od = original.data();

    auto deny = [&](ErrorCode code, const std::string& msg) -> Failure {
        d_.audit.record(actor.userId, "REFUND", originalTxId, "FAILURE", {{"reason", msg}});
        (void)d_.db.commit();
        return fail(code, msg);
    };
    if (!refundablePaymentType(od.type)) return deny(ErrorCode::RefundNotAllowed, "Only merchant/QR payments can be refunded");
    if (actor.role == Role::Merchant && od.payeeUserId != actor.userId)
        return deny(ErrorCode::Unauthorized, "You can only refund payments made to you");
    if (original.status() != TxStatus::Success && original.status() != TxStatus::PartiallyRefunded)
        return deny(ErrorCode::RefundNotAllowed, original.status() == TxStatus::Refunded
                                                      ? "Transaction is already fully refunded"
                                                      : std::string("Transaction is ") + toString(original.status()) + " and cannot be refunded");
    if (amount > original.refundable())
        return deny(ErrorCode::RefundExceedsOriginal, "Refund " + amount.toString() + " exceeds the refundable balance " +
                                                          original.refundable().toString());

    TransactionData data;
    data.id = d_.ids.next("TX", 6);
    data.scopedKey = scoped;
    data.type = TxType::Refund;
    data.amount = amount;
    data.refundedAmount = Money::fromMinor(0, amount.currency());
    data.payerUserId = od.payeeUserId;      // the merchant pays back
    data.payeeUserId = od.payerUserId;      // the customer receives
    data.payerWalletId = od.payeeWalletId;
    data.payeeWalletId = od.payerWalletId;
    data.merchantId = od.merchantId;
    data.method = "WALLET";
    data.description = reason.empty() ? "Refund for " + originalTxId : reason;
    data.linkedTxId = originalTxId;
    data.createdAt = data.updatedAt = d_.clock.now();
    data.metadata["fingerprint"] = fingerprint;
    data.metadata["issuedBy"] = actor.userId;
    Transaction refund(std::move(data));
    d_.db.transactions().save(refund);

    auto failRefund = [&](const Error& e) -> Failure {
        refund.markFailed(e.code, e.message, d_.clock.now());
        d_.db.transactions().save(refund);
        d_.audit.record(actor.userId, "REFUND", originalTxId, "FAILURE", {{"reason", e.message}});
        (void)d_.db.commit();
        return fail(e);
    };

    const Timestamp now = d_.clock.now();
    if (auto t = refund.transitionTo(TxStatus::Processing, now); !t) return failRefund(t.error());
    d_.db.transactions().save(refund);
    if (auto s = d_.settlement.settle(refund); !s) return failRefund(s.error());
    (void)refund.transitionTo(TxStatus::Success, now);
    if (auto r = original.applyRefund(amount, now); !r) return failRefund(r.error());  // cannot happen after the checks above
    d_.db.transactions().save(refund);
    d_.db.transactions().save(original);

    d_.audit.record(actor.userId, "REFUND", originalTxId, "SUCCESS",
                    {{"refundTx", refund.id()}, {"amount", amount.toString()}});
    d_.notifier.toUser(od.payerUserId, "Refund received", amount.toString() + " refunded for " + originalTxId + " (" + refund.id() + ").");
    d_.notifier.toUser(od.payeeUserId, "Refund issued", amount.toString() + " refunded to customer for " + originalTxId + ".");
    if (auto c = d_.db.commit(); !c) return fail(ErrorCode::PersistenceFailure, c.error().message);
    d_.notifier.flush();
    d_.log.info("refund", refund.id() + " refunds " + originalTxId);
    return refund;
}

Result<RefundRequest> RefundService::requestRefund(const Session& customer, const std::string& originalTxId,
                                                   const Money& amount, const std::string& reason) {
    if (auto a = d_.access.authorize(customer, Permission::RequestRefund); !a) return fail(a.error());
    if (amount.isZero()) return fail(ErrorCode::InvalidAmount, "Refund amount must be greater than zero");
    auto found = d_.db.transactions().findById(originalTxId);
    if (!found || found->data().payerUserId != customer.userId)
        return fail(ErrorCode::TransactionNotFound, "No such payment of yours");
    if (!refundablePaymentType(found->type())) return fail(ErrorCode::RefundNotAllowed, "Only merchant/QR payments can be refunded");
    if (found->status() != TxStatus::Success && found->status() != TxStatus::PartiallyRefunded)
        return fail(ErrorCode::RefundNotAllowed, "This payment cannot be refunded");
    if (amount > found->refundable())
        return fail(ErrorCode::RefundExceedsOriginal, "Requested amount exceeds the refundable balance " + found->refundable().toString());
    for (const auto& r : d_.db.refunds().all())  // one open request per payment
        if (r.transactionId == originalTxId && r.status == RefundRequestStatus::Pending)
            return fail(ErrorCode::DuplicateRefundRequest, "A refund request for this payment is already pending");

    RefundRequest req;
    req.id = d_.ids.next("RFR", 4);
    req.transactionId = originalTxId;
    req.requesterUserId = customer.userId;
    req.merchantUserId = found->data().payeeUserId;
    req.amount = amount;
    req.reason = reason;
    req.createdAt = d_.clock.now();
    d_.db.refunds().save(req);
    d_.audit.record(customer.userId, "REFUND_REQUEST", req.id, "SUCCESS", {{"tx", originalTxId}, {"amount", amount.toString()}});
    d_.notifier.toUser(req.merchantUserId, "Refund requested", "Customer requests " + amount.toString() + " back for " + originalTxId + " (" + req.id + ").");
    if (auto c = d_.db.commit(); !c) return fail(ErrorCode::PersistenceFailure, c.error().message);
    d_.notifier.flush();
    return req;
}

Result<Transaction> RefundService::approveRequest(const Session& merchant, const std::string& requestId) {
    if (auto a = d_.access.authorize(merchant, Permission::IssueRefund); !a) return fail(a.error());
    auto req = d_.db.refunds().find(requestId);
    if (!req) return fail(ErrorCode::RefundNotFound, "Refund request not found");
    if (merchant.role == Role::Merchant && req->merchantUserId != merchant.userId)
        return fail(ErrorCode::Unauthorized, "This request is addressed to another merchant");
    if (req->status != RefundRequestStatus::Pending) return fail(ErrorCode::RefundNotAllowed, "Request already resolved");

    auto refund = issueRefund(merchant, req->transactionId, req->amount, req->reason, "approve-" + requestId);
    if (!refund) return fail(refund.error());
    req->status = RefundRequestStatus::Approved;
    req->refundTransactionId = refund->id();
    req->resolvedAt = d_.clock.now();
    d_.db.refunds().save(*req);
    d_.audit.record(merchant.userId, "REFUND_APPROVE", requestId, "SUCCESS", {{"refundTx", refund->id()}});
    if (auto c = d_.db.commit(); !c) return fail(ErrorCode::PersistenceFailure, c.error().message);
    return refund.value();
}

Result<RefundRequest> RefundService::rejectRequest(const Session& merchant, const std::string& requestId) {
    if (auto a = d_.access.authorize(merchant, Permission::IssueRefund); !a) return fail(a.error());
    auto req = d_.db.refunds().find(requestId);
    if (!req) return fail(ErrorCode::RefundNotFound, "Refund request not found");
    if (merchant.role == Role::Merchant && req->merchantUserId != merchant.userId)
        return fail(ErrorCode::Unauthorized, "This request is addressed to another merchant");
    if (req->status != RefundRequestStatus::Pending) return fail(ErrorCode::RefundNotAllowed, "Request already resolved");
    req->status = RefundRequestStatus::Rejected;
    req->resolvedAt = d_.clock.now();
    d_.db.refunds().save(*req);
    d_.audit.record(merchant.userId, "REFUND_REJECT", requestId, "SUCCESS");
    d_.notifier.toUser(req->requesterUserId, "Refund declined", "Your refund request " + requestId + " was declined.");
    if (auto c = d_.db.commit(); !c) return fail(ErrorCode::PersistenceFailure, c.error().message);
    d_.notifier.flush();
    return *req;
}

std::vector<RefundRequest> RefundService::requestsFor(const Session& actor) const {
    std::vector<RefundRequest> out;
    for (const auto& r : d_.db.refunds().all()) {
        bool mine = actor.role == Role::Admin || r.requesterUserId == actor.userId || r.merchantUserId == actor.userId;
        if (mine) out.push_back(r);
    }
    return out;
}

std::vector<Transaction> RefundService::refundsOf(const std::string& originalTxId) const {
    std::vector<Transaction> out;
    for (auto& t : d_.db.transactions().all())
        if (t.type() == TxType::Refund && t.data().linkedTxId == originalTxId) out.push_back(std::move(t));
    return out;
}

} // namespace wallet
