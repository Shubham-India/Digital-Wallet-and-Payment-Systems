#include "repositories/JsonDatabase.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "infrastructure/Json.h"

namespace wallet {

namespace {

// ---------- enum parsing ----------
template <class E, class F>
bool parseEnum(const std::string& s, std::initializer_list<E> values, F name, E& out) {
    for (E v : values)
        if (s == name(v)) { out = v; return true; }
    return false;
}

Money moneyOf(const Json& j, const char* key, Currency c) {
    std::int64_t v = j.integer(key, 0);
    return Money::fromMinor(v < 0 ? 0 : v, c);  // throws WalletException if absurdly large -> caught by load()
}

// ---------- users ----------
Json toJson(const User& u) {
    Json::Object o;
    o["id"] = u.id(); o["role"] = std::string(toString(u.role())); o["name"] = u.name();
    o["email"] = u.email(); o["phone"] = u.phone(); o["createdAt"] = u.createdAt();
    o["status"] = std::string(toString(u.status())); o["walletId"] = u.walletId();
    o["salt"] = u.credentials().salt(); o["hash"] = u.credentials().hash();
    o["failedAttempts"] = u.credentials().failedAttempts();
    if (auto c = dynamic_cast<const Customer*>(&u)) o["tier"] = std::string(toString(c->tier()));
    if (auto m = dynamic_cast<const Merchant*>(&u)) {
        o["businessName"] = m->businessName(); o["category"] = m->category(); o["paymentId"] = m->paymentId();
    }
    return Json(std::move(o));
}

Result<std::shared_ptr<User>> userFromJson(const Json& j) {
    std::string id = j.str("id"), roleText = j.str("role");
    if (id.empty() || j.str("email").empty()) return fail(ErrorCode::PersistenceFailure, "User record missing id/email");
    Role role; AccountStatus status; AccountTier tier = AccountTier::Basic;
    if (!parseEnum<Role>(roleText, {Role::Customer, Role::Merchant, Role::Admin}, [](Role r) { return toString(r); }, role))
        return fail(ErrorCode::PersistenceFailure, "Unknown role " + roleText);
    if (!parseEnum<AccountStatus>(j.str("status"), {AccountStatus::Active, AccountStatus::Suspended, AccountStatus::Locked},
                                  [](AccountStatus s) { return toString(s); }, status))
        return fail(ErrorCode::PersistenceFailure, "Unknown account status");
    parseEnum<AccountTier>(j.str("tier"), {AccountTier::Basic, AccountTier::Premium}, [](AccountTier t) { return toString(t); }, tier);
    Credentials cred(j.str("salt"), j.str("hash"), static_cast<int>(j.integer("failedAttempts")));
    std::shared_ptr<User> u;
    switch (role) {
        case Role::Customer:
            u = std::make_shared<Customer>(id, j.str("name"), j.str("email"), j.str("phone"), j.integer("createdAt"), cred, tier, status, j.str("walletId"));
            break;
        case Role::Merchant:
            u = std::make_shared<Merchant>(id, j.str("name"), j.str("email"), j.str("phone"), j.integer("createdAt"), cred,
                                           j.str("businessName"), j.str("category"), j.str("paymentId"), status, j.str("walletId"));
            break;
        case Role::Admin:
            u = std::make_shared<Admin>(id, j.str("name"), j.str("email"), j.str("phone"), j.integer("createdAt"), cred, status, j.str("walletId"));
            break;
    }
    return u;
}

// ---------- wallets ----------
Json toJson(const Wallet& w) {
    Json::Object o;
    o["id"] = w.id(); o["ownerId"] = w.ownerId(); o["balance"] = w.balance().minor();
    o["currency"] = std::string(toString(w.currency())); o["status"] = std::string(toString(w.status()));
    return Json(std::move(o));
}

Currency currencyOf(const std::string& s) { return s == "USD" ? Currency::USD : Currency::INR; }

Result<Wallet> walletFromJson(const Json& j) {
    WalletStatus st;
    if (j.str("id").empty() || !parseEnum<WalletStatus>(j.str("status"), {WalletStatus::Active, WalletStatus::Frozen, WalletStatus::Closed},
                                                         [](WalletStatus s) { return toString(s); }, st))
        return fail(ErrorCode::PersistenceFailure, "Bad wallet record");
    Currency c = currencyOf(j.str("currency"));
    return Wallet::restore(j.str("id"), j.str("ownerId"), moneyOf(j, "balance", c), st);
}

// ---------- transactions ----------
Json toJson(const Transaction& t) {
    const auto& d = t.data();
    Json::Object o, meta;
    o["id"] = d.id; o["scopedKey"] = d.scopedKey; o["type"] = std::string(toString(d.type));
    o["status"] = std::string(toString(d.status));
    o["payerUserId"] = d.payerUserId; o["payeeUserId"] = d.payeeUserId;
    o["payerWalletId"] = d.payerWalletId; o["payeeWalletId"] = d.payeeWalletId; o["merchantId"] = d.merchantId;
    o["amount"] = d.amount.minor(); o["refunded"] = d.refundedAmount.minor();
    o["currency"] = std::string(toString(d.amount.currency()));
    o["method"] = d.method; o["reference"] = d.reference; o["description"] = d.description;
    o["failureCode"] = d.failureCode; o["failureReason"] = d.failureReason; o["linkedTxId"] = d.linkedTxId;
    o["createdAt"] = d.createdAt; o["updatedAt"] = d.updatedAt;
    for (const auto& [k, v] : d.metadata) meta[k] = v;
    o["metadata"] = Json(std::move(meta));
    return Json(std::move(o));
}

Result<Transaction> transactionFromJson(const Json& j) {
    TransactionData d;
    d.id = j.str("id");
    if (d.id.empty() || !parseTxType(j.str("type"), d.type) || !parseTxStatus(j.str("status"), d.status))
        return fail(ErrorCode::PersistenceFailure, "Bad transaction record");
    Currency c = currencyOf(j.str("currency"));
    d.scopedKey = j.str("scopedKey");
    d.payerUserId = j.str("payerUserId"); d.payeeUserId = j.str("payeeUserId");
    d.payerWalletId = j.str("payerWalletId"); d.payeeWalletId = j.str("payeeWalletId"); d.merchantId = j.str("merchantId");
    d.amount = moneyOf(j, "amount", c); d.refundedAmount = moneyOf(j, "refunded", c);
    d.method = j.str("method"); d.reference = j.str("reference"); d.description = j.str("description");
    d.failureCode = j.str("failureCode"); d.failureReason = j.str("failureReason"); d.linkedTxId = j.str("linkedTxId");
    d.createdAt = j.integer("createdAt"); d.updatedAt = j.integer("updatedAt");
    if (const Json* m = j.find("metadata"); m && m->isObject())
        for (const auto& [k, v] : m->object())
            if (v.type() == Json::Type::String) d.metadata[k] = v.asString();
    return Transaction(std::move(d));
}

// ---------- ledger ----------
Json toJson(const LedgerEntry& e) {
    Json::Object o;
    o["id"] = e.id; o["txId"] = e.transactionId; o["walletId"] = e.walletId;
    o["direction"] = std::string(toString(e.direction)); o["amount"] = e.amount.minor();
    o["balanceAfter"] = e.balanceAfter.minor(); o["currency"] = std::string(toString(e.amount.currency()));
    o["timestamp"] = e.timestamp;
    return Json(std::move(o));
}

Result<LedgerEntry> ledgerFromJson(const Json& j) {
    LedgerEntry e;
    if (!parseEnum<EntryDirection>(j.str("direction"), {EntryDirection::Debit, EntryDirection::Credit},
                                   [](EntryDirection d) { return toString(d); }, e.direction) || j.str("id").empty())
        return fail(ErrorCode::PersistenceFailure, "Bad ledger record");
    Currency c = currencyOf(j.str("currency"));
    e.id = j.str("id"); e.transactionId = j.str("txId"); e.walletId = j.str("walletId");
    e.amount = moneyOf(j, "amount", c); e.balanceAfter = moneyOf(j, "balanceAfter", c); e.timestamp = j.integer("timestamp");
    return e;
}

// ---------- refunds ----------
Json toJson(const RefundRequest& r) {
    Json::Object o;
    o["id"] = r.id; o["txId"] = r.transactionId; o["requester"] = r.requesterUserId; o["merchant"] = r.merchantUserId;
    o["amount"] = r.amount.minor(); o["reason"] = r.reason; o["status"] = std::string(toString(r.status));
    o["refundTxId"] = r.refundTransactionId; o["createdAt"] = r.createdAt; o["resolvedAt"] = r.resolvedAt;
    return Json(std::move(o));
}

Result<RefundRequest> refundFromJson(const Json& j) {
    RefundRequest r;
    if (j.str("id").empty() || !parseEnum<RefundRequestStatus>(j.str("status"),
            {RefundRequestStatus::Pending, RefundRequestStatus::Approved, RefundRequestStatus::Rejected},
            [](RefundRequestStatus s) { return toString(s); }, r.status))
        return fail(ErrorCode::PersistenceFailure, "Bad refund record");
    r.id = j.str("id"); r.transactionId = j.str("txId"); r.requesterUserId = j.str("requester");
    r.merchantUserId = j.str("merchant"); r.amount = moneyOf(j, "amount", Currency::INR); r.reason = j.str("reason");
    r.refundTransactionId = j.str("refundTxId"); r.createdAt = j.integer("createdAt"); r.resolvedAt = j.integer("resolvedAt");
    return r;
}

// ---------- audit ----------
Json toJson(const AuditEvent& e) {
    Json::Object o, meta;
    o["id"] = e.id; o["timestamp"] = e.timestamp; o["actor"] = e.actor; o["action"] = e.action;
    o["target"] = e.target; o["result"] = e.result;
    for (const auto& [k, v] : e.metadata) meta[k] = v;
    o["metadata"] = Json(std::move(meta));
    return Json(std::move(o));
}

Result<AuditEvent> auditFromJson(const Json& j) {
    AuditEvent e;
    if (j.str("id").empty()) return fail(ErrorCode::PersistenceFailure, "Bad audit record");
    e.id = j.str("id"); e.timestamp = j.integer("timestamp"); e.actor = j.str("actor"); e.action = j.str("action");
    e.target = j.str("target"); e.result = j.str("result");
    if (const Json* m = j.find("metadata"); m && m->isObject())
        for (const auto& [k, v] : m->object())
            if (v.type() == Json::Type::String) e.metadata[k] = v.asString();
    return e;
}

// ---------- file helpers ----------
Result<Json::Array> readArray(const std::string& path) {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) return Json::Array{};
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::PersistenceFailure, "Cannot read " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = Json::parse(ss.str());
    if (!parsed) return fail(ErrorCode::PersistenceFailure, path + ": " + parsed.error().message);
    if (!parsed.value().isArray()) return fail(ErrorCode::PersistenceFailure, path + ": expected a JSON array");
    return parsed.value().array();
}

Result<void> writeArray(const std::string& path, const Json::Array& items) {
    namespace fs = std::filesystem;
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(ErrorCode::PersistenceFailure, "Cannot write " + tmp);
        out << Json(items).dump(1) << '\n';
        out.flush();
        if (!out) return fail(ErrorCode::PersistenceFailure, "Write failed for " + tmp);
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) return fail(ErrorCode::PersistenceFailure, "Cannot replace " + path + ": " + ec.message());
    return {};
}

template <class Repo, class Item, class FromJson, class Add>
Result<void> loadInto(const std::string& path, Repo& repo, FromJson from, Add add) {
    auto arr = readArray(path);
    if (!arr) return fail(arr.error());
    try {
        for (const Json& j : arr.value()) {
            auto item = from(j);
            if (!item) return fail(ErrorCode::PersistenceFailure, path + ": " + item.error().message);
            auto r = add(repo, std::move(item).value());
            if (!r) return fail(ErrorCode::PersistenceFailure, path + ": " + r.error().message);
        }
    } catch (const WalletException& e) {
        return fail(ErrorCode::PersistenceFailure, path + ": " + e.what());
    }
    repo.clearDirty();
    return {};
}

}  // namespace

Result<void> JsonDatabase::load() {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) return fail(ErrorCode::PersistenceFailure, "Cannot create data directory " + dir_ + ": " + ec.message());

    if (auto r = loadInto<InMemoryUserRepository, std::shared_ptr<User>>(path("users"), users_, userFromJson,
            [](auto& repo, std::shared_ptr<User> u) { return repo.add(std::move(u)); }); !r) return r;
    if (auto r = loadInto<InMemoryWalletRepository, Wallet>(path("wallets"), wallets_, walletFromJson,
            [](auto& repo, Wallet w) -> Result<void> { repo.save(w); return {}; }); !r) return r;
    if (auto r = loadInto<InMemoryTransactionRepository, Transaction>(path("transactions"), txs_, transactionFromJson,
            [](auto& repo, Transaction t) -> Result<void> { repo.save(t); return {}; }); !r) return r;
    if (auto r = loadInto<InMemoryLedgerRepository, LedgerEntry>(path("ledger"), ledger_, ledgerFromJson,
            [](auto& repo, LedgerEntry e) -> Result<void> { repo.append(e); return {}; }); !r) return r;
    if (auto r = loadInto<InMemoryRefundRepository, RefundRequest>(path("refunds"), refunds_, refundFromJson,
            [](auto& repo, RefundRequest x) -> Result<void> { repo.save(x); return {}; }); !r) return r;
    if (auto r = loadInto<InMemoryAuditRepository, AuditEvent>(path("audit"), audit_, auditFromJson,
            [](auto& repo, AuditEvent e) -> Result<void> { repo.append(e); return {}; }); !r) return r;
    return {};
}

Result<void> JsonDatabase::commit() {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);

    auto flush = [&](auto& repo, const char* name, auto items) -> Result<void> {
        if (!repo.dirty()) return {};
        Json::Array arr;
        for (const auto& item : items) arr.push_back(toJson(*item));
        if (auto r = writeArray(path(name), arr); !r) return r;
        repo.clearDirty();
        return {};
    };
    auto ptrs = [](const auto& vec) {
        std::vector<const std::decay_t<decltype(vec[0])>*> out;
        for (const auto& v : vec) out.push_back(&v);
        return out;
    };
    auto userPtrs = [](const std::vector<std::shared_ptr<User>>& vec) {
        std::vector<const User*> out;
        for (const auto& v : vec) out.push_back(v.get());
        return out;
    };

    if (auto r = flush(users_, "users", userPtrs(users_.all())); !r) return r;
    if (auto r = flush(wallets_, "wallets", ptrs(wallets_.all())); !r) return r;
    if (auto r = flush(txs_, "transactions", ptrs(txs_.all())); !r) return r;
    if (auto r = flush(ledger_, "ledger", ptrs(ledger_.all())); !r) return r;
    if (auto r = flush(refunds_, "refunds", ptrs(refunds_.all())); !r) return r;
    if (auto r = flush(audit_, "audit", ptrs(audit_.all())); !r) return r;
    return {};
}

} // namespace wallet
