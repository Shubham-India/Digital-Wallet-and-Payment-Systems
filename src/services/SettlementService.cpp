#include "services/SettlementService.h"

#include <optional>

namespace wallet {

Result<void> SettlementService::settle(const Transaction& tx) {
    const auto& d = tx.data();
    if (d.payerWalletId.empty() || d.payeeWalletId.empty() || d.payerWalletId == d.payeeWalletId)
        return fail(ErrorCode::ValidationFailed, "Transaction " + d.id + " has invalid wallet routing");

    std::optional<Wallet> from, to;
    if (d.payerWalletId != kExternalWallet) {
        from = wallets_.find(d.payerWalletId);
        if (!from) return fail(ErrorCode::WalletNotFound, "Payer wallet not found");
    }
    if (d.payeeWalletId != kExternalWallet) {
        to = wallets_.find(d.payeeWalletId);
        if (!to) return fail(ErrorCode::WalletNotFound, "Payee wallet not found");
    }

    // Step 1: apply on copies. Nothing is persisted if either side refuses.
    if (from) if (auto r = from->debit(d.amount); !r) return r;
    if (to) if (auto r = to->credit(d.amount); !r) return r;

    // Step 2: commit to repositories (in-memory operations that cannot fail).
    Timestamp now = clock_.now();
    const Money zero = Money::fromMinor(0, d.amount.currency());
    auto post = [&](const std::string& walletId, EntryDirection dir, const Money& after) {
        LedgerEntry e;
        e.id = ids_.next("LED", 6);
        e.transactionId = d.id;
        e.walletId = walletId;
        e.direction = dir;
        e.amount = d.amount;
        e.balanceAfter = after;
        e.timestamp = now;
        ledger_.append(e);
    };
    post(d.payerWalletId, EntryDirection::Debit, from ? from->balance() : zero);
    post(d.payeeWalletId, EntryDirection::Credit, to ? to->balance() : zero);
    if (from) wallets_.save(*from);
    if (to) wallets_.save(*to);
    return {};
}

bool SettlementService::reconciles(const std::string& walletId) const {
    auto wallet = wallets_.find(walletId);
    if (!wallet) return false;
    std::int64_t net = 0;
    for (const auto& e : ledger_.forWallet(walletId))
        net += e.direction == EntryDirection::Credit ? e.amount.minor() : -e.amount.minor();
    return net == wallet->balance().minor();
}

bool SettlementService::balanced(const std::string& txId) const {
    std::int64_t debits = 0, credits = 0;
    auto entries = ledger_.forTransaction(txId);
    for (const auto& e : entries) (e.direction == EntryDirection::Debit ? debits : credits) += e.amount.minor();
    return !entries.empty() && debits == credits;
}

} // namespace wallet
