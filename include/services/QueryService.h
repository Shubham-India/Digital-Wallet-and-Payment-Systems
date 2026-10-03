#pragma once
#include <string>
#include <vector>

#include "repositories/Repositories.h"
#include "services/AccessControl.h"

namespace wallet {

// Read-side access to transactions and wallets with authorization applied:
// non-admins can only ever see transactions they take part in.
class QueryService {
public:
    QueryService(Database& db, AccessControl& access) : db_(db), access_(access) {}

    Result<Wallet> myWallet(const Session& s) const;
    // Newest first. Non-admin callers are always restricted to their own userId.
    Result<std::vector<Transaction>> search(const Session& s, TransactionFilter filter) const;
    Result<Transaction> details(const Session& s, const std::string& txId) const;
    Result<std::vector<LedgerEntry>> ledgerFor(const Session& s, const std::string& txId) const;

private:
    Database& db_;
    AccessControl& access_;
};

} // namespace wallet
