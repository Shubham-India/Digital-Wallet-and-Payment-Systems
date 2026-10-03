#pragma once
#include <unordered_map>

#include "repositories/Repositories.h"

namespace wallet {

// In-memory implementations. They double as the working set of the JSON repositories,
// which add load/flush on top (see JsonDatabase).
class InMemoryUserRepository : public IUserRepository {
public:
    Result<void> add(std::shared_ptr<User> user) override;
    void save(const std::shared_ptr<User>& user) override;
    std::shared_ptr<User> findById(const std::string& id) const override;
    std::shared_ptr<User> findByEmail(const std::string& email) const override;
    std::shared_ptr<Merchant> findMerchantByPaymentId(const std::string& paymentId) const override;
    std::vector<std::shared_ptr<User>> all() const override { return ordered_; }
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
    static std::string normalizeEmail(std::string email);
private:
    std::vector<std::shared_ptr<User>> ordered_;
    std::unordered_map<std::string, std::shared_ptr<User>> byId_, byEmail_;  // O(1) average lookup
    std::unordered_map<std::string, std::shared_ptr<Merchant>> byPaymentId_;
    bool dirty_ = false;
};

class InMemoryWalletRepository : public IWalletRepository {
public:
    void save(const Wallet& wallet) override;
    std::optional<Wallet> find(const std::string& id) const override;
    std::vector<Wallet> all() const override;
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
private:
    std::vector<Wallet> wallets_;
    std::unordered_map<std::string, std::size_t> index_;
    bool dirty_ = false;
};

// Transactions are kept in a vector ordered by createdAt, with hash indexes for id,
// idempotency key and user. Time-range queries use binary search on the ordered vector.
class InMemoryTransactionRepository : public ITransactionRepository {
public:
    void save(const Transaction& tx) override;
    std::optional<Transaction> findById(const std::string& id) const override;
    std::optional<Transaction> findByScopedKey(const std::string& scopedKey) const override;
    std::vector<Transaction> forUserSince(const std::string& userId, Timestamp since) const override;
    std::vector<Transaction> search(const TransactionFilter& f) const override;
    std::vector<Transaction> all() const override { return txs_; }
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
private:
    void rebuildIndexes();
    void indexUsers(const Transaction& tx, std::size_t pos);
    std::vector<Transaction> txs_;
    std::unordered_map<std::string, std::size_t> byId_, byKey_;
    std::unordered_map<std::string, std::vector<std::size_t>> byUser_;  // positions, ascending time
    bool dirty_ = false;
};

class InMemoryLedgerRepository : public ILedgerRepository {
public:
    void append(const LedgerEntry& entry) override;
    std::vector<LedgerEntry> forWallet(const std::string& walletId) const override;
    std::vector<LedgerEntry> forTransaction(const std::string& txId) const override;
    std::vector<LedgerEntry> all() const override { return entries_; }
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
private:
    std::vector<LedgerEntry> entries_;
    std::unordered_map<std::string, std::vector<std::size_t>> byWallet_, byTx_;
    bool dirty_ = false;
};

class InMemoryRefundRepository : public IRefundRepository {
public:
    void save(const RefundRequest& r) override;
    std::optional<RefundRequest> find(const std::string& id) const override;
    std::vector<RefundRequest> all() const override { return items_; }
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
private:
    std::vector<RefundRequest> items_;
    std::unordered_map<std::string, std::size_t> index_;
    bool dirty_ = false;
};

class InMemoryAuditRepository : public IAuditRepository {
public:
    void append(const AuditEvent& e) override { events_.push_back(e); dirty_ = true; }
    std::vector<AuditEvent> all() const override { return events_; }
    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
private:
    std::vector<AuditEvent> events_;
    bool dirty_ = false;
};

class InMemoryDatabase : public Database {
public:
    IUserRepository& users() override { return users_; }
    IWalletRepository& wallets() override { return wallets_; }
    ITransactionRepository& transactions() override { return txs_; }
    ILedgerRepository& ledger() override { return ledger_; }
    IRefundRepository& refunds() override { return refunds_; }
    IAuditRepository& audit() override { return audit_; }
    Result<void> commit() override { return {}; }
protected:
    InMemoryUserRepository users_;
    InMemoryWalletRepository wallets_;
    InMemoryTransactionRepository txs_;
    InMemoryLedgerRepository ledger_;
    InMemoryRefundRepository refunds_;
    InMemoryAuditRepository audit_;
};

} // namespace wallet
