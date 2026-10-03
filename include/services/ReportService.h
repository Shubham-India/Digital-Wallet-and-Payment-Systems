#pragma once
#include <map>
#include <string>
#include <vector>

#include "repositories/Repositories.h"
#include "services/AccessControl.h"

namespace wallet {

struct SystemSummary {
    std::size_t totalTransactions = 0, successful = 0, failed = 0, pendingReview = 0, cancelled = 0;
    std::size_t refundedTransactions = 0;  // originals with any refund
    std::size_t suspicious = 0;            // flagged REVIEW or REJECT by the risk engine
    Money volume;                          // sum of successful non-refund payments (not top-ups/withdrawals)
    Money refundedAmount;
    std::size_t users = 0, customers = 0, merchants = 0;
};

struct MerchantTotal {
    std::string merchantId, businessName;
    Money total;
    std::size_t count = 0;
};

// Reporting over repository data. Complexity: summary/top-N are O(n) / O(n log k) over n transactions.
class ReportService {
public:
    ReportService(Database& db, AccessControl& access) : db_(db), access_(access) {}

    Result<SystemSummary> summary(const Session& s) const;
    Result<std::vector<MerchantTotal>> topMerchants(const Session& s, std::size_t n) const;
    Result<std::vector<Transaction>> highestTransactions(const Session& s, std::size_t n) const;
    Result<std::vector<Transaction>> recentTransactions(const Session& s, std::size_t n) const;
    Result<std::vector<Transaction>> suspiciousTransactions(const Session& s) const;
    Result<std::map<std::string, std::size_t>> dailyCounts(const Session& s) const;   // "YYYY-MM-DD" -> count
    Result<std::vector<std::pair<std::string, std::size_t>>> busiestUsers(const Session& s, std::size_t n) const;

    Result<std::string> renderText(const Session& s) const;       // full text report
    Result<std::string> exportCsv(const Session& s) const;        // all transactions as CSV

private:
    Database& db_;
    AccessControl& access_;
};

} // namespace wallet
