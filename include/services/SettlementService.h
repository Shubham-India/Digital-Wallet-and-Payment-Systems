#pragma once
#include "domain/Transaction.h"
#include "infrastructure/Clock.h"
#include "infrastructure/IdGenerator.h"
#include "repositories/Repositories.h"

namespace wallet {

// Moves money for one transaction and posts the double-entry ledger lines.
// Atomic at application level: both wallets are changed on COPIES first; only when every
// check passed are the wallets saved and the ledger appended. Any failure leaves all
// persisted financial state untouched.
class SettlementService {
public:
    SettlementService(IWalletRepository& wallets, ILedgerRepository& ledger, IdGenerator& ids, const IClock& clock)
        : wallets_(wallets), ledger_(ledger), ids_(ids), clock_(clock) {}

    Result<void> settle(const Transaction& tx);

    // Invariant check: credits - debits of a wallet's ledger lines == its balance.
    bool reconciles(const std::string& walletId) const;
    // Invariant check: a transaction's debits == its credits.
    bool balanced(const std::string& txId) const;

private:
    IWalletRepository& wallets_;
    ILedgerRepository& ledger_;
    IdGenerator& ids_;
    const IClock& clock_;
};

} // namespace wallet
