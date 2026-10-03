#include "services/QueryService.h"

#include <algorithm>

namespace wallet {

Result<Wallet> QueryService::myWallet(const Session& s) const {
    if (auto a = access_.authorize(s, Permission::ViewOwnWallet); !a) return fail(a.error());
    auto user = db_.users().findById(s.userId);
    if (!user) return fail(ErrorCode::UserNotFound, "Unknown user");
    auto w = db_.wallets().find(user->walletId());
    if (!w) return fail(ErrorCode::WalletNotFound, "No wallet");
    return *w;
}

Result<std::vector<Transaction>> QueryService::search(const Session& s, TransactionFilter filter) const {
    if (s.role == Role::Admin) {
        if (auto a = access_.authorize(s, Permission::ViewAllTransactions); !a) return fail(a.error());
    } else {
        if (auto a = access_.authorize(s, Permission::ViewOwnTransactions); !a) return fail(a.error());
        filter.userId = s.userId;  // overrides whatever the caller asked for
    }
    auto result = db_.transactions().search(filter);
    std::stable_sort(result.begin(), result.end(),
                     [](const Transaction& a, const Transaction& b) { return a.createdAt() > b.createdAt(); });
    return result;
}

Result<Transaction> QueryService::details(const Session& s, const std::string& txId) const {
    TransactionFilter f;
    f.id = txId;
    auto found = search(s, f);
    if (!found) return fail(found.error());
    if (found.value().empty()) return fail(ErrorCode::TransactionNotFound, "Transaction not found");
    return found.value().front();
}

Result<std::vector<LedgerEntry>> QueryService::ledgerFor(const Session& s, const std::string& txId) const {
    auto tx = details(s, txId);
    if (!tx) return fail(tx.error());
    return db_.ledger().forTransaction(txId);
}

} // namespace wallet
