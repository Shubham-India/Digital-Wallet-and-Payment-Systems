// Refunds, reports, admin operations, JSON persistence, end-to-end scenarios.
#include <filesystem>
#include <fstream>

#include "TestHelpers.h"
#include "repositories/JsonDatabase.h"

using namespace testenv;

namespace {
struct Shop {
    Env e;
    std::string aliceId, cafeId;
    Session alice, cafe;
    std::string txId;   // Alice's INR 1000 payment to the cafe
    Shop() {
        aliceId = e.addCustomer("Alice", "alice@example.com");
        cafeId = e.addMerchant("Cafe", "cafe@example.com");
        alice = e.login("alice@example.com");
        cafe = e.login("cafe@example.com");
        e.fund(alice, 5000);
        e.tick();
        txId = e.ctx->payments.payMerchant(alice, e.paymentIdOf(cafeId), rupees(1000), e.key(), "order", "ORD1").value().tx.id();
    }
};
}  // namespace

TEST(scenario7_full_refund_returns_money_and_links_transactions) {
    Shop s;
    auto r = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(1000), "cancelled", "rk1");
    CHECK(r.ok());
    CHECK(r.value().type() == TxType::Refund);
    CHECK_EQ(r.value().data().linkedTxId, s.txId);
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(5000).minor());
    CHECK_EQ(s.e.balanceOf(s.cafeId).minor(), 0);
    auto original = s.e.ctx->db->transactions().findById(s.txId).value();
    CHECK(original.status() == TxStatus::Refunded);
    CHECK_EQ(original.refundedAmount().minor(), rupees(1000).minor());
    CHECK(s.e.ctx->settlement.balanced(r.value().id()));
    CHECK_EQ(s.e.ctx->refunds.refundsOf(s.txId).size(), 1u);
}

TEST(partial_refunds_accumulate_and_cannot_exceed_original) {
    Shop s;
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(300), "", "rk1").ok());
    auto orig = s.e.ctx->db->transactions().findById(s.txId).value();
    CHECK(orig.status() == TxStatus::PartiallyRefunded);
    CHECK_EQ(orig.refundable().minor(), rupees(700).minor());
    auto over = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(800), "", "rk2");
    CHECK(over.error().code == ErrorCode::RefundExceedsOriginal);
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(700), "", "rk3").ok());
    CHECK(s.e.ctx->db->transactions().findById(s.txId)->status() == TxStatus::Refunded);
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(5000).minor());
}

TEST(duplicate_refund_is_blocked_by_key_and_by_cap) {
    Shop s;
    auto first = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(1000), "", "same-key");
    auto retry = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(1000), "", "same-key");
    CHECK(first.ok()); CHECK(retry.ok());
    CHECK_EQ(first.value().id(), retry.value().id());                       // replayed, not repeated
    auto again = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(1000), "", "other-key");
    CHECK(!again.ok());                                                     // already fully refunded
    CHECK(again.error().code == ErrorCode::RefundNotAllowed);
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(5000).minor());
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(5), "", "same-key").error().code == ErrorCode::IdempotencyMismatch);
}

TEST(refund_authorization_and_validation) {
    Shop s;
    CHECK(s.e.ctx->refunds.issueRefund(s.alice, s.txId, rupees(10), "", "k").error().code == ErrorCode::Unauthorized);   // customer cannot issue
    s.e.addMerchant("Other", "other@example.com");
    Session other = s.e.login("other@example.com");
    CHECK(s.e.ctx->refunds.issueRefund(other, s.txId, rupees(10), "", "k").error().code == ErrorCode::Unauthorized);       // not their sale
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, "TX-999999", rupees(10), "", "k2").error().code == ErrorCode::TransactionNotFound);
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, Money(), "", "k3").error().code == ErrorCode::InvalidAmount);
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(10), "", "").error().code == ErrorCode::ValidationFailed);
    // plain wallet transfers and top-ups are not refundable
    s.e.addCustomer("Bob", "bob@example.com");
    s.e.tick();
    auto tr = s.e.ctx->payments.transfer(s.alice, "bob@example.com", rupees(10), s.e.key()).value().tx.id();
    CHECK(s.e.ctx->refunds.issueRefund(s.e.admin(), tr, rupees(10), "", "k4").error().code == ErrorCode::RefundNotAllowed);
}

TEST(refund_fails_cleanly_if_merchant_already_spent_the_money) {
    Shop s;
    CHECK(s.e.ctx->payments.withdraw(s.cafe, rupees(900), s.e.key()).ok());      // merchant keeps only 100
    auto r = s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(500), "", "rk");
    CHECK(r.error().code == ErrorCode::InsufficientFunds);
    CHECK(s.e.ctx->db->transactions().findById(s.txId)->status() == TxStatus::Success);   // untouched
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(4000).minor());
    CHECK_EQ(s.e.balanceOf(s.cafeId).minor(), rupees(100).minor());
}

TEST(customer_requests_refund_and_merchant_approves) {
    Shop s;
    auto req = s.e.ctx->refunds.requestRefund(s.alice, s.txId, rupees(400), "wrong item");
    CHECK(req.ok());
    CHECK(s.e.ctx->refunds.requestRefund(s.alice, s.txId, rupees(100), "dup").error().code == ErrorCode::DuplicateRefundRequest);
    CHECK(s.e.ctx->refunds.requestRefund(s.alice, s.txId, rupees(5000), "too much").error().code != ErrorCode::DuplicateRefundRequest);
    CHECK_EQ(s.e.ctx->refunds.requestsFor(s.cafe).size(), 1u);
    CHECK(s.e.ctx->inbox->inboxFor(s.cafeId).back().title == "Refund requested");
    CHECK(s.e.ctx->refunds.approveRequest(s.alice, req.value().id).error().code == ErrorCode::Unauthorized);

    auto refund = s.e.ctx->refunds.approveRequest(s.cafe, req.value().id);
    CHECK(refund.ok());
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(4400).minor());
    CHECK(s.e.ctx->db->refunds().find(req.value().id)->status == RefundRequestStatus::Approved);
    CHECK(!s.e.ctx->refunds.approveRequest(s.cafe, req.value().id).ok());          // already resolved
}

TEST(merchant_can_reject_refund_request) {
    Shop s;
    auto req = s.e.ctx->refunds.requestRefund(s.alice, s.txId, rupees(400), "why not").value();
    CHECK(s.e.ctx->refunds.rejectRequest(s.cafe, req.id).value().status == RefundRequestStatus::Rejected);
    CHECK_EQ(s.e.balanceOf(s.aliceId).minor(), rupees(4000).minor());
    CHECK(s.e.ctx->refunds.requestRefund(s.alice, s.txId, rupees(100), "again").ok());   // a new request is fine now
}

TEST(reports_summarize_system_activity) {
    Shop s;
    s.e.tick();
    (void)s.e.ctx->payments.transfer(s.alice, "ghost@example.com", rupees(10), s.e.key());   // creates a FAILED tx
    CHECK(s.e.ctx->refunds.issueRefund(s.cafe, s.txId, rupees(250), "", "rk").ok());
    Session admin = s.e.admin();
    auto sum = s.e.ctx->reports.summary(admin).value();
    CHECK_EQ(sum.users, 3u);
    CHECK_EQ(sum.merchants, 1u);
    CHECK(sum.failed >= 1u);
    CHECK_EQ(sum.refundedAmount.minor(), rupees(250).minor());
    CHECK_EQ(sum.refundedTransactions, 1u);
    CHECK_EQ(sum.volume.minor(), rupees(1000).minor());
    auto top = s.e.ctx->reports.topMerchants(admin, 3).value();
    CHECK_EQ(top.size(), 1u);
    CHECK_EQ(top[0].businessName, std::string("Cafe Store"));
    auto hi = s.e.ctx->reports.highestTransactions(admin, 2).value();
    CHECK_EQ(hi.size(), 2u);
    CHECK(hi[0].amount() >= hi[1].amount());
    CHECK(!s.e.ctx->reports.dailyCounts(admin).value().empty());
    CHECK(!s.e.ctx->reports.busiestUsers(admin, 1).value().empty());
    CHECK(s.e.ctx->reports.recentTransactions(admin, 2).value().size() == 2u);
    CHECK(s.e.ctx->reports.renderText(admin).value().find("SYSTEM SUMMARY") != std::string::npos);
    auto csv = s.e.ctx->reports.exportCsv(admin).value();
    CHECK(csv.rfind("id,created,type", 0) == 0);
    CHECK(s.e.ctx->reports.summary(s.alice).error().code == ErrorCode::Unauthorized);
}

TEST(admin_operations_are_audited) {
    Shop s;
    Session admin = s.e.admin();
    CHECK(s.e.ctx->admin.freezeWallet(admin, s.aliceId).ok());
    CHECK(s.e.ctx->admin.viewWallet(admin, s.aliceId).value().status() == WalletStatus::Frozen);
    CHECK(s.e.ctx->admin.unfreezeWallet(admin, s.aliceId).ok());
    CHECK(!s.e.ctx->admin.freezeWallet(admin, "USR-9999").ok());
    CHECK(!s.e.ctx->admin.setUserStatus(admin, admin.userId, AccountStatus::Suspended).ok());   // cannot lock yourself out
    auto log = s.e.ctx->admin.auditLog(admin).value();
    bool froze = false, unfroze = false;
    for (const auto& ev : log) { froze |= ev.action == "FREEZE_WALLET"; unfroze |= ev.action == "UNFREEZE_WALLET"; }
    CHECK(froze); CHECK(unfroze);
    CHECK_EQ(s.e.ctx->admin.auditLog(admin, 3).value().size(), 3u);
}

// ---------- persistence ----------
namespace {
std::string freshDir(const std::string& name) {
    std::string dir = "build/test-data/" + name;
    std::filesystem::remove_all(dir);
    return dir;
}
std::unique_ptr<AppContext> open(const std::string& dir, ManualClock& clock) {
    AppOptions o;
    o.dataDir = dir; o.configPath = "does-not-exist.json"; o.fileLogging = false; o.passwordIterations = 10;
    return AppContext::create(o, clock).value();
}
}  // namespace

TEST(persistence_state_survives_restart) {
    std::string dir = freshDir("restart");
    ManualClock clock;
    std::string aliceId, bobId, txId;
    std::size_t txCount, ledgerCount, auditCount;
    {
        auto ctx = open(dir, clock);
        aliceId = ctx->auth.registerCustomer("Alice", "alice@example.com", "9876543210", kPassword).value();
        bobId = ctx->auth.registerCustomer("Bob", "bob@example.com", "", kPassword).value();
        Session alice = ctx->auth.login("alice@example.com", kPassword).value();
        auto m = PaymentMethodFactory::create("BANK_ACCOUNT", "123456789012").value();
        clock.advance(30);
        CHECK(ctx->payments.addMoney(alice, rupees(5000), *m, "top1").ok());
        clock.advance(30);
        txId = ctx->payments.transfer(alice, "bob@example.com", rupees(1250), "xfer1").value().tx.id();
        clock.advance(30);
        CHECK(ctx->payments.transfer(alice, "bob@example.com", rupees(15000 - 14000), "held1").ok());   // 1000, approved
        txCount = ctx->db->transactions().all().size();
        ledgerCount = ctx->db->ledger().all().size();
        auditCount = ctx->db->audit().all().size();
    }
    {
        auto ctx = open(dir, clock);
        auto alice = ctx->db->users().findByEmail("alice@example.com");
        CHECK(alice != nullptr);
        CHECK_EQ(alice->id(), aliceId);
        CHECK_EQ(ctx->db->transactions().all().size(), txCount);
        CHECK_EQ(ctx->db->ledger().all().size(), ledgerCount);
        CHECK(ctx->db->audit().all().size() >= auditCount);
        CHECK_EQ(ctx->db->wallets().find(alice->walletId())->balance().minor(), rupees(5000 - 1250 - 1000).minor());
        CHECK_EQ(ctx->db->wallets().find(ctx->db->users().findById(bobId)->walletId())->balance().minor(), rupees(2250).minor());
        CHECK(ctx->db->transactions().findById(txId).has_value());
        for (const auto& w : ctx->db->wallets().all()) CHECK(ctx->settlement.reconciles(w.id()));
        // password hash survived: login works with the original password, fails with a wrong one
        CHECK(ctx->auth.login("alice@example.com", kPassword).ok());
        CHECK(!ctx->auth.login("alice@example.com", "WrongPass1").ok());
        // ids continue instead of colliding
        auto carol = ctx->auth.registerCustomer("Carol", "carol@example.com", "", kPassword).value();
        CHECK(carol != aliceId && carol != bobId);
        // the idempotency record persisted too: replaying the old key does not charge again
        Session a2 = ctx->auth.login("alice@example.com", kPassword).value();
        auto replay = ctx->payments.transfer(a2, "bob@example.com", rupees(1250), "xfer1");
        CHECK(replay.ok());
        CHECK(replay.value().duplicate);
        CHECK_EQ(ctx->db->wallets().find(alice->walletId())->balance().minor(), rupees(2750).minor());
    }
}

TEST(persistence_review_queue_and_refund_state_survive_restart) {
    std::string dir = freshDir("review");
    ManualClock clock;
    std::string heldId;
    {
        auto ctx = open(dir, clock);
        ctx->auth.registerCustomer("Alice", "alice@example.com", "", kPassword).value();
        ctx->auth.registerCustomer("Bob", "bob@example.com", "", kPassword).value();
        Session alice = ctx->auth.login("alice@example.com", kPassword).value();
        auto m = PaymentMethodFactory::create("BANK_ACCOUNT", "123456789012").value();
        clock.advance(30); CHECK(ctx->payments.addMoney(alice, rupees(20000), *m, "t1").ok());
        clock.advance(30);
        heldId = ctx->payments.transfer(alice, "bob@example.com", rupees(15000), "big").value().tx.id();
    }
    auto ctx = open(dir, clock);
    CHECK_EQ(ctx->reviewQueue.size(), 1u);
    CHECK(ctx->reviewQueue.contains(heldId));
    auto admin = ctx->auth.login(kDefaultAdminEmail, kDefaultAdminPassword).value();
    CHECK(ctx->payments.approveReview(admin, heldId).ok());
}

TEST(persistence_corrupt_file_is_reported_and_left_untouched) {
    std::string dir = freshDir("corrupt");
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir + "/users.json");
        f << "{ this is not json";
    }
    ManualClock clock;
    AppOptions o;
    o.dataDir = dir; o.configPath = "none.json"; o.fileLogging = false; o.passwordIterations = 10;
    auto r = AppContext::create(o, clock);
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::PersistenceFailure);
    std::ifstream in(dir + "/users.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_EQ(content, std::string("{ this is not json"));   // never overwritten
}

TEST(persistence_rejects_structurally_invalid_records) {
    std::string dir = freshDir("badrecord");
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir + "/wallets.json");
        f << R"([{"id":"WAL-1","ownerId":"U","balance":5,"status":"EXPLODED"}])";
    }
    ManualClock clock;
    AppOptions o;
    o.dataDir = dir; o.configPath = "none.json"; o.fileLogging = false; o.passwordIterations = 10;
    CHECK(!AppContext::create(o, clock).ok());
}

TEST(json_repositories_roundtrip_through_disk) {
    std::string dir = freshDir("repo");
    {
        JsonDatabase db(dir);
        CHECK(db.load().ok());
        db.wallets().save(Wallet::restore("WAL-1", "USR-1", Money::fromMinor(777), WalletStatus::Frozen));
        AuditEvent ev; ev.id = "AUD-1"; ev.timestamp = 5; ev.actor = "SYSTEM"; ev.action = "X"; ev.target = "T"; ev.result = "SUCCESS";
        ev.metadata["k"] = "v \"quoted\"\nline";
        db.audit().append(ev);
        CHECK(db.commit().ok());
    }
    JsonDatabase db(dir);
    CHECK(db.load().ok());
    auto w = db.wallets().find("WAL-1");
    CHECK(w.has_value());
    CHECK_EQ(w->balance().minor(), 777);
    CHECK(w->status() == WalletStatus::Frozen);
    CHECK_EQ(db.audit().all().size(), 1u);
    CHECK_EQ(db.audit().all()[0].metadata.at("k"), std::string("v \"quoted\"\nline"));
}

TEST(repository_update_and_ordering_guarantees) {
    InMemoryTransactionRepository repo;
    auto mk = [](const std::string& id, Timestamp t, const std::string& payer) {
        TransactionData d; d.id = id; d.createdAt = t; d.payerUserId = payer; d.amount = Money::fromMinor(100); d.scopedKey = payer + ":" + id;
        return Transaction(d);
    };
    repo.save(mk("A", 10, "U1"));
    repo.save(mk("C", 30, "U1"));
    repo.save(mk("B", 20, "U1"));   // out of order: must still be sorted
    auto all = repo.all();
    CHECK_EQ(all[0].id(), std::string("A")); CHECK_EQ(all[1].id(), std::string("B")); CHECK_EQ(all[2].id(), std::string("C"));
    CHECK_EQ(repo.forUserSince("U1", 20).size(), 2u);
    TransactionFilter f; f.from = 15; f.to = 25;
    CHECK_EQ(repo.search(f).size(), 1u);
    auto b = repo.findById("B").value();
    CHECK(b.transitionTo(TxStatus::Processing, 99).ok());
    repo.save(b);                    // update in place, no duplicate
    CHECK_EQ(repo.all().size(), 3u);
    CHECK(repo.findByScopedKey("U1:B")->status() == TxStatus::Processing);
    CHECK(!repo.findById("Z").has_value());
}

TEST(user_repository_enforces_uniqueness) {
    InMemoryUserRepository repo;
    auto mk = [](const std::string& id, const std::string& email) {
        return std::make_shared<Customer>(id, "N", email, "", 0, Credentials("s", "h"));
    };
    CHECK(repo.add(mk("U1", "a@x.com")).ok());
    CHECK(!repo.add(mk("U2", "A@X.com")).ok());
    CHECK(!repo.add(mk("U1", "b@x.com")).ok());
    CHECK(repo.findByEmail("A@x.COM") != nullptr);
    CHECK(repo.findById("nope") == nullptr);
}

// ---------- end to end ----------
TEST(end_to_end_demo_flow_with_fraud_review_and_refund) {
    Env e;
    auto aliceId = e.addCustomer("Alice", "alice@example.com", AccountTier::Premium);
    auto bobId = e.addCustomer("Bob", "bob@example.com");
    auto shopId = e.addMerchant("Shop", "shop@example.com");
    Session alice = e.login("alice@example.com");
    Session shop = e.login("shop@example.com");
    Session admin = e.admin();

    e.fund(alice, 50000);
    e.tick(); CHECK(e.ctx->payments.transfer(alice, "bob@example.com", rupees(2000), e.key()).ok());
    e.tick(); auto pay = e.ctx->payments.payMerchant(alice, e.paymentIdOf(shopId), rupees(3000), e.key(), "headphones", "ORD-1");
    CHECK(pay.ok());
    e.tick(); auto big = e.ctx->payments.payMerchant(alice, e.paymentIdOf(shopId), rupees(25000), e.key(), "laptop", "ORD-2");
    CHECK(big.ok());
    CHECK(big.value().tx.status() == TxStatus::PendingReview);

    auto queue = e.ctx->admin.reviewQueue(admin).value();
    CHECK_EQ(queue.size(), 1u);
    CHECK(e.ctx->payments.approveReview(admin, queue[0].transactionId).ok());
    CHECK_EQ(e.balanceOf(shopId).minor(), rupees(28000).minor());

    CHECK(e.ctx->refunds.issueRefund(shop, pay.value().tx.id(), rupees(1000), "partial", e.key()).ok());
    CHECK_EQ(e.balanceOf(aliceId).minor(), rupees(50000 - 2000 - 3000 - 25000 + 1000).minor());
    CHECK_EQ(e.balanceOf(bobId).minor(), rupees(2000).minor());
    CHECK_EQ(e.balanceOf(shopId).minor(), rupees(27000).minor());

    // global invariants
    Money total;
    for (const auto& w : e.ctx->db->wallets().all()) { CHECK(e.ctx->settlement.reconciles(w.id())); total = total + w.balance(); }
    CHECK_EQ(total.minor(), rupees(50000).minor());   // money is conserved: only the 50,000 top-up entered the system
    for (const auto& t : e.ctx->db->transactions().all())
        if (t.status() != TxStatus::Failed && t.status() != TxStatus::PendingReview && t.status() != TxStatus::Cancelled)
            CHECK(e.ctx->settlement.balanced(t.id()));

    auto text = e.ctx->reports.renderText(admin).value();
    CHECK(text.find("Refunded amount") != std::string::npos);
    bool sawReviewApprove = false, sawRefund = false, sawSuspicious = false;
    for (const auto& ev : e.ctx->admin.auditLog(admin).value()) {
        sawReviewApprove |= ev.action == "REVIEW_APPROVE";
        sawRefund |= ev.action == "REFUND" && ev.result == "SUCCESS";
        sawSuspicious |= ev.result == "SUSPICIOUS";
    }
    CHECK(sawReviewApprove); CHECK(sawRefund); CHECK(sawSuspicious);
}
