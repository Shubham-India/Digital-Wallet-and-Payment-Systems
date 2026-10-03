// Transfers, merchant/QR/bill payments, idempotency, limits, fraud rules, review flow, ledger invariants.
#include "TestHelpers.h"
#include "domain/PaymentIdentifier.h"

using namespace testenv;

namespace {
struct Pair {
    Env e;
    std::string aliceId, bobId;
    Session alice, bob;
    Pair() {
        aliceId = e.addCustomer("Alice", "alice@example.com");
        bobId = e.addCustomer("Bob", "bob@example.com");
        alice = e.login("alice@example.com");
        bob = e.login("bob@example.com");
    }
};
}  // namespace

TEST(add_money_via_each_payment_method) {
    Env e;
    auto id = e.addCustomer("Alice", "alice@example.com");
    Session s = e.login("alice@example.com");
    struct Case { const char* kind; const char* ref; };
    for (Case c : {Case{"BANK_ACCOUNT", "123456789012"}, Case{"DEBIT_CARD", "4111111111111111"},
                   Case{"CREDIT_CARD", "5500000000000004"}, Case{"CASH_DEPOSIT", "AGENT-7"}}) {
        auto method = PaymentMethodFactory::create(c.kind, c.ref).value();
        e.tick();
        auto r = e.ctx->payments.addMoney(s, rupees(1000), *method, e.key());
        CHECK(r.ok());
        CHECK(r.value().tx.status() == TxStatus::Success);
    }
    CHECK_EQ(e.balanceOf(id).minor(), rupees(4000).minor());
}

TEST(payment_method_factory_and_masking) {
    CHECK(!PaymentMethodFactory::create("BITCOIN", "x").ok());
    CHECK(!PaymentMethodFactory::create("DEBIT_CARD", "12ab").ok());
    CHECK(!PaymentMethodFactory::create("CASH_DEPOSIT", "").ok());
    auto card = PaymentMethodFactory::create("DEBIT_CARD", "4111111111111111").value();
    CHECK(card->describe().find("4111111111111111") == std::string::npos);  // masked
    CHECK(card->describe().find("1111") != std::string::npos);
}

TEST(cash_deposit_cap_is_enforced) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    Session s = e.login("alice@example.com");
    auto cash = PaymentMethodFactory::create("CASH_DEPOSIT", "AGENT-1").value();
    auto r = e.ctx->payments.addMoney(s, rupees(60000), *cash, e.key());   // above cash cap
    CHECK(!r.ok());
}

TEST(scenario1_transfer_succeeds_and_updates_both_wallets) {
    Pair p;
    p.e.fund(p.alice, 5000);
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(1000), p.e.key(), "lunch");
    CHECK(r.ok());
    CHECK(r.value().tx.status() == TxStatus::Success);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(4000).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(1000).minor());
}

TEST(scenario2_insufficient_balance_fails_and_changes_nothing) {
    Pair p;
    p.e.fund(p.alice, 500);
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(800), p.e.key());
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::InsufficientFunds);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(500).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), 0);
    // the failed attempt is still recorded, as FAILED, with a reason
    TransactionFilter f; f.status = TxStatus::Failed;
    auto failed = p.e.ctx->db->transactions().search(f);
    CHECK_EQ(failed.size(), 1u);
    CHECK_EQ(failed[0].data().failureCode, std::string("InsufficientFunds"));
    // and it produced no ledger postings
    CHECK(p.e.ctx->db->ledger().forTransaction(failed[0].id()).empty());
}

TEST(transfer_to_unknown_account_self_admin_and_frozen_wallet) {
    Pair p;
    p.e.fund(p.alice, 1000);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "ghost@example.com", rupees(10), p.e.key()).error().code == ErrorCode::UserNotFound);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "alice@example.com", rupees(10), p.e.key()).error().code == ErrorCode::SelfTransfer);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, kDefaultAdminEmail, rupees(10), p.e.key()).error().code == ErrorCode::ValidationFailed);

    Session admin = p.e.admin();
    CHECK(p.e.ctx->admin.freezeWallet(admin, p.bobId).ok());
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).error().code == ErrorCode::WalletFrozen);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(1000).minor());

    CHECK(p.e.ctx->admin.freezeWallet(admin, p.aliceId).ok());     // sender frozen
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).error().code == ErrorCode::WalletFrozen);
    CHECK(p.e.ctx->admin.unfreezeWallet(admin, p.aliceId).ok());
    CHECK(p.e.ctx->admin.unfreezeWallet(admin, p.bobId).ok());
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).ok());
}

TEST(transfer_to_suspended_user_is_rejected) {
    Pair p;
    p.e.fund(p.alice, 1000);
    CHECK(p.e.ctx->admin.setUserStatus(p.e.admin(), p.bobId, AccountStatus::Suspended).ok());
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).error().code == ErrorCode::AccountInactive);
}

TEST(invalid_amounts_are_rejected) {
    Pair p;
    p.e.fund(p.alice, 1000);
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", Money(), p.e.key()).error().code == ErrorCode::InvalidAmount);
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), "").error().code == ErrorCode::ValidationFailed);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(1000).minor());
}

TEST(withdraw_reduces_balance_and_cannot_overdraw) {
    Pair p;
    p.e.fund(p.alice, 1000);
    p.e.tick();
    CHECK(p.e.ctx->payments.withdraw(p.alice, rupees(300), p.e.key()).ok());
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(700).minor());
    p.e.tick();
    CHECK(p.e.ctx->payments.withdraw(p.alice, rupees(701), p.e.key()).error().code == ErrorCode::InsufficientFunds);
}

TEST(scenario3_same_idempotency_key_never_double_charges) {
    Pair p;
    p.e.fund(p.alice, 5000);
    p.e.tick();
    auto first = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(1000), "retry-key-1");
    p.e.tick();
    auto second = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(1000), "retry-key-1");
    CHECK(first.ok()); CHECK(second.ok());
    CHECK(!first.value().duplicate);
    CHECK(second.value().duplicate);
    CHECK_EQ(first.value().tx.id(), second.value().tx.id());
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(4000).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(1000).minor());
    TransactionFilter f; f.type = TxType::Transfer;
    CHECK_EQ(p.e.ctx->db->transactions().search(f).size(), 1u);
}

TEST(idempotency_key_reuse_with_different_payload_is_an_error) {
    Pair p;
    p.e.fund(p.alice, 5000);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(100), "k").ok());
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(999), "k");
    CHECK(r.error().code == ErrorCode::IdempotencyMismatch);
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(100).minor());
}

TEST(idempotency_keys_are_scoped_per_user) {
    Pair p;
    p.e.fund(p.alice, 1000);
    p.e.fund(p.bob, 1000);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), "same").ok());
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.bob, "alice@example.com", rupees(10), "same");
    CHECK(r.ok());
    CHECK(!r.value().duplicate);   // another user's key space
}

TEST(replaying_a_failed_request_does_not_retry_it) {
    Pair p;
    p.e.fund(p.alice, 100);
    p.e.tick();
    CHECK(!p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(500), "k-fail").ok());
    p.e.fund(p.alice, 1000);   // now affordable, but the key is spent: caller must use a new key
    p.e.tick();
    CHECK(!p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(500), "k-fail").ok());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), 0);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(500), "k-new").ok());
}

TEST(scenario4_per_transaction_limit) {
    Pair p;
    p.e.fund(p.alice, 60000);
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(30000), p.e.key());   // Basic limit is 25,000
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::LimitExceeded);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(60000).minor());
}

TEST(premium_tier_has_higher_limit) {
    Env e;
    auto id = e.addCustomer("Prem", "prem@example.com", AccountTier::Premium);
    e.addCustomer("Bob", "bob@example.com");
    Session s = e.login("prem@example.com");
    // Premium per-tx = 1,00,000; top-ups are limited by the same per-tx rule, so fund in chunks
    for (int i = 0; i < 3; ++i) { e.tick(); auto m = PaymentMethodFactory::create("BANK_ACCOUNT", "123456789012").value();
                                  CHECK(e.ctx->payments.addMoney(s, rupees(40000), *m, e.key()).ok()); }
    e.tick();
    auto r = e.ctx->payments.transfer(s, "bob@example.com", rupees(30000), e.key());   // > Basic limit, OK for Premium
    CHECK(r.ok() || r.error().code == ErrorCode::FraudRejected);
    CHECK(r.ok());
    CHECK(r.value().tx.status() == TxStatus::PendingReview);   // 30,000 >= review threshold
    (void)id;
}

TEST(daily_amount_limit) {
    Pair p;
    p.e.fund(p.alice, 100000);
    for (int i = 0; i < 5; ++i) {                  // 5 x 9,000 = 45,000 of a 50,000 daily limit
        p.e.tick();
        CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(9000), p.e.key()).ok());
    }
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(9000), p.e.key());
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::LimitExceeded);
    // the limit resets on the next UTC day
    p.e.clock.advance(kSecondsPerDay);
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(9000), p.e.key()).ok());
}

TEST(daily_count_limit_is_configurable_by_admin) {
    Pair p;
    p.e.fund(p.alice, 5000);
    LimitSettings s{rupees(25000), rupees(50000), 2};
    CHECK(p.e.ctx->admin.configureLimits(p.e.admin(), LimitTier::Basic, s).ok());
    for (int i = 0; i < 2; ++i) { p.e.tick(); CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).ok()); }
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key()).error().code == ErrorCode::LimitExceeded);
    // invalid configuration is refused
    CHECK(!p.e.ctx->admin.configureLimits(p.e.admin(), LimitTier::Basic, LimitSettings{rupees(100), rupees(50), 5}).ok());
    CHECK(p.e.ctx->admin.configureLimits(p.alice, LimitTier::Basic, s).error().code == ErrorCode::Unauthorized);
}

TEST(scenario5_large_payment_is_held_for_review_without_moving_money) {
    Pair p;
    p.e.fund(p.alice, 20000);
    p.e.tick();
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(15000), p.e.key());   // >= 10,000 review threshold
    CHECK(r.ok());
    CHECK(r.value().tx.status() == TxStatus::PendingReview);
    CHECK(r.value().risk.decision == RiskDecision::Review);
    CHECK(r.value().risk.score > 0);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(20000).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), 0);
    CHECK_EQ(p.e.ctx->reviewQueue.size(), 1u);
}

TEST(admin_approves_review_and_money_moves_once) {
    Pair p;
    p.e.fund(p.alice, 20000);
    p.e.tick();
    auto held = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(15000), p.e.key()).value().tx;
    Session admin = p.e.admin();
    CHECK(p.e.ctx->payments.approveReview(p.alice, held.id()).error().code == ErrorCode::Unauthorized);
    auto done = p.e.ctx->payments.approveReview(admin, held.id());
    CHECK(done.ok());
    CHECK(done.value().status() == TxStatus::Success);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(5000).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(15000).minor());
    CHECK_EQ(p.e.ctx->reviewQueue.size(), 0u);
    CHECK(!p.e.ctx->payments.approveReview(admin, held.id()).ok());   // cannot approve twice
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(15000).minor());
}

TEST(admin_rejects_review_and_nothing_moves) {
    Pair p;
    p.e.fund(p.alice, 20000);
    p.e.tick();
    auto held = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(15000), p.e.key()).value().tx;
    auto r = p.e.ctx->payments.rejectReview(p.e.admin(), held.id(), "looks wrong");
    CHECK(r.ok());
    CHECK(r.value().status() == TxStatus::Cancelled);
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(20000).minor());
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), 0);
    CHECK(!p.e.ctx->payments.rejectReview(p.e.admin(), held.id(), "again").ok());
}

TEST(review_queue_orders_by_risk_score_then_age) {
    FraudReviewQueue q;
    q.push("A", 40, 10);
    q.push("B", 90, 20);
    q.push("C", 40, 5);
    q.push("D", 90, 15);
    auto items = q.pending();
    CHECK_EQ(items.size(), 4u);
    CHECK_EQ(items[0].transactionId, std::string("D"));   // 90, older
    CHECK_EQ(items[1].transactionId, std::string("B"));
    CHECK_EQ(items[2].transactionId, std::string("C"));   // 40, older
    CHECK_EQ(items[3].transactionId, std::string("A"));
    q.remove("D");
    CHECK_EQ(q.top()->transactionId, std::string("B"));
    CHECK_EQ(q.size(), 3u);
}

TEST(fraud_large_amount_rule_unit) {
    FraudSettings fs = AppConfig::defaults().fraud;
    LargeAmountRule rule(fs.reviewAmount, fs.rejectAmount);
    RiskContext rc; rc.userId = "U"; rc.now = 1000;
    rc.amount = rupees(500);     CHECK(rule.evaluate(rc).decision == RiskDecision::Approve);
    rc.amount = rupees(10000);   CHECK(rule.evaluate(rc).decision == RiskDecision::Review);
    rc.amount = rupees(150000);  CHECK(rule.evaluate(rc).decision == RiskDecision::Reject);
}

TEST(fraud_velocity_rule_unit_uses_a_sliding_window) {
    auto mk = [](const std::string& id, Timestamp t) {
        TransactionData d; d.id = id; d.type = TxType::Transfer; d.payerUserId = "U"; d.createdAt = t; d.amount = Money::fromMinor(100);
        return Transaction(d);
    };
    VelocityRule rule(60, 3, 6);
    RiskContext rc; rc.userId = "U"; rc.now = 1000; rc.amount = Money::fromMinor(100);
    rc.recent = {mk("old", 100), mk("a", 950), mk("b", 990)};              // "old" is outside the window
    CHECK(rule.evaluate(rc).decision == RiskDecision::Review);              // a, b + candidate = 3
    rc.recent = {mk("old", 100), mk("a", 950)};
    CHECK(rule.evaluate(rc).decision == RiskDecision::Approve);             // 2
    rc.recent = {mk("1", 941), mk("2", 950), mk("3", 960), mk("4", 970), mk("5", 980)};
    CHECK(rule.evaluate(rc).decision == RiskDecision::Reject);              // 6 incl. candidate
}

TEST(fraud_engine_combines_rules_strictest_wins) {
    FraudEngine engine = FraudEngine::standard(AppConfig::defaults().fraud);
    CHECK_EQ(engine.ruleCount(), 3u);
    RiskContext rc; rc.userId = "U"; rc.now = 1000; rc.amount = rupees(20000);
    auto a = engine.assess(rc);
    CHECK(a.decision == RiskDecision::Review);
    CHECK(!a.reasons.empty());
    rc.amount = rupees(100);
    CHECK(engine.assess(rc).decision == RiskDecision::Approve);
}

TEST(fraud_rapid_repeated_transfers_trigger_review_then_reject) {
    Pair p;
    p.e.fund(p.alice, 5000);
    std::vector<TxStatus> statuses;
    ErrorCode lastError = ErrorCode::Overflow;
    for (int i = 0; i < 10; ++i) {   // no clock ticks: all inside one velocity window
        auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key());
        if (r.ok()) statuses.push_back(r.value().tx.status()); else lastError = r.error().code;
    }
    CHECK_EQ(statuses.size(), 9u);
    CHECK(statuses[0] == TxStatus::Success);
    CHECK(statuses[3] == TxStatus::Success);
    CHECK(statuses[4] == TxStatus::PendingReview);     // 5th attempt in the window
    CHECK(lastError == ErrorCode::FraudRejected);      // 10th attempt
    CHECK_EQ(p.e.balanceOf(p.bobId).minor(), rupees(40).minor());   // only the 4 approved ones moved money
}

TEST(fraud_repeated_failures_trigger_review) {
    Pair p;
    p.e.fund(p.alice, 100);
    for (int i = 0; i < 3; ++i) {
        p.e.tick(5);
        CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(900), p.e.key()).error().code == ErrorCode::InsufficientFunds);
    }
    p.e.tick(5);
    auto r = p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(10), p.e.key());
    CHECK(r.ok());
    CHECK(r.value().tx.status() == TxStatus::PendingReview);
}

TEST(scenario6_merchant_payment) {
    Env e;
    auto aliceId = e.addCustomer("Alice", "alice@example.com");
    auto cafeId = e.addMerchant("Cafe", "cafe@example.com");
    Session alice = e.login("alice@example.com");
    e.fund(alice, 2000);
    e.tick();
    auto r = e.ctx->payments.payMerchant(alice, e.paymentIdOf(cafeId), rupees(450), e.key(), "coffee", "ORD1");
    CHECK(r.ok());
    CHECK(r.value().tx.status() == TxStatus::Success);
    CHECK_EQ(r.value().tx.data().merchantId, cafeId);
    CHECK_EQ(e.balanceOf(aliceId).minor(), rupees(1550).minor());
    CHECK_EQ(e.balanceOf(cafeId).minor(), rupees(450).minor());
    CHECK(e.ctx->payments.payMerchant(alice, "MER-9999", rupees(10), e.key()).error().code == ErrorCode::MerchantNotFound);
}

TEST(qr_payment_uses_generated_identifier) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    auto cafeId = e.addMerchant("Cafe", "cafe@example.com");
    Session alice = e.login("alice@example.com");
    e.fund(alice, 1000);
    std::string qr = PaymentIdentifier::generate(e.paymentIdOf(cafeId), "ORD9823", rupees(125));
    e.tick();
    auto r = e.ctx->payments.payQr(alice, qr, std::nullopt, e.key());
    CHECK(r.ok());
    CHECK(r.value().tx.type() == TxType::QrPayment);
    CHECK_EQ(r.value().tx.data().reference, std::string("ORD9823"));
    CHECK_EQ(e.balanceOf(cafeId).minor(), rupees(125).minor());
    // amountless QR needs an amount; conflicting amounts are refused; garbage is refused
    std::string open = PaymentIdentifier::generate(e.paymentIdOf(cafeId), "ORD2");
    e.tick();
    CHECK(!e.ctx->payments.payQr(alice, open, std::nullopt, e.key()).ok());
    e.tick();
    CHECK(e.ctx->payments.payQr(alice, open, rupees(50), e.key()).ok());
    e.tick();
    CHECK(!e.ctx->payments.payQr(alice, qr, rupees(1), e.key()).ok());
    CHECK(e.ctx->payments.payQr(alice, "hello", rupees(1), e.key()).error().code == ErrorCode::InvalidIdentifier);
}

TEST(bill_payment_debits_wallet_and_posts_balanced_ledger) {
    Env e;
    auto id = e.addCustomer("Alice", "alice@example.com");
    Session alice = e.login("alice@example.com");
    e.fund(alice, 1000);
    e.tick();
    auto r = e.ctx->payments.payBill(alice, "City Power", "ACC-778", rupees(300), e.key());
    CHECK(r.ok());
    CHECK_EQ(e.balanceOf(id).minor(), rupees(700).minor());
    CHECK(e.ctx->settlement.balanced(r.value().tx.id()));
    CHECK(!e.ctx->payments.payBill(alice, "", "x", rupees(1), e.key()).ok());
}

TEST(ledger_is_double_entry_and_reconciles_with_wallet_balances) {
    Pair p;
    p.e.fund(p.alice, 5000);
    for (int i = 0; i < 3; ++i) {
        p.e.tick();
        CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(100 + i), p.e.key()).ok());
    }
    p.e.tick();
    CHECK(!p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(900000), p.e.key()).ok());   // fails, must not post
    for (const auto& t : p.e.ctx->db->transactions().all()) {
        if (t.status() == TxStatus::Success) CHECK(p.e.ctx->settlement.balanced(t.id()));
        else CHECK(p.e.ctx->db->ledger().forTransaction(t.id()).empty());
    }
    for (const auto& w : p.e.ctx->db->wallets().all()) CHECK(p.e.ctx->settlement.reconciles(w.id()));
}

TEST(settlement_is_atomic_when_the_second_leg_fails) {
    // Receiver wallet frozen after routing was resolved: the debit on the sender copy must not persist.
    Pair p;
    p.e.fund(p.alice, 1000);
    TransactionData d;
    d.id = "TX-X"; d.type = TxType::Transfer; d.amount = rupees(100); d.status = TxStatus::Processing;
    d.payerWalletId = p.e.ctx->db->users().findById(p.aliceId)->walletId();
    d.payeeWalletId = p.e.ctx->db->users().findById(p.bobId)->walletId();
    auto bobWallet = p.e.ctx->db->wallets().find(d.payeeWalletId).value();
    CHECK(bobWallet.freeze().ok());
    p.e.ctx->db->wallets().save(bobWallet);
    auto r = p.e.ctx->settlement.settle(Transaction(d));
    CHECK(!r.ok());
    CHECK_EQ(p.e.balanceOf(p.aliceId).minor(), rupees(1000).minor());
    CHECK(p.e.ctx->db->ledger().forTransaction("TX-X").empty());
}

TEST(notifications_reach_both_parties_and_audit_records_the_payment) {
    Pair p;
    p.e.fund(p.alice, 1000);
    p.e.tick();
    CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(100), p.e.key()).ok());
    CHECK(p.e.ctx->inbox->inboxFor(p.bobId).size() >= 1u);
    CHECK_EQ(p.e.ctx->inbox->inboxFor(p.bobId).back().title, std::string("Payment received"));
    CHECK_EQ(p.e.ctx->inbox->inboxFor(p.aliceId).back().title, std::string("Payment sent"));
    CHECK(p.e.ctx->inbox->unreadCount(p.bobId) >= 1u);
    p.e.ctx->inbox->markAllRead(p.bobId);
    CHECK_EQ(p.e.ctx->inbox->unreadCount(p.bobId), 0u);
    bool audited = false;
    for (const auto& ev : p.e.ctx->db->audit().all()) audited |= ev.action == "PAYMENT_TRANSFER" && ev.result == "SUCCESS" && ev.actor == p.aliceId;
    CHECK(audited);
}

TEST(notification_channels_format_messages) {
    std::ostringstream out;
    EventPublisher pub;
    pub.subscribe(std::make_shared<EmailNotification>(out));
    pub.subscribe(std::make_shared<SmsNotification>(out));
    pub.subscribe(std::make_shared<PushNotification>(out));
    pub.subscribe(std::make_shared<ConsoleNotification>(out));
    Notification n;
    n.userId = "U1"; n.recipientName = "Alice"; n.email = "alice@example.com"; n.phone = "9876543210";
    n.title = "Hello"; n.body = "World"; n.timestamp = 1700000000;
    pub.publish(n);
    CHECK_EQ(pub.pending(), 1u);
    pub.dispatch();
    CHECK_EQ(pub.pending(), 0u);
    std::string s = out.str();
    CHECK(s.find("EMAIL to=alice@example.com") != std::string::npos);
    CHECK(s.find("SMS to=9876543210") != std::string::npos);
    CHECK(s.find("PUSH user=U1") != std::string::npos);
    CHECK(s.find("[notification] Alice") != std::string::npos);
}

TEST(search_filters_and_authorization_scoping) {
    Pair p;
    auto cafeId = p.e.addMerchant("Cafe", "cafe@example.com");
    p.e.fund(p.alice, 5000);
    p.e.tick(); CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(100), p.e.key()).ok());
    p.e.tick(); CHECK(p.e.ctx->payments.payMerchant(p.alice, p.e.paymentIdOf(cafeId), rupees(300), p.e.key()).ok());
    p.e.tick(); CHECK(p.e.ctx->payments.transfer(p.alice, "bob@example.com", rupees(50), p.e.key()).ok());

    TransactionFilter f; f.type = TxType::Transfer;
    CHECK_EQ(p.e.ctx->query.search(p.alice, f).value().size(), 2u);
    f = {}; f.minAmount = rupees(100); f.maxAmount = rupees(300);
    CHECK_EQ(p.e.ctx->query.search(p.alice, f).value().size(), 2u);
    f = {}; f.merchantId = cafeId;
    CHECK_EQ(p.e.ctx->query.search(p.alice, f).value().size(), 1u);
    f = {}; f.status = TxStatus::Success; f.from = p.e.clock.now() - 10; f.to = p.e.clock.now();
    CHECK(p.e.ctx->query.search(p.alice, f).value().size() >= 1u);
    // newest first
    auto all = p.e.ctx->query.search(p.alice, {}).value();
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1].createdAt() >= all[i].createdAt());
    // Bob only ever sees his own; he cannot see Alice's merchant payment
    auto bobs = p.e.ctx->query.search(p.bob, {}).value();
    for (const auto& t : bobs) CHECK(t.data().payerUserId == p.bobId || t.data().payeeUserId == p.bobId);
    f = {}; f.userId = p.aliceId;    // attempt to widen the filter is overridden
    for (const auto& t : p.e.ctx->query.search(p.bob, f).value()) CHECK(t.data().payerUserId == p.bobId || t.data().payeeUserId == p.bobId);
    CHECK(p.e.ctx->query.details(p.bob, all.front().id()).ok() == (all.front().data().payeeUserId == p.bobId));
    // admin sees everything
    CHECK(p.e.ctx->query.search(p.e.admin(), {}).value().size() >= all.size());
}
