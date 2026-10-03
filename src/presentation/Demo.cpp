#include "presentation/Demo.h"

#include "domain/PaymentIdentifier.h"
#include "presentation/Formatter.h"

namespace wallet {

namespace {
struct Narrator {
    std::ostream& out;
    int step = 0;
    int unexpected = 0;
    void say(const std::string& title) { out << "\n[" << ++step << "] " << title << "\n"; }
    void line(const std::string& text) { out << "    " << text << "\n"; }
    void expect(bool ok, const std::string& what) {
        out << "    " << (ok ? "ok: " : "UNEXPECTED: ") << what << "\n";
        if (!ok) ++unexpected;
    }
};
std::string pid(AppContext& ctx, const std::string& userId) {
    return std::dynamic_pointer_cast<Merchant>(ctx.db->users().findById(userId))->paymentId();
}
}  // namespace

int runDemo(AppContext& ctx, std::ostream& out) {
    Narrator n{out};
    Formatter fmt(ctx);
    out << "================ DIGITAL WALLET & PAYMENT SYSTEM - DEMO ================\n"
           "Educational simulation: no real money, banks or gateways are involved.\n";

    n.say("Admin starts the system and logs in");
    auto adminSession = ctx.auth.login(kDefaultAdminEmail, kDefaultAdminPassword);
    n.expect(adminSession.ok(), "admin logged in (" + std::string(kDefaultAdminEmail) + ")");
    if (!adminSession) return 1;
    Session admin = adminSession.value();

    n.say("Register Alice (premium customer), Bob (customer) and Sunrise Cafe (merchant)");
    auto aliceId = ctx.auth.registerCustomer("Alice", "alice@demo.com", "9876500001", "Alice@2024", AccountTier::Premium);
    auto bobId = ctx.auth.registerCustomer("Bob", "bob@demo.com", "9876500002", "Bob@2024x");
    auto cafeId = ctx.auth.registerMerchant("Carol", "cafe@demo.com", "9876500003", "Cafe@2024x", "Sunrise Cafe", "Food");
    n.expect(aliceId.ok() && bobId.ok() && cafeId.ok(), "three accounts + wallets created (passwords stored salted+hashed)");
    if (!aliceId || !bobId || !cafeId) return 1;

    n.say("Alice logs in and adds INR 20,000 from her debit card");
    Session alice = ctx.auth.login("alice@demo.com", "Alice@2024").value();
    auto card = PaymentMethodFactory::create("DEBIT_CARD", "4111111111111111").value();
    auto topup = ctx.payments.addMoney(alice, Money::fromMinor(2000000), *card, "demo-topup-1");
    n.expect(topup.ok() && topup.value().tx.status() == TxStatus::Success, "top-up " + (topup ? topup.value().tx.id() : "?") + " succeeded via " + card->describe());
    out << "    ledger:\n" << fmt.ledger(ctx.db->ledger().forTransaction(topup.value().tx.id()));

    n.say("Alice transfers INR 1,000 to Bob");
    auto xfer = ctx.payments.transfer(alice, "bob@demo.com", Money::fromMinor(100000), "demo-xfer-1", "dinner share");
    n.expect(xfer.ok() && xfer.value().tx.status() == TxStatus::Success, "transfer " + (xfer ? xfer.value().tx.id() : "?") + " succeeded");

    n.say("Alice pays the cafe INR 2,500 (limits and risk engine run first)");
    auto pay = ctx.payments.payMerchant(alice, pid(ctx, cafeId.value()), Money::fromMinor(250000), "demo-pay-1", "catering order", "ORD-1001");
    n.expect(pay.ok() && pay.value().tx.status() == TxStatus::Success, "merchant payment succeeded; risk decision " + std::string(toString(pay.value().risk.decision)));
    out << fmt.details(pay.value().tx) << "    ledger (double entry):\n" << fmt.ledger(ctx.db->ledger().forTransaction(pay.value().tx.id()));

    n.say("Network retry: the same payment request (same idempotency key) is submitted again");
    auto retry = ctx.payments.payMerchant(alice, pid(ctx, cafeId.value()), Money::fromMinor(250000), "demo-pay-1", "catering order", "ORD-1001");
    n.expect(retry.ok() && retry.value().duplicate && retry.value().tx.id() == pay.value().tx.id(), "replayed original " + pay.value().tx.id() + "; Alice was NOT charged twice");

    n.say("Alice pays INR 12,000 -> above the review threshold, so the risk engine holds it");
    auto big = ctx.payments.payMerchant(alice, pid(ctx, cafeId.value()), Money::fromMinor(1200000), "demo-pay-3", "equipment", "ORD-1004");
    n.expect(big.ok() && big.value().tx.status() == TxStatus::PendingReview, "held for review: " + (big ? big.value().tx.meta("riskReasons") : std::string()));

    n.say("Alice scans a QR code from the cafe (NETRAPAY identifier) for INR 180");
    std::string qr = PaymentIdentifier::generate(pid(ctx, cafeId.value()), "ORD-1002", Money::fromMinor(18000));
    n.line("QR payload: " + qr);
    auto qrPay = ctx.payments.payQr(alice, qr, std::nullopt, "demo-qr-1");
    n.expect(qrPay.ok() && qrPay.value().tx.status() == TxStatus::Success, "QR payment succeeded");

    n.say("Alice tries to pay INR 18,000 but only has INR 16,320 -> fails safely");
    auto tooMuch = ctx.payments.payMerchant(alice, pid(ctx, cafeId.value()), Money::fromMinor(1800000), "demo-pay-2", "big order", "ORD-1003");
    n.expect(!tooMuch.ok() && tooMuch.error().code == ErrorCode::InsufficientFunds, "rejected: " + (tooMuch ? std::string() : tooMuch.error().message));
    n.line("balances unchanged; the failed attempt is recorded as FAILED with no ledger lines");

    n.say("The cafe issues a partial refund of INR 1,000 on the INR 2,500 order");
    Session cafe = ctx.auth.login("cafe@demo.com", "Cafe@2024x").value();
    auto refund = ctx.refunds.issueRefund(cafe, pay.value().tx.id(), Money::fromMinor(100000), "item unavailable", "demo-refund-1");
    n.expect(refund.ok() && refund.value().status() == TxStatus::Success, "refund " + (refund ? refund.value().id() : "?") + " linked to " + pay.value().tx.id());
    auto original = ctx.db->transactions().findById(pay.value().tx.id()).value();
    n.expect(original.status() == TxStatus::PartiallyRefunded && original.refundable().minor() == 150000, "original is PARTIALLY_REFUNDED, INR 1,500.00 still refundable");
    auto overRefund = ctx.refunds.issueRefund(cafe, pay.value().tx.id(), Money::fromMinor(200000), "too much", "demo-refund-2");
    n.expect(!overRefund.ok() && overRefund.error().code == ErrorCode::RefundExceedsOriginal, "over-refund blocked: " + (overRefund ? std::string() : overRefund.error().message));

    n.say("Alice checks her balance and history");
    auto wallet = ctx.query.myWallet(alice).value();
    out << fmt.wallet(wallet) << "\n";
    auto history = ctx.query.search(alice, {}).value();
    out << fmt.table(history);
    n.expect(wallet.balance().minor() == 2000000 - 100000 - 250000 - 18000 + 100000, "Alice's balance = 20000 - 1000 - 2500 - 180 + 1000 refund = INR 17320.00");

    n.say("Admin reviews the fraud queue and approves the held payment");
    auto queue = ctx.admin.reviewQueue(admin).value();
    for (const auto& item : queue) out << fmt.review(item) << "\n";
    n.expect(queue.size() == 1, "one transaction awaiting review");
    auto approved = ctx.payments.approveReview(admin, big.value().tx.id());
    n.expect(approved.ok() && approved.value().status() == TxStatus::Success, "approved and settled " + big.value().tx.id());

    n.say("Admin freezes Bob's wallet; a transfer to him is then refused");
    n.expect(ctx.admin.freezeWallet(admin, bobId.value()).ok(), "Bob's wallet frozen");
    auto toFrozen = ctx.payments.transfer(alice, "bob@demo.com", Money::fromMinor(10000), "demo-xfer-2");
    n.expect(!toFrozen.ok() && toFrozen.error().code == ErrorCode::WalletFrozen, "rejected: " + (toFrozen ? std::string() : toFrozen.error().message));
    n.expect(ctx.admin.unfreezeWallet(admin, bobId.value()).ok(), "Bob's wallet unfrozen");

    n.say("System report");
    out << ctx.reports.renderText(admin).value();

    n.say("Integrity checks (ledger vs balances, double entry, money conservation)");
    bool reconciled = true;
    Money total;
    for (const auto& w : ctx.db->wallets().all()) { reconciled = reconciled && ctx.settlement.reconciles(w.id()); total = total + w.balance(); }
    n.expect(reconciled, "every wallet balance equals the sum of its ledger entries");
    bool balanced = true;
    for (const auto& t : ctx.db->transactions().all())
        if (t.status() == TxStatus::Success || t.status() == TxStatus::PartiallyRefunded || t.status() == TxStatus::Refunded)
            balanced = balanced && ctx.settlement.balanced(t.id());
    n.expect(balanced, "every settled transaction has equal debits and credits");
    n.expect(total.minor() == 2000000, "total money in all wallets = INR 20000.00 (only the top-up entered the system)");

    n.say("Audit trail (last 12 events)");
    for (const auto& e : ctx.admin.auditLog(admin, 12).value()) out << fmt.audit(e) << "\n";

    out << "\nDemo finished: " << (n.unexpected == 0 ? "all steps behaved as expected." : std::to_string(n.unexpected) + " step(s) UNEXPECTED.") << "\n";
    return n.unexpected == 0 ? 0 : 1;
}

} // namespace wallet
