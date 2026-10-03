#pragma once
// Repository interfaces (dependency inversion): services depend on these abstractions and
// never know whether data lives in memory, JSON files, or SQLite.
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "domain/Records.h"
#include "domain/Result.h"
#include "domain/Transaction.h"
#include "domain/User.h"
#include "domain/Wallet.h"

namespace wallet {

class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    virtual Result<void> add(std::shared_ptr<User> user) = 0;  // fails on duplicate id/email/paymentId
    virtual void save(const std::shared_ptr<User>& user) = 0;   // call after mutating a stored user
    virtual std::shared_ptr<User> findById(const std::string& id) const = 0;
    virtual std::shared_ptr<User> findByEmail(const std::string& email) const = 0;
    virtual std::shared_ptr<Merchant> findMerchantByPaymentId(const std::string& paymentId) const = 0;
    virtual std::vector<std::shared_ptr<User>> all() const = 0;
};

class IWalletRepository {
public:
    virtual ~IWalletRepository() = default;
    virtual void save(const Wallet& wallet) = 0;  // upsert; wallets are handed out as copies
    virtual std::optional<Wallet> find(const std::string& id) const = 0;
    virtual std::vector<Wallet> all() const = 0;
};

struct TransactionFilter {
    std::optional<std::string> id, userId, merchantId;
    std::optional<TxType> type;
    std::optional<TxStatus> status;
    std::optional<Timestamp> from, to;  // inclusive createdAt range
    std::optional<Money> minAmount, maxAmount;
};

class ITransactionRepository {
public:
    virtual ~ITransactionRepository() = default;
    virtual void save(const Transaction& tx) = 0;  // upsert by id
    virtual std::optional<Transaction> findById(const std::string& id) const = 0;
    virtual std::optional<Transaction> findByScopedKey(const std::string& scopedKey) const = 0;
    virtual std::vector<Transaction> forUserSince(const std::string& userId, Timestamp since) const = 0;
    virtual std::vector<Transaction> search(const TransactionFilter& f) const = 0;  // oldest first
    virtual std::vector<Transaction> all() const = 0;
};

class ILedgerRepository {
public:
    virtual ~ILedgerRepository() = default;
    virtual void append(const LedgerEntry& entry) = 0;
    virtual std::vector<LedgerEntry> forWallet(const std::string& walletId) const = 0;
    virtual std::vector<LedgerEntry> forTransaction(const std::string& txId) const = 0;
    virtual std::vector<LedgerEntry> all() const = 0;
};

class IRefundRepository {
public:
    virtual ~IRefundRepository() = default;
    virtual void save(const RefundRequest& r) = 0;
    virtual std::optional<RefundRequest> find(const std::string& id) const = 0;
    virtual std::vector<RefundRequest> all() const = 0;
};

class IAuditRepository {
public:
    virtual ~IAuditRepository() = default;
    virtual void append(const AuditEvent& e) = 0;
    virtual std::vector<AuditEvent> all() const = 0;
};

// One handle to every repository plus an application-level commit point. Services call
// commit() once at the end of an operation, so a payment hits disk as one unit.
class Database {
public:
    virtual ~Database() = default;
    virtual IUserRepository& users() = 0;
    virtual IWalletRepository& wallets() = 0;
    virtual ITransactionRepository& transactions() = 0;
    virtual ILedgerRepository& ledger() = 0;
    virtual IRefundRepository& refunds() = 0;
    virtual IAuditRepository& audit() = 0;
    virtual Result<void> commit() = 0;
};

} // namespace wallet
