// Money, Wallet, Transaction state machine, hashing, JSON, QR identifiers, time utilities.
#include "TestHelpers.h"
#include "domain/PaymentIdentifier.h"
#include "infrastructure/Json.h"
#include "infrastructure/PasswordHasher.h"

using namespace wallet;

TEST(money_parse_valid_amounts) {
    CHECK_EQ(Money::parse("125.50").value().minor(), 12550);
    CHECK_EQ(Money::parse("125").value().minor(), 12500);
    CHECK_EQ(Money::parse("0.5").value().minor(), 50);
    CHECK_EQ(Money::parse("  42.07 ").value().minor(), 4207);
    CHECK(Money::parse("0").value().isZero());
}

TEST(money_parse_rejects_garbage) {
    for (const char* bad : {"", "   ", "-5", "+5", "abc", "1.234", "1.", ".5", "1e5", "1,000", "12 34", "9999999999999999"}) {
        auto r = Money::parse(bad);
        CHECK(!r.ok());
    }
}

TEST(money_has_no_floating_point_drift) {
    Money total;
    for (int i = 0; i < 10; ++i) total = total + Money::parse("0.10").value();
    CHECK_EQ(total.minor(), 100);  // 10 x 0.10 is exactly 1.00
}

TEST(money_arithmetic_and_comparison) {
    Money a = Money::fromMinor(1000), b = Money::fromMinor(250);
    CHECK_EQ((a + b).minor(), 1250);
    CHECK_EQ((a - b).minor(), 750);
    CHECK(a > b);
    CHECK(b <= a);
    CHECK(a == Money::fromMinor(1000));
    CHECK_EQ(Money::fromMinor(12550).toString(), std::string("INR 125.50"));
    CHECK_EQ(Money::fromMinor(5).toString(), std::string("INR 0.05"));
}

TEST(money_invariants_throw) {
    bool negative = false, tooBig = false, underflow = false, overflow = false, mixed = false;
    try { Money::fromMinor(-1); } catch (const WalletException&) { negative = true; }
    try { Money::fromMinor(Money::kMaxMinor + 1); } catch (const WalletException&) { tooBig = true; }
    try { Money::fromMinor(1) - Money::fromMinor(2); } catch (const WalletException&) { underflow = true; }
    try { Money::fromMinor(Money::kMaxMinor) + Money::fromMinor(1); } catch (const WalletException&) { overflow = true; }
    try { Money::fromMinor(1, Currency::INR) + Money::fromMinor(1, Currency::USD); } catch (const WalletException&) { mixed = true; }
    CHECK(negative); CHECK(tooBig); CHECK(underflow); CHECK(overflow); CHECK(mixed);
}

TEST(wallet_credit_and_debit) {
    Wallet w = Wallet::open("W1", "U1");
    CHECK(w.credit(Money::fromMinor(5000)).ok());
    CHECK(w.debit(Money::fromMinor(1200)).ok());
    CHECK_EQ(w.balance().minor(), 3800);
}

TEST(wallet_rejects_insufficient_funds_and_zero) {
    Wallet w = Wallet::open("W1", "U1");
    CHECK(w.credit(Money::fromMinor(1000)).ok());
    auto r = w.debit(Money::fromMinor(1001));
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::InsufficientFunds);
    CHECK_EQ(w.balance().minor(), 1000);  // unchanged
    CHECK(w.credit(Money()).error().code == ErrorCode::InvalidAmount);
    CHECK(w.debit(Money()).error().code == ErrorCode::InvalidAmount);
}

TEST(wallet_freeze_unfreeze_close) {
    Wallet w = Wallet::open("W1", "U1");
    CHECK(w.credit(Money::fromMinor(1000)).ok());
    CHECK(w.freeze().ok());
    CHECK(w.debit(Money::fromMinor(1)).error().code == ErrorCode::WalletFrozen);
    CHECK(w.credit(Money::fromMinor(1)).error().code == ErrorCode::WalletFrozen);
    CHECK(w.unfreeze().ok());
    CHECK(w.debit(Money::fromMinor(1)).ok());
    CHECK(!w.close().ok());  // balance not zero
    CHECK(w.debit(Money::fromMinor(999)).ok());
    CHECK(w.close().ok());
    CHECK(w.credit(Money::fromMinor(1)).error().code == ErrorCode::WalletClosed);
    CHECK(!w.unfreeze().ok());
}

TEST(transaction_state_machine) {
    TransactionData d;
    d.id = "TX-1"; d.amount = Money::fromMinor(1000); d.refundedAmount = Money();
    Transaction t(d);
    CHECK(t.status() == TxStatus::Pending);
    CHECK(!t.transitionTo(TxStatus::Success, 1).ok());          // must go through Processing
    CHECK(t.transitionTo(TxStatus::Processing, 1).ok());
    CHECK(t.transitionTo(TxStatus::Success, 2).ok());
    CHECK(!t.transitionTo(TxStatus::Failed, 3).ok());           // success is final (except refunds)
    CHECK(!t.transitionTo(TxStatus::Pending, 3).ok());
}

TEST(transaction_refund_accounting) {
    TransactionData d;
    d.id = "TX-1"; d.amount = Money::fromMinor(1000); d.refundedAmount = Money(); d.status = TxStatus::Success;
    Transaction t(d);
    CHECK(t.applyRefund(Money::fromMinor(400), 1).ok());
    CHECK(t.status() == TxStatus::PartiallyRefunded);
    CHECK_EQ(t.refundable().minor(), 600);
    CHECK(t.applyRefund(Money::fromMinor(601), 2).error().code == ErrorCode::RefundExceedsOriginal);
    CHECK(t.applyRefund(Money::fromMinor(600), 3).ok());
    CHECK(t.status() == TxStatus::Refunded);
    CHECK(t.applyRefund(Money::fromMinor(1), 4).error().code == ErrorCode::RefundNotAllowed);
}

TEST(transaction_failed_and_pending_cannot_be_refunded) {
    TransactionData d;
    d.id = "TX-1"; d.amount = Money::fromMinor(1000); d.refundedAmount = Money();
    Transaction t(d);
    CHECK(t.applyRefund(Money::fromMinor(1), 1).error().code == ErrorCode::RefundNotAllowed);
    t.markFailed(ErrorCode::InsufficientFunds, "no money", 5);
    CHECK(t.status() == TxStatus::Failed);
    CHECK_EQ(t.data().failureCode, std::string("InsufficientFunds"));
}

TEST(sha256_known_vectors) {
    CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(pbkdf2_known_vectors) {
    CHECK_EQ(pbkdf2HmacSha256Hex("password", "salt", 1), std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    CHECK_EQ(pbkdf2HmacSha256Hex("password", "salt", 2), std::string("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
}

TEST(password_hasher_salts_differ_and_verify_works) {
    Pbkdf2PasswordHasher h(5);
    std::string s1 = h.newSalt(), s2 = h.newSalt();
    CHECK(s1 != s2);
    std::string hash = h.hash("Secret123", s1);
    CHECK(h.verify("Secret123", s1, hash));
    CHECK(!h.verify("secret123", s1, hash));
    CHECK(h.hash("Secret123", s2) != hash);
}

TEST(json_roundtrip_and_errors) {
    auto j = Json::parse(R"({"a":1,"b":[true,null,"x\n\"y\""],"c":{"d":-7}})");
    CHECK(j.ok());
    CHECK_EQ(j.value().integer("a"), 1);
    CHECK_EQ(j.value().find("c")->integer("d"), -7);
    auto again = Json::parse(j.value().dump());
    CHECK(again.ok());
    CHECK_EQ(again.value().dump(), j.value().dump());
    CHECK(j.value().find("b")->array()[2].asString() == "x\n\"y\"");
    for (const char* bad : {"", "{", "[1,]", "{\"a\":}", "nul", "{\"a\":1.5}", "[1] x", "\"abc"}) CHECK(!Json::parse(bad).ok());
}

TEST(payment_identifier_roundtrip) {
    std::string qr = PaymentIdentifier::generate("MER-0001", "ORD9823", Money::fromMinor(12550));
    CHECK_EQ(qr, std::string("NETRAPAY://merchant/MER-0001/order/ORD9823?amount=12550"));
    auto p = PaymentIdentifier::parse(qr);
    CHECK(p.ok());
    CHECK_EQ(p.value().merchantPaymentId, std::string("MER-0001"));
    CHECK_EQ(p.value().orderId, std::string("ORD9823"));
    CHECK_EQ(p.value().amount->minor(), 12550);
    CHECK(!PaymentIdentifier::parse("NETRAPAY://merchant/MER-0001/order/ORD1").value().amount.has_value());
}

TEST(payment_identifier_rejects_malformed) {
    for (const char* bad : {"", "http://x", "NETRAPAY://merchant/", "NETRAPAY://merchant/M1", "NETRAPAY://merchant//order/O1",
                            "NETRAPAY://merchant/M 1/order/O1", "NETRAPAY://merchant/M1/order/O1?amount=abc",
                            "NETRAPAY://merchant/M1/order/O1?amount=0", "NETRAPAY://merchant/M1/order/O1?foo=1"})
        CHECK(!PaymentIdentifier::parse(bad).ok());
}

TEST(time_formatting) {
    CHECK_EQ(formatTimestamp(0), std::string("1970-01-01 00:00:00"));
    CHECK_EQ(formatTimestamp(1700000000), std::string("2023-11-14 22:13:20"));
    CHECK_EQ(formatDate(1700000000), std::string("2023-11-14"));
    CHECK_EQ(startOfDay(1700000000), 1699920000);
}

TEST(id_generator_continues_after_observe) {
    IdGenerator g;
    CHECK_EQ(g.next("TX", 6), std::string("TX-000001"));
    g.observe("TX-000041");
    g.observe("garbage");
    CHECK_EQ(g.next("TX", 6), std::string("TX-000042"));
    CHECK_EQ(g.next("USR", 4), std::string("USR-0001"));
}
