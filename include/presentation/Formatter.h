#pragma once
#include <string>
#include <vector>

#include "app/AppContext.h"

namespace wallet {

// Turns domain objects into display text. Pure presentation: no business rules here.
class Formatter {
public:
    explicit Formatter(AppContext& ctx) : ctx_(ctx) {}

    std::string name(const std::string& userId) const;      // "Alice (USR-0001)" or "-" / "External"
    std::string line(const Transaction& t) const;           // one-line summary
    std::string details(const Transaction& t) const;        // multi-line
    std::string ledger(const std::vector<LedgerEntry>& entries) const;
    std::string table(const std::vector<Transaction>& txs) const;
    std::string user(const User& u) const;
    std::string audit(const AuditEvent& e) const;
    std::string wallet(const Wallet& w) const;
    std::string review(const ReviewItem& item) const;

private:
    AppContext& ctx_;
};

} // namespace wallet
