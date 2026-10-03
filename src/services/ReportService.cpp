#include "services/ReportService.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace wallet {

namespace {
bool isPayment(TxType t) { return t == TxType::Transfer || t == TxType::MerchantPayment || t == TxType::QrPayment || t == TxType::BillPayment; }
bool settled(TxStatus s) { return s == TxStatus::Success || s == TxStatus::PartiallyRefunded || s == TxStatus::Refunded; }
bool flagged(const Transaction& t) {
    std::string d = t.meta("riskDecision");
    return d == "REVIEW" || d == "REJECT";
}
std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}
}  // namespace

Result<SystemSummary> ReportService::summary(const Session& s) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    SystemSummary out;
    out.volume = out.refundedAmount = Money();
    for (const auto& t : db_.transactions().all()) {
        ++out.totalTransactions;
        if (settled(t.status())) ++out.successful;
        if (t.status() == TxStatus::Failed) ++out.failed;
        if (t.status() == TxStatus::PendingReview) ++out.pendingReview;
        if (t.status() == TxStatus::Cancelled) ++out.cancelled;
        if (!t.refundedAmount().isZero()) ++out.refundedTransactions;
        if (flagged(t)) ++out.suspicious;
        if (settled(t.status()) && isPayment(t.type())) out.volume = out.volume + t.amount();
        if (t.type() == TxType::Refund && t.status() == TxStatus::Success) out.refundedAmount = out.refundedAmount + t.amount();
    }
    for (const auto& u : db_.users().all()) {
        ++out.users;
        if (u->role() == Role::Customer) ++out.customers;
        if (u->role() == Role::Merchant) ++out.merchants;
    }
    return out;
}

Result<std::vector<MerchantTotal>> ReportService::topMerchants(const Session& s, std::size_t n) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    std::unordered_map<std::string, MerchantTotal> totals;  // hash aggregation: O(n)
    for (const auto& t : db_.transactions().all()) {
        if (!settled(t.status()) || (t.type() != TxType::MerchantPayment && t.type() != TxType::QrPayment)) continue;
        auto& m = totals[t.data().merchantId];
        m.merchantId = t.data().merchantId;
        m.total = m.total + t.amount();
        ++m.count;
    }
    std::vector<MerchantTotal> out;
    for (auto& [id, m] : totals) {
        if (auto u = db_.users().findById(id)) {
            if (auto merchant = std::dynamic_pointer_cast<Merchant>(u)) m.businessName = merchant->businessName();
        }
        out.push_back(std::move(m));
    }
    n = std::min(n, out.size());
    std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n), out.end(),
                      [](const MerchantTotal& a, const MerchantTotal& b) { return a.total > b.total; });
    out.resize(n);
    return out;
}

Result<std::vector<Transaction>> ReportService::highestTransactions(const Session& s, std::size_t n) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    auto all = db_.transactions().all();
    n = std::min(n, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(),
                      [](const Transaction& a, const Transaction& b) { return a.amount() > b.amount(); });
    all.erase(all.begin() + static_cast<std::ptrdiff_t>(n), all.end());
    return all;
}

Result<std::vector<Transaction>> ReportService::recentTransactions(const Session& s, std::size_t n) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    auto all = db_.transactions().all();  // already time-ordered: take the tail, newest first
    std::vector<Transaction> out(all.rbegin(), all.rbegin() + static_cast<std::ptrdiff_t>(std::min(n, all.size())));
    return out;
}

Result<std::vector<Transaction>> ReportService::suspiciousTransactions(const Session& s) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    std::vector<Transaction> out;
    for (auto& t : db_.transactions().all())
        if (flagged(t)) out.push_back(std::move(t));
    return out;
}

Result<std::map<std::string, std::size_t>> ReportService::dailyCounts(const Session& s) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    std::map<std::string, std::size_t> out;
    for (const auto& t : db_.transactions().all()) ++out[formatDate(t.createdAt())];
    return out;
}

Result<std::vector<std::pair<std::string, std::size_t>>> ReportService::busiestUsers(const Session& s, std::size_t n) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    std::unordered_map<std::string, std::size_t> freq;  // frequency counting
    for (const auto& t : db_.transactions().all())
        if (!t.data().payerUserId.empty()) ++freq[t.data().payerUserId];
    std::vector<std::pair<std::string, std::size_t>> out(freq.begin(), freq.end());
    n = std::min(n, out.size());
    std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    out.resize(n);
    return out;
}

Result<std::string> ReportService::renderText(const Session& s) const {
    auto sum = summary(s);
    if (!sum) return fail(sum.error());
    std::ostringstream o;
    const SystemSummary& m = sum.value();
    o << "SYSTEM SUMMARY\n"
      << "  Users:                 " << m.users << " (" << m.customers << " customers, " << m.merchants << " merchants)\n"
      << "  Transactions:          " << m.totalTransactions << "\n"
      << "  Successful:            " << m.successful << "\n"
      << "  Failed:                " << m.failed << "\n"
      << "  Awaiting review:       " << m.pendingReview << "\n"
      << "  Cancelled:             " << m.cancelled << "\n"
      << "  Flagged by risk rules: " << m.suspicious << "\n"
      << "  Payment volume:        " << m.volume.toString() << "\n"
      << "  Refunded amount:       " << m.refundedAmount.toString() << " (" << m.refundedTransactions << " payments refunded)\n";
    if (auto top = topMerchants(s, 5); top && !top.value().empty()) {
        o << "TOP MERCHANTS\n";
        for (const auto& t : top.value()) o << "  " << t.businessName << " (" << t.merchantId << "): " << t.total.toString() << " in " << t.count << " payments\n";
    }
    if (auto hi = highestTransactions(s, 3); hi && !hi.value().empty()) {
        o << "HIGHEST TRANSACTIONS\n";
        for (const auto& t : hi.value()) o << "  " << t.id() << " " << toString(t.type()) << " " << t.amount().toString() << " [" << toString(t.status()) << "]\n";
    }
    if (auto days = dailyCounts(s); days && !days.value().empty()) {
        o << "TRANSACTIONS PER DAY\n";
        for (const auto& [day, count] : days.value()) o << "  " << day << ": " << count << "\n";
    }
    return o.str();
}

Result<std::string> ReportService::exportCsv(const Session& s) const {
    if (auto a = access_.authorize(s, Permission::ViewReports); !a) return fail(a.error());
    std::ostringstream o;
    o << "id,created,type,status,payer,payee,amount_minor,refunded_minor,reference,failure_code\n";
    for (const auto& t : db_.transactions().all()) {
        const auto& d = t.data();
        o << d.id << ',' << formatTimestamp(d.createdAt) << ',' << toString(d.type) << ',' << toString(d.status) << ','
          << d.payerUserId << ',' << d.payeeUserId << ',' << d.amount.minor() << ',' << d.refundedAmount.minor() << ','
          << csvEscape(d.reference) << ',' << d.failureCode << '\n';
    }
    return o.str();
}

} // namespace wallet
