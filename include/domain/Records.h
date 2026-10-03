#pragma once
// Small immutable-ish record types: ledger postings, refund requests, audit events.
#include <map>
#include <string>

#include "domain/Money.h"
#include "utils/TimeUtil.h"

namespace wallet {

enum class EntryDirection { Debit, Credit };  // from the wallet's point of view: Debit = balance goes down
const char* toString(EntryDirection d);

// One posting in the ledger. A successful money movement produces exactly two entries
// (a debit and a credit of the same amount), so the ledger always balances.
struct LedgerEntry {
    std::string id;
    std::string transactionId;
    std::string walletId;  // may be kExternalWallet
    EntryDirection direction = EntryDirection::Debit;
    Money amount;
    Money balanceAfter;  // wallet balance after the posting (zero for the external side)
    Timestamp timestamp = 0;
};

enum class RefundRequestStatus { Pending, Approved, Rejected };
const char* toString(RefundRequestStatus s);

// A customer's ask for money back; the merchant approves (-> a REFUND transaction) or rejects.
struct RefundRequest {
    std::string id;
    std::string transactionId;  // original payment
    std::string requesterUserId;
    std::string merchantUserId;
    Money amount;
    std::string reason;
    RefundRequestStatus status = RefundRequestStatus::Pending;
    std::string refundTransactionId;
    Timestamp createdAt = 0;
    Timestamp resolvedAt = 0;
};

// Security/audit trail: who did what to what, and with what result.
struct AuditEvent {
    std::string id;
    Timestamp timestamp = 0;
    std::string actor;   // user id or "SYSTEM"
    std::string action;  // e.g. LOGIN, TRANSFER, REFUND, FREEZE_WALLET
    std::string target;  // entity affected
    std::string result;  // SUCCESS | FAILURE | SUSPICIOUS | DENIED
    std::map<std::string, std::string> metadata;
};

} // namespace wallet
