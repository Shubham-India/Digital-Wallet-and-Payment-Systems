#include "services/PaymentService.h"

#include <algorithm>

#include "domain/PaymentIdentifier.h"

namespace wallet {

namespace {
bool countsAsSpent(const Transaction& t, const std::string& userId) {
    TxStatus s = t.status();
    return t.isOutgoingFor(userId) &&
           (s == TxStatus::Success || s == TxStatus::PartiallyRefunded || s == TxStatus::Refunded);
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "; ") + p;
    return out;
}
}  // namespace

Permission PaymentService::permissionFor(TxType type) {
    switch (type) {
        case TxType::TopUp: return Permission::AddMoney;
        case TxType::Withdrawal: return Permission::Withdraw;
        case TxType::Transfer: return Permission::Transfer;
        default: return Permission::PayMerchant;
    }
}

// ---------- public entry points ----------
Result<PaymentOutcome> PaymentService::addMoney(const Session& s, const Money& amount, PaymentMethod& method, const std::string& key) {
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::TopUp; c.amount = amount; c.method = &method;
    c.description = "Add money via " + method.describe();
    return execute(s, c);
}

Result<PaymentOutcome> PaymentService::withdraw(const Session& s, const Money& amount, const std::string& key) {
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::Withdrawal; c.amount = amount; c.description = "Withdrawal to bank";
    return execute(s, c);
}

Result<PaymentOutcome> PaymentService::transfer(const Session& s, const std::string& toEmail, const Money& amount,
                                                const std::string& key, const std::string& description) {
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::Transfer; c.counterparty = toEmail; c.amount = amount;
    c.description = description;
    return execute(s, c);
}

Result<PaymentOutcome> PaymentService::payMerchant(const Session& s, const std::string& merchantPaymentId, const Money& amount,
                                                   const std::string& key, const std::string& description,
                                                   const std::string& orderRef) {
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::MerchantPayment; c.counterparty = merchantPaymentId; c.amount = amount;
    c.description = description; c.reference = orderRef;
    return execute(s, c);
}

Result<PaymentOutcome> PaymentService::payQr(const Session& s, const std::string& qrText, std::optional<Money> amount,
                                             const std::string& key) {
    auto parsed = PaymentIdentifier::parse(qrText);
    if (!parsed) return fail(parsed.error());
    if (parsed->amount && amount && *parsed->amount != *amount)
        return fail(ErrorCode::InvalidAmount, "Amount differs from the amount encoded in the QR code");
    std::optional<Money> chosen = parsed->amount ? parsed->amount : amount;
    if (!chosen) return fail(ErrorCode::InvalidAmount, "QR code has no amount; please enter one");
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::QrPayment; c.counterparty = parsed->merchantPaymentId; c.amount = *chosen;
    c.reference = parsed->orderId; c.description = "QR payment for order " + parsed->orderId;
    return execute(s, c);
}

Result<PaymentOutcome> PaymentService::payBill(const Session& s, const std::string& biller, const std::string& accountNumber,
                                               const Money& amount, const std::string& key) {
    if (biller.empty() || accountNumber.empty())
        return fail(ErrorCode::ValidationFailed, "Biller and account number are required");
    PaymentCommand c;
    c.idempotencyKey = key; c.type = TxType::BillPayment; c.counterparty = biller; c.amount = amount;
    c.reference = accountNumber; c.description = "Bill payment to " + biller;
    return execute(s, c);
}

// ---------- pipeline ----------
Result<void> PaymentService::commitOrFail() {
    if (auto c = d_.db.commit(); !c) {
        d_.log.error("payment", "commit failed: " + c.error().message);
        return fail(ErrorCode::PersistenceFailure, "Could not persist the operation: " + c.error().message);
    }
    return {};
}

Failure PaymentService::abort(Transaction& tx, const Error& e, const std::string& auditResult) {
    Timestamp now = d_.clock.now();
    tx.markFailed(e.code, e.message, now);
    d_.db.transactions().save(tx);
    d_.audit.record(tx.data().payerUserId, std::string("PAYMENT_") + toString(tx.type()), tx.id(), auditResult,
                    {{"code", toString(e.code)}, {"reason", e.message}, {"amount", tx.amount().toString()}});
    d_.notifier.toUser(tx.data().payerUserId, "Payment failed",
                       std::string(toString(tx.type())) + " of " + tx.amount().toString() + " failed: " + e.message);
    if (auto c = commitOrFail(); !c) d_.log.error("payment", "failed to persist failed tx " + tx.id());
    d_.notifier.flush();
    d_.log.info("payment", tx.id() + " failed: " + toString(e.code));
    return fail(e);
}

Result<PaymentOutcome> PaymentService::execute(const Session& session, const PaymentCommand& cmd) {
    if (auto a = d_.access.authorize(session, permissionFor(cmd.type)); !a) {
        d_.audit.record(session.userId, std::string("PAYMENT_") + toString(cmd.type), "-", "DENIED", {{"reason", a.error().message}});
        (void)d_.db.commit();
        return fail(a.error());
    }
    auto payer = d_.db.users().findById(session.userId);
    if (!payer) return fail(ErrorCode::UserNotFound, "Unknown user");

    auto reject = [&](ErrorCode code, const std::string& msg) -> Failure {
        d_.audit.record(payer->id(), std::string("PAYMENT_") + toString(cmd.type), "-", "FAILURE", {{"reason", msg}});
        (void)d_.db.commit();
        return fail(code, msg);
    };
    if (cmd.amount.isZero()) return reject(ErrorCode::InvalidAmount, "Amount must be greater than zero");
    if (cmd.idempotencyKey.empty() || cmd.idempotencyKey.size() > 100)
        return reject(ErrorCode::ValidationFailed, "An idempotency key of 1-100 characters is required");

    const std::string scoped = payer->id() + ":" + cmd.idempotencyKey;
    const std::string fingerprint = std::string(toString(cmd.type)) + "|" + cmd.counterparty + "|" +
                                    std::to_string(cmd.amount.minor()) + "|" + cmd.reference;

    // Idempotency: the same key from the same user must describe the same request.
    if (auto existing = d_.db.transactions().findByScopedKey(scoped)) {
        if (existing->meta("fingerprint") != fingerprint)
            return reject(ErrorCode::IdempotencyMismatch, "Idempotency key was already used for a different request");
        d_.audit.record(payer->id(), "DUPLICATE_REQUEST", existing->id(), "SUCCESS", {{"key", cmd.idempotencyKey}});
        (void)d_.db.commit();
        if (existing->status() == TxStatus::Failed) {
            return fail(ErrorCode::ValidationFailed, "Original request failed: " + existing->data().failureReason);
        }
        return PaymentOutcome{*existing, true, {}};
    }

    TransactionData data;
    data.id = d_.ids.next("TX", 6);
    data.scopedKey = scoped;
    data.type = cmd.type;
    data.status = TxStatus::Pending;
    data.amount = cmd.amount;
    data.refundedAmount = Money::fromMinor(0, cmd.amount.currency());
    data.method = cmd.method ? cmd.method->describe() : "WALLET";
    data.reference = cmd.reference;
    data.description = cmd.description;
    data.createdAt = data.updatedAt = d_.clock.now();
    data.metadata["fingerprint"] = fingerprint;
    Transaction tx(std::move(data));
    tx.setRouting(payer->id(), "", payer->walletId(), "");

    auto routing = resolveRouting(tx, *payer, cmd);
    d_.db.transactions().save(tx);  // saved after routing so the per-user indexes are right
    if (!routing) return abort(tx, routing.error());
    return process(tx, *payer, cmd);
}

Result<void> PaymentService::resolveRouting(Transaction& tx, const User& payer, const PaymentCommand& cmd) {
    auto checkRecipient = [&](const std::shared_ptr<User>& payee, const char* what) -> Result<void> {
        if (!payee) return fail(ErrorCode::UserNotFound, std::string(what) + " not found");
        if (payee->id() == payer.id()) return fail(ErrorCode::SelfTransfer, "You cannot pay yourself");
        if (payee->role() == Role::Admin || payee->walletId().empty())
            return fail(ErrorCode::ValidationFailed, std::string(what) + " cannot receive money");
        if (payee->status() != AccountStatus::Active) return fail(ErrorCode::AccountInactive, std::string(what) + " account is not active");
        auto wallet = d_.db.wallets().find(payee->walletId());
        if (!wallet) return fail(ErrorCode::WalletNotFound, "Recipient wallet not found");
        if (wallet->status() == WalletStatus::Frozen) return fail(ErrorCode::WalletFrozen, "Recipient wallet is frozen");
        if (wallet->status() == WalletStatus::Closed) return fail(ErrorCode::WalletClosed, "Recipient wallet is closed");
        return {};
    };

    switch (cmd.type) {
        case TxType::TopUp:
            if (!cmd.method) return fail(ErrorCode::InvalidPaymentMethod, "A payment method is required");
            tx.setRouting(payer.id(), payer.id(), kExternalWallet, payer.walletId());
            return {};
        case TxType::Withdrawal:
            tx.setRouting(payer.id(), "", payer.walletId(), kExternalWallet);
            return {};
        case TxType::BillPayment:
            tx.setRouting(payer.id(), "", payer.walletId(), kExternalWallet);
            tx.setMeta("biller", cmd.counterparty);
            return {};
        case TxType::Transfer: {
            auto payee = d_.db.users().findByEmail(cmd.counterparty);
            if (auto r = checkRecipient(payee, "Recipient"); !r) return r;
            tx.setRouting(payer.id(), payee->id(), payer.walletId(), payee->walletId(),
                          payee->role() == Role::Merchant ? payee->id() : "");
            return {};
        }
        case TxType::MerchantPayment:
        case TxType::QrPayment: {
            std::shared_ptr<User> merchant = d_.db.users().findMerchantByPaymentId(cmd.counterparty);
            if (!merchant) return fail(ErrorCode::MerchantNotFound, "Unknown merchant payment id: " + cmd.counterparty);
            if (auto r = checkRecipient(merchant, "Merchant"); !r) return r;
            tx.setRouting(payer.id(), merchant->id(), payer.walletId(), merchant->walletId(), merchant->id());
            return {};
        }
        case TxType::Refund:
            return fail(ErrorCode::ValidationFailed, "Refunds are created by RefundService");
    }
    return fail(ErrorCode::ValidationFailed, "Unsupported transaction type");
}

Result<PaymentOutcome> PaymentService::process(Transaction& tx, const User& payer, const PaymentCommand& cmd) {
    const Money amount = tx.amount();
    const Timestamp now = d_.clock.now();
    const bool outflow = tx.type() != TxType::TopUp;

    if (payer.status() != AccountStatus::Active) return abort(tx, {ErrorCode::AccountInactive, "Your account is not active"});
    auto wallet = d_.db.wallets().find(payer.walletId());
    if (!wallet) return abort(tx, {ErrorCode::WalletNotFound, "Wallet not found"});

    if (tx.type() == TxType::TopUp) {
        if (auto a = cmd.method->authorize(amount); !a) return abort(tx, a.error());
    }

    // Limits (daily usage is computed from the user's own history for today).
    LimitContext lc{amount, Money::fromMinor(0, amount.currency()), 0};
    if (outflow) {
        for (const auto& t : d_.db.transactions().forUserSince(payer.id(), startOfDay(now))) {
            if (t.id() != tx.id() && countsAsSpent(t, payer.id())) {
                lc.spentToday = lc.spentToday + t.amount();
                ++lc.countToday;
            }
        }
    }
    if (auto r = LimitPolicyFactory::forTier(LimitPolicyFactory::tierFor(payer), d_.config)->check(lc); !r)
        return abort(tx, r.error());

    RiskAssessment risk;
    if (outflow) {
        // Cheap feasibility probe on a copy: frozen/closed/insufficient fail here with a precise error.
        Wallet probe = *wallet;
        if (auto r = probe.debit(amount); !r) return abort(tx, r.error());

        const FraudSettings& fs = d_.config.fraud;
        Timestamp horizon = now - std::max(fs.velocityWindowSeconds, fs.failureWindowSeconds);
        RiskContext rc;
        rc.userId = payer.id(); rc.amount = amount; rc.now = now;
        for (auto& t : d_.db.transactions().forUserSince(payer.id(), horizon))
            if (t.id() != tx.id()) rc.recent.push_back(std::move(t));
        risk = d_.fraud.assess(rc);
        tx.setMeta("riskScore", std::to_string(risk.score));
        tx.setMeta("riskDecision", toString(risk.decision));
        if (!risk.reasons.empty()) tx.setMeta("riskReasons", join(risk.reasons));

        if (risk.decision == RiskDecision::Reject) {
            return abort(tx, {ErrorCode::FraudRejected, "Rejected by risk engine: " + join(risk.reasons)}, "SUSPICIOUS");
        }
        if (risk.decision == RiskDecision::Review) {
            (void)tx.transitionTo(TxStatus::PendingReview, now);
            d_.db.transactions().save(tx);
            d_.reviewQueue.push(tx.id(), risk.score, now);
            d_.audit.record(payer.id(), std::string("PAYMENT_") + toString(tx.type()), tx.id(), "SUSPICIOUS",
                            {{"score", std::to_string(risk.score)}, {"reasons", join(risk.reasons)}});
            d_.notifier.toUser(payer.id(), "Payment under review",
                               "Your " + amount.toString() + " payment " + tx.id() + " is held for manual review.");
            if (auto c = commitOrFail(); !c) return fail(c.error());
            d_.notifier.flush();
            return PaymentOutcome{tx, false, risk};
        }
    }
    return finalize(tx, risk);
}

Result<PaymentOutcome> PaymentService::finalize(Transaction& tx, const RiskAssessment& risk) {
    const Timestamp now = d_.clock.now();
    if (auto t = tx.transitionTo(TxStatus::Processing, now); !t) return abort(tx, t.error());
    d_.db.transactions().save(tx);
    if (auto s = d_.settlement.settle(tx); !s) return abort(tx, s.error());
    (void)tx.transitionTo(TxStatus::Success, now);
    d_.db.transactions().save(tx);

    const auto& d = tx.data();
    d_.audit.record(d.payerUserId, std::string("PAYMENT_") + toString(tx.type()), tx.id(), "SUCCESS",
                    {{"amount", tx.amount().toString()}, {"payee", d.payeeUserId}});
    switch (tx.type()) {
        case TxType::TopUp:
            d_.notifier.toUser(d.payerUserId, "Money added", tx.amount().toString() + " added to your wallet (" + tx.id() + ").");
            break;
        case TxType::Withdrawal:
            d_.notifier.toUser(d.payerUserId, "Withdrawal complete", tx.amount().toString() + " sent to your bank (" + tx.id() + ").");
            break;
        default:
            d_.notifier.toUser(d.payerUserId, "Payment sent", tx.amount().toString() + " paid (" + tx.id() + ").");
            d_.notifier.toUser(d.payeeUserId, "Payment received", tx.amount().toString() + " received (" + tx.id() + ").");
    }
    if (auto c = commitOrFail(); !c) return fail(c.error());
    d_.notifier.flush();
    d_.log.info("payment", tx.id() + " " + toString(tx.type()) + " success");
    return PaymentOutcome{tx, false, risk};
}

// ---------- admin review resolution ----------
Result<Transaction> PaymentService::approveReview(const Session& admin, const std::string& txId) {
    if (auto a = d_.access.authorize(admin, Permission::ReviewFraud); !a) return fail(a.error());
    auto found = d_.db.transactions().findById(txId);
    if (!found) return fail(ErrorCode::TransactionNotFound, "Transaction not found");
    Transaction tx = *found;
    if (tx.status() != TxStatus::PendingReview)
        return fail(ErrorCode::InvalidTransition, "Transaction " + txId + " is not awaiting review");
    tx.setMeta("reviewedBy", admin.userId);
    auto outcome = finalize(tx, {});
    if (!outcome) {
        d_.reviewQueue.remove(txId);
        return fail(outcome.error());
    }
    d_.reviewQueue.remove(txId);
    d_.audit.record(admin.userId, "REVIEW_APPROVE", txId, "SUCCESS");
    (void)d_.db.commit();
    return outcome->tx;
}

Result<Transaction> PaymentService::rejectReview(const Session& admin, const std::string& txId, const std::string& reason) {
    if (auto a = d_.access.authorize(admin, Permission::ReviewFraud); !a) return fail(a.error());
    auto found = d_.db.transactions().findById(txId);
    if (!found) return fail(ErrorCode::TransactionNotFound, "Transaction not found");
    Transaction tx = *found;
    if (auto t = tx.transitionTo(TxStatus::Cancelled, d_.clock.now()); !t) return fail(t.error());
    tx.setMeta("reviewedBy", admin.userId);
    tx.setMeta("reviewReason", reason);
    d_.db.transactions().save(tx);
    d_.reviewQueue.remove(txId);
    d_.audit.record(admin.userId, "REVIEW_REJECT", txId, "SUCCESS", {{"reason", reason}});
    d_.notifier.toUser(tx.data().payerUserId, "Payment cancelled",
                       "Payment " + txId + " was not approved after review. No money was moved.");
    if (auto c = commitOrFail(); !c) return fail(c.error());
    d_.notifier.flush();
    return tx;
}

} // namespace wallet
