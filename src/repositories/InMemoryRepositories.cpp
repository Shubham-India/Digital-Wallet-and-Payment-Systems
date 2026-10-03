#include "repositories/InMemoryRepositories.h"

#include <algorithm>
#include <cctype>

namespace wallet {

// ---------- users ----------
std::string InMemoryUserRepository::normalizeEmail(std::string email) {
    std::transform(email.begin(), email.end(), email.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return email;
}

Result<void> InMemoryUserRepository::add(std::shared_ptr<User> user) {
    std::string email = normalizeEmail(user->email());
    if (byId_.count(user->id())) return fail(ErrorCode::DuplicateUser, "User id already exists");
    if (byEmail_.count(email)) return fail(ErrorCode::DuplicateUser, "Email is already registered");
    if (auto m = std::dynamic_pointer_cast<Merchant>(user)) {
        if (byPaymentId_.count(m->paymentId())) return fail(ErrorCode::DuplicateUser, "Merchant payment id already exists");
        byPaymentId_[m->paymentId()] = m;
    }
    byId_[user->id()] = user;
    byEmail_[email] = user;
    ordered_.push_back(std::move(user));
    dirty_ = true;
    return {};
}

void InMemoryUserRepository::save(const std::shared_ptr<User>& user) {
    if (byId_.count(user->id())) dirty_ = true;  // users are shared pointers: mutations are already visible
}

std::shared_ptr<User> InMemoryUserRepository::findById(const std::string& id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}
std::shared_ptr<User> InMemoryUserRepository::findByEmail(const std::string& email) const {
    auto it = byEmail_.find(normalizeEmail(email));
    return it == byEmail_.end() ? nullptr : it->second;
}
std::shared_ptr<Merchant> InMemoryUserRepository::findMerchantByPaymentId(const std::string& paymentId) const {
    auto it = byPaymentId_.find(paymentId);
    return it == byPaymentId_.end() ? nullptr : it->second;
}

// ---------- wallets ----------
void InMemoryWalletRepository::save(const Wallet& wallet) {
    auto it = index_.find(wallet.id());
    if (it == index_.end()) {
        index_[wallet.id()] = wallets_.size();
        wallets_.push_back(wallet);
    } else {
        wallets_[it->second] = wallet;
    }
    dirty_ = true;
}
std::optional<Wallet> InMemoryWalletRepository::find(const std::string& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return wallets_[it->second];
}
std::vector<Wallet> InMemoryWalletRepository::all() const { return wallets_; }

// ---------- transactions ----------
void InMemoryTransactionRepository::indexUsers(const Transaction& tx, std::size_t pos) {
    const auto& d = tx.data();
    if (!d.payerUserId.empty()) byUser_[d.payerUserId].push_back(pos);
    if (!d.payeeUserId.empty() && d.payeeUserId != d.payerUserId) byUser_[d.payeeUserId].push_back(pos);
}

void InMemoryTransactionRepository::rebuildIndexes() {
    byId_.clear(); byKey_.clear(); byUser_.clear();
    for (std::size_t i = 0; i < txs_.size(); ++i) {
        byId_[txs_[i].id()] = i;
        if (!txs_[i].data().scopedKey.empty()) byKey_[txs_[i].data().scopedKey] = i;
        indexUsers(txs_[i], i);
    }
}

void InMemoryTransactionRepository::save(const Transaction& tx) {
    dirty_ = true;
    auto it = byId_.find(tx.id());
    if (it != byId_.end()) {
        // Routing (payer/payee) is fixed at creation except while still pending; re-index on change.
        const auto& old = txs_[it->second].data();
        bool routingChanged = old.payerUserId != tx.data().payerUserId || old.payeeUserId != tx.data().payeeUserId;
        txs_[it->second] = tx;
        if (routingChanged) rebuildIndexes();
        return;
    }
    if (txs_.empty() || tx.createdAt() >= txs_.back().createdAt()) {
        std::size_t pos = txs_.size();
        txs_.push_back(tx);
        byId_[tx.id()] = pos;
        if (!tx.data().scopedKey.empty()) byKey_[tx.data().scopedKey] = pos;
        indexUsers(tx, pos);
    } else {  // out-of-order timestamp (e.g. clock adjusted): keep the vector sorted
        auto pos = std::upper_bound(txs_.begin(), txs_.end(), tx.createdAt(),
                                    [](Timestamp t, const Transaction& x) { return t < x.createdAt(); });
        txs_.insert(pos, tx);
        rebuildIndexes();
    }
}

std::optional<Transaction> InMemoryTransactionRepository::findById(const std::string& id) const {
    auto it = byId_.find(id);
    if (it == byId_.end()) return std::nullopt;
    return txs_[it->second];
}

std::optional<Transaction> InMemoryTransactionRepository::findByScopedKey(const std::string& key) const {
    auto it = byKey_.find(key);
    if (it == byKey_.end()) return std::nullopt;
    return txs_[it->second];
}

std::vector<Transaction> InMemoryTransactionRepository::forUserSince(const std::string& userId, Timestamp since) const {
    std::vector<Transaction> out;
    auto it = byUser_.find(userId);
    if (it == byUser_.end()) return out;
    const auto& positions = it->second;
    // Positions are in ascending time order: binary-search the first one inside the window.
    auto first = std::partition_point(positions.begin(), positions.end(),
                                      [&](std::size_t p) { return txs_[p].createdAt() < since; });
    for (; first != positions.end(); ++first) out.push_back(txs_[*first]);
    return out;
}

std::vector<Transaction> InMemoryTransactionRepository::search(const TransactionFilter& f) const {
    std::vector<Transaction> out;
    auto matches = [&](const Transaction& t) {
        const auto& d = t.data();
        if (f.id && d.id != *f.id) return false;
        if (f.userId && d.payerUserId != *f.userId && d.payeeUserId != *f.userId) return false;
        if (f.merchantId && d.merchantId != *f.merchantId) return false;
        if (f.type && d.type != *f.type) return false;
        if (f.status && d.status != *f.status) return false;
        if (f.from && d.createdAt < *f.from) return false;
        if (f.to && d.createdAt > *f.to) return false;
        if (f.minAmount && d.amount < *f.minAmount) return false;
        if (f.maxAmount && d.amount > *f.maxAmount) return false;
        return true;
    };
    if (f.id) {  // O(1) direct hit
        if (auto t = findById(*f.id); t && matches(*t)) out.push_back(*t);
        return out;
    }
    if (f.userId) {  // narrow to the user's index first: O(log m + k)
        for (auto& t : forUserSince(*f.userId, f.from.value_or(0)))
            if (matches(t)) out.push_back(t);
        return out;
    }
    // Otherwise binary-search the date range on the time-ordered vector.
    auto lo = txs_.begin(), hi = txs_.end();
    if (f.from) lo = std::lower_bound(txs_.begin(), txs_.end(), *f.from, [](const Transaction& t, Timestamp v) { return t.createdAt() < v; });
    if (f.to) hi = std::upper_bound(txs_.begin(), txs_.end(), *f.to, [](Timestamp v, const Transaction& t) { return v < t.createdAt(); });
    for (auto it = lo; it < hi; ++it)
        if (matches(*it)) out.push_back(*it);
    return out;
}

// ---------- ledger ----------
void InMemoryLedgerRepository::append(const LedgerEntry& entry) {
    byWallet_[entry.walletId].push_back(entries_.size());
    byTx_[entry.transactionId].push_back(entries_.size());
    entries_.push_back(entry);
    dirty_ = true;
}
std::vector<LedgerEntry> InMemoryLedgerRepository::forWallet(const std::string& walletId) const {
    std::vector<LedgerEntry> out;
    if (auto it = byWallet_.find(walletId); it != byWallet_.end())
        for (auto p : it->second) out.push_back(entries_[p]);
    return out;
}
std::vector<LedgerEntry> InMemoryLedgerRepository::forTransaction(const std::string& txId) const {
    std::vector<LedgerEntry> out;
    if (auto it = byTx_.find(txId); it != byTx_.end())
        for (auto p : it->second) out.push_back(entries_[p]);
    return out;
}

// ---------- refunds ----------
void InMemoryRefundRepository::save(const RefundRequest& r) {
    auto it = index_.find(r.id);
    if (it == index_.end()) {
        index_[r.id] = items_.size();
        items_.push_back(r);
    } else {
        items_[it->second] = r;
    }
    dirty_ = true;
}
std::optional<RefundRequest> InMemoryRefundRepository::find(const std::string& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return items_[it->second];
}

} // namespace wallet
