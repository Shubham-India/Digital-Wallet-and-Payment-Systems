#include "presentation/Formatter.h"

#include <iomanip>
#include <sstream>

namespace wallet {

std::string Formatter::name(const std::string& userId) const {
    if (userId.empty()) return "External";
    auto u = ctx_.db->users().findById(userId);
    return u ? u->name() + " (" + u->id() + ")" : userId;
}

std::string Formatter::line(const Transaction& t) const {
    const auto& d = t.data();
    std::ostringstream o;
    o << std::left << std::setw(10) << d.id << ' ' << formatTimestamp(d.createdAt) << ' ' << std::setw(17) << toString(d.type)
      << std::right << std::setw(16) << d.amount.toString() << ' ' << std::left << std::setw(20) << toString(d.status);
    return o.str();
}

std::string Formatter::table(const std::vector<Transaction>& txs) const {
    if (txs.empty()) return "  (no transactions)\n";
    std::ostringstream o;
    o << "  " << std::left << std::setw(10) << "ID" << ' ' << std::setw(19) << "WHEN (UTC)" << ' ' << std::setw(17) << "TYPE"
      << std::right << std::setw(16) << "AMOUNT" << ' ' << std::left << std::setw(20) << "STATUS" << "COUNTERPARTY\n";
    for (const auto& t : txs) {
        const auto& d = t.data();
        std::string who = d.type == TxType::TopUp ? "-" : name(d.payeeUserId.empty() ? "" : d.payeeUserId);
        if (d.type == TxType::BillPayment) who = t.meta("biller");
        o << "  " << line(t) << who << "\n";
    }
    return o.str();
}

std::string Formatter::details(const Transaction& t) const {
    const auto& d = t.data();
    std::ostringstream o;
    o << "  ID:          " << d.id << "\n"
      << "  Type:        " << toString(d.type) << "\n"
      << "  Status:      " << toString(d.status) << "\n"
      << "  Amount:      " << d.amount.toString() << "\n"
      << "  Created:     " << formatTimestamp(d.createdAt) << "\n"
      << "  From:        " << name(d.payerUserId) << "\n"
      << "  To:          " << (d.type == TxType::BillPayment ? t.meta("biller") : name(d.payeeUserId)) << "\n"
      << "  Method:      " << d.method << "\n";
    if (!d.merchantId.empty()) o << "  Merchant:    " << name(d.merchantId) << "\n";
    if (!d.reference.empty()) o << "  Reference:   " << d.reference << "\n";
    if (!d.description.empty()) o << "  Description: " << d.description << "\n";
    if (!d.linkedTxId.empty()) o << "  Refund of:   " << d.linkedTxId << "\n";
    if (!d.refundedAmount.isZero()) o << "  Refunded:    " << d.refundedAmount.toString() << " of " << d.amount.toString() << "\n";
    if (!d.failureReason.empty()) o << "  Failure:     [" << d.failureCode << "] " << d.failureReason << "\n";
    if (!t.meta("riskDecision").empty())
        o << "  Risk:        " << t.meta("riskDecision") << " (score " << t.meta("riskScore") << ")"
          << (t.meta("riskReasons").empty() ? "" : " - " + t.meta("riskReasons")) << "\n";
    return o.str();
}

std::string Formatter::ledger(const std::vector<LedgerEntry>& entries) const {
    if (entries.empty()) return "  (no ledger entries - no money moved)\n";
    std::ostringstream o;
    for (const auto& e : entries)
        o << "  " << e.id << "  " << std::left << std::setw(8) << toString(e.direction) << std::setw(10) << e.walletId
          << std::right << std::setw(16) << e.amount.toString()
          << (e.walletId == kExternalWallet ? "" : "   balance after " + e.balanceAfter.toString()) << "\n";
    return o.str();
}

std::string Formatter::user(const User& u) const {
    std::ostringstream o;
    o << "  " << std::left << std::setw(10) << u.id() << std::setw(10) << toString(u.role()) << std::setw(10) << toString(u.status())
      << std::setw(22) << u.name() << u.email();
    return o.str();
}

std::string Formatter::audit(const AuditEvent& e) const {
    std::ostringstream o;
    o << "  " << e.id << ' ' << formatTimestamp(e.timestamp) << ' ' << std::left << std::setw(10) << e.actor << std::setw(26) << e.action
      << std::setw(12) << e.target << e.result;
    return o.str();
}

std::string Formatter::wallet(const Wallet& w) const {
    return "  Wallet " + w.id() + "  balance " + w.balance().toString() + "  [" + toString(w.status()) + "]";
}

std::string Formatter::review(const ReviewItem& item) const {
    auto tx = ctx_.db->transactions().findById(item.transactionId);
    std::ostringstream o;
    o << "  risk " << std::setw(3) << item.riskScore << "  ";
    if (tx) o << line(*tx) << name(tx->data().payerUserId) << "  " << tx->meta("riskReasons");
    else o << item.transactionId;
    return o.str();
}

} // namespace wallet
