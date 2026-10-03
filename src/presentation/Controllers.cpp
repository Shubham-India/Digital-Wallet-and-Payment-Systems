#include "presentation/Controllers.h"

#include <fstream>
#include <random>

#include "domain/PaymentIdentifier.h"

namespace wallet {

// ====================== shared helpers ======================
std::string ControllerBase::newKey() {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string key = "cli-";
    for (int i = 0; i < 16; ++i) key += hex[rd() % 16];
    return key;
}

void ControllerBase::showOutcome(const Result<PaymentOutcome>& r) {
    if (!report(r)) return;
    const auto& o = r.value();
    if (o.duplicate) con_.info("(This request was already processed - showing the original result; nothing was charged twice.)");
    switch (o.tx.status()) {
        case TxStatus::Success: con_.success("Completed " + o.tx.id() + " for " + o.tx.amount().toString()); break;
        case TxStatus::PendingReview:
            con_.info("[REVIEW] " + o.tx.id() + " was flagged by the risk engine and is held for admin review. No money has moved yet.");
            break;
        default: con_.info(o.tx.id() + " status: " + toString(o.tx.status()));
    }
}

TransactionFilter ControllerBase::promptFilter() {
    TransactionFilter f;
    con_.info("Optional filters - press Enter to skip each one.");
    std::string type = con_.prompt("  Type (TOP_UP, WITHDRAWAL, TRANSFER, MERCHANT_PAYMENT, QR_PAYMENT, BILL_PAYMENT, REFUND)");
    TxType t;
    if (!type.empty()) { if (parseTxType(type, t)) f.type = t; else con_.error("Unknown type ignored."); }
    std::string status = con_.prompt("  Status (SUCCESS, FAILED, PENDING_REVIEW, CANCELLED, PARTIALLY_REFUNDED, REFUNDED)");
    TxStatus st;
    if (!status.empty()) { if (parseTxStatus(status, st)) f.status = st; else con_.error("Unknown status ignored."); }
    if (auto lo = con_.promptMoney("  Minimum amount")) f.minAmount = *lo;
    if (auto hi = con_.promptMoney("  Maximum amount")) f.maxAmount = *hi;
    return f;
}

void ControllerBase::showHistory(const Session& s) {
    TransactionFilter f;
    if (con_.confirm("Apply filters?")) f = promptFilter();
    auto r = ctx_.query.search(s, f);
    if (!report(r)) return;
    con_.section("Transactions (newest first)");
    con_.out() << fmt_.table(r.value());
}

void ControllerBase::showTransaction(const Session& s) {
    std::string id = con_.prompt("Transaction ID");
    auto r = ctx_.query.details(s, id);
    if (!report(r)) return;
    con_.out() << fmt_.details(r.value());
    auto ledger = ctx_.query.ledgerFor(s, id);
    if (ledger) { con_.section("Ledger entries"); con_.out() << fmt_.ledger(ledger.value()); }
    auto refunds = ctx_.refunds.refundsOf(id);
    for (const auto& rf : refunds) con_.info("  refunded by " + fmt_.line(rf));
}

void ControllerBase::showNotifications(const Session& s) {
    const auto& inbox = ctx_.inbox->inboxFor(s.userId);
    con_.section("Notifications");
    if (inbox.empty()) con_.info("  (none this session)");
    for (auto it = inbox.rbegin(); it != inbox.rend(); ++it)
        con_.info("  " + formatTimestamp(it->timestamp) + "  " + it->title + " - " + it->body);
    ctx_.inbox->markAllRead(s.userId);
}

void ControllerBase::showProfile(const Session& s) {
    auto u = ctx_.db->users().findById(s.userId);
    if (!u) return;
    con_.section("Profile");
    con_.info("  " + u->describe());
    con_.info(std::string("  Status: ") + toString(u->status()) + "   Member since " + formatDate(u->createdAt()));
    if (!u->phone().empty()) con_.info("  Phone: " + u->phone());
    if (auto w = ctx_.query.myWallet(s); w) con_.info(fmt_.wallet(w.value()));
}

std::string ControllerBase::unreadHeader(const Session& s) {
    auto u = ctx_.db->users().findById(s.userId);
    std::string h = u ? "Logged in as " + u->name() + " [" + toString(s.role) + "]" : "";
    if (auto w = ctx_.query.myWallet(s); w) h += "   Balance: " + w.value().balance().toString();
    if (auto n = ctx_.inbox->unreadCount(s.userId)) h += "   (" + std::to_string(n) + " new notifications)";
    return h;
}

// ====================== customer ======================
void CustomerController::run(const Session& s) {
    Menu m("DIGITAL WALLET & PAYMENT SYSTEM");
    m.add("1", "Wallet balance", [&] { auto w = ctx_.query.myWallet(s); if (report(w)) con_.info(fmt_.wallet(w.value())); })
        .add("2", "Add money", [&] { addMoney(s); })
        .add("3", "Withdraw to bank", [&] {
            if (auto a = con_.promptMoney("Amount")) showOutcome(ctx_.payments.withdraw(s, *a, newKey()));
        })
        .add("4", "Transfer to another user", [&] { transfer(s); })
        .add("5", "Pay merchant", [&] { payMerchant(s); })
        .add("6", "Pay by QR / payment identifier", [&] { payQr(s); })
        .add("7", "Pay a bill", [&] { payBill(s); })
        .add("8", "Transaction history", [&] { showHistory(s); })
        .add("9", "Transaction details & ledger", [&] { showTransaction(s); })
        .add("10", "Request a refund", [&] { requestRefund(s); })
        .add("11", "My refund requests", [&] { myRefundRequests(s); })
        .add("12", "Notifications", [&] { showNotifications(s); })
        .add("13", "Profile", [&] { showProfile(s); });
    m.run(con_, "0", "Logout", [&] { return unreadHeader(s); });
    ctx_.auth.logout(s);
    if (!con_.eof()) con_.success("Logged out.");
}

void CustomerController::addMoney(const Session& s) {
    con_.info("Source: 1) Bank account  2) Debit card  3) Credit card  4) Cash deposit");
    std::string choice = con_.prompt("Choose source");
    static const char* kinds[] = {"BANK_ACCOUNT", "DEBIT_CARD", "CREDIT_CARD", "CASH_DEPOSIT"};
    if (choice.size() != 1 || choice[0] < '1' || choice[0] > '4') { con_.error("Invalid source."); return; }
    const char* kind = kinds[choice[0] - '1'];
    std::string ref = con_.prompt(choice == "4" ? "Agent code" : "Account / card number");
    auto method = PaymentMethodFactory::create(kind, ref);
    if (!report(method)) return;
    if (auto a = con_.promptMoney("Amount")) showOutcome(ctx_.payments.addMoney(s, *a, *method.value(), newKey()));
}

void CustomerController::transfer(const Session& s) {
    std::string to = con_.prompt("Recipient email");
    if (to.empty()) return;
    auto a = con_.promptMoney("Amount");
    if (!a) return;
    std::string note = con_.prompt("Note (optional)");
    showOutcome(ctx_.payments.transfer(s, to, *a, newKey(), note));
}

void CustomerController::payMerchant(const Session& s) {
    std::string id = con_.prompt("Merchant payment ID (e.g. MER-0001)");
    if (id.empty()) return;
    auto a = con_.promptMoney("Amount");
    if (!a) return;
    std::string order = con_.prompt("Order reference (optional)");
    showOutcome(ctx_.payments.payMerchant(s, id, *a, newKey(), "", order));
}

void CustomerController::payQr(const Session& s) {
    std::string qr = con_.prompt("Paste payment identifier (NETRAPAY://merchant/...)");
    if (qr.empty()) return;
    auto parsed = PaymentIdentifier::parse(qr);
    if (!report(parsed)) return;
    con_.info("  Merchant: " + parsed.value().merchantPaymentId + "   Order: " + parsed.value().orderId);
    std::optional<Money> amount;
    if (!parsed.value().amount) {
        amount = con_.promptMoney("Amount");
        if (!amount) return;
    } else {
        con_.info("  Amount: " + parsed.value().amount->toString());
    }
    if (!con_.confirm("Confirm payment?")) return;
    showOutcome(ctx_.payments.payQr(s, qr, amount, newKey()));
}

void CustomerController::payBill(const Session& s) {
    std::string biller = con_.prompt("Biller name (e.g. City Power)");
    std::string account = con_.prompt("Consumer / account number");
    if (biller.empty() || account.empty()) { con_.error("Biller and account number are required."); return; }
    if (auto a = con_.promptMoney("Amount")) showOutcome(ctx_.payments.payBill(s, biller, account, *a, newKey()));
}

void CustomerController::requestRefund(const Session& s) {
    std::string id = con_.prompt("Transaction ID of the payment");
    if (id.empty()) return;
    auto a = con_.promptMoney("Refund amount");
    if (!a) return;
    std::string reason = con_.prompt("Reason");
    auto r = ctx_.refunds.requestRefund(s, id, *a, reason);
    if (report(r)) con_.success("Refund request " + r.value().id + " sent to the merchant.");
}

void CustomerController::myRefundRequests(const Session& s) {
    auto list = ctx_.refunds.requestsFor(s);
    con_.section("Refund requests");
    if (list.empty()) con_.info("  (none)");
    for (const auto& r : list)
        con_.info("  " + r.id + "  payment " + r.transactionId + "  " + r.amount.toString() + "  " + toString(r.status) +
                  (r.refundTransactionId.empty() ? "" : "  -> " + r.refundTransactionId));
}

// ====================== merchant ======================
void MerchantController::run(const Session& s) {
    Menu m("MERCHANT PORTAL");
    m.add("1", "Wallet balance", [&] { auto w = ctx_.query.myWallet(s); if (report(w)) con_.info(fmt_.wallet(w.value())); })
        .add("2", "My payment ID / generate QR identifier", [&] { showPaymentId(s); })
        .add("3", "Transaction history", [&] { showHistory(s); })
        .add("4", "Transaction details & ledger", [&] { showTransaction(s); })
        .add("5", "Issue refund", [&] { issueRefund(s); })
        .add("6", "Customer refund requests", [&] { refundRequests(s); })
        .add("7", "Withdraw settlement to bank", [&] {
            if (auto a = con_.promptMoney("Amount")) showOutcome(ctx_.payments.withdraw(s, *a, newKey()));
        })
        .add("8", "Notifications", [&] { showNotifications(s); })
        .add("9", "Profile", [&] { showProfile(s); });
    m.run(con_, "0", "Logout", [&] { return unreadHeader(s); });
    ctx_.auth.logout(s);
    if (!con_.eof()) con_.success("Logged out.");
}

void MerchantController::showPaymentId(const Session& s) {
    auto merchant = std::dynamic_pointer_cast<Merchant>(ctx_.db->users().findById(s.userId));
    if (!merchant) return;
    con_.info("  Your payment ID: " + merchant->paymentId() + "  (" + merchant->businessName() + ")");
    std::string order = con_.prompt("Order id for a QR identifier (Enter to skip)");
    if (order.empty()) return;
    std::optional<Money> amount = con_.promptMoney("Fixed amount (Enter for open amount)");
    con_.info("  " + PaymentIdentifier::generate(merchant->paymentId(), order, amount));
}

void MerchantController::issueRefund(const Session& s) {
    std::string id = con_.prompt("Transaction ID to refund");
    if (id.empty()) return;
    auto a = con_.promptMoney("Refund amount");
    if (!a) return;
    std::string reason = con_.prompt("Reason");
    auto r = ctx_.refunds.issueRefund(s, id, *a, reason, newKey());
    if (report(r)) con_.success("Refund " + r.value().id() + " of " + a->toString() + " sent to the customer.");
}

void MerchantController::refundRequests(const Session& s) {
    auto list = ctx_.refunds.requestsFor(s);
    con_.section("Refund requests");
    if (list.empty()) { con_.info("  (none)"); return; }
    for (const auto& r : list)
        con_.info("  " + r.id + "  payment " + r.transactionId + "  " + r.amount.toString() + "  " + toString(r.status) + "  \"" + r.reason + "\"");
    std::string id = con_.prompt("Request ID to resolve (Enter to go back)");
    if (id.empty()) return;
    std::string action = con_.prompt("Approve or reject? (a/r)");
    if (action == "a") {
        auto r = ctx_.refunds.approveRequest(s, id);
        if (report(r)) con_.success("Approved. Refund transaction " + r.value().id());
    } else if (action == "r") {
        if (report(ctx_.refunds.rejectRequest(s, id))) con_.success("Request rejected.");
    } else {
        con_.error("Please answer a or r.");
    }
}

// ====================== admin ======================
void AdminController::run(const Session& s) {
    Menu m("ADMIN CONSOLE");
    m.add("1", "List users", [&] {
            auto r = ctx_.admin.listUsers(s);
            if (!report(r)) return;
            for (const auto& u : r.value()) con_.info(fmt_.user(*u));
        })
        .add("2", "View user & wallet", [&] { viewUser(s); })
        .add("3", "Freeze wallet", [&] { freeze(s, true); })
        .add("4", "Unfreeze wallet", [&] { freeze(s, false); })
        .add("5", "Set account status (unlock / suspend)", [&] { setStatus(s); })
        .add("6", "List transactions", [&] { showHistory(s); })
        .add("7", "Transaction details & ledger", [&] { showTransaction(s); })
        .add("8", "Fraud review queue", [&] { reviewQueue(s); })
        .add("9", "View audit log", [&] { auditLog(s); })
        .add("10", "Configure limits", [&] { configureLimits(s); })
        .add("11", "System summary report", [&] {
            auto r = ctx_.reports.renderText(s);
            if (report(r)) con_.out() << r.value();
        })
        .add("12", "Export transactions to CSV", [&] { exportCsv(s); })
        .add("13", "Create another admin", [&] { createAdmin(s); });
    m.run(con_, "0", "Logout", [&] { return unreadHeader(s); });
    ctx_.auth.logout(s);
    if (!con_.eof()) con_.success("Logged out.");
}

void AdminController::viewUser(const Session& s) {
    std::string id = con_.prompt("User ID");
    auto u = ctx_.admin.viewUser(s, id);
    if (!report(u)) return;
    con_.info("  " + u.value()->describe());
    con_.info(fmt_.user(*u.value()));
    if (auto w = ctx_.admin.viewWallet(s, id); w) con_.info(fmt_.wallet(w.value()));
}

void AdminController::freeze(const Session& s, bool freezeIt) {
    std::string id = con_.prompt("User ID");
    if (id.empty()) return;
    auto r = freezeIt ? ctx_.admin.freezeWallet(s, id) : ctx_.admin.unfreezeWallet(s, id);
    if (report(r)) con_.success(freezeIt ? "Wallet frozen." : "Wallet unfrozen.");
}

void AdminController::setStatus(const Session& s) {
    std::string id = con_.prompt("User ID");
    std::string st = con_.prompt("New status (ACTIVE, SUSPENDED, LOCKED)");
    AccountStatus status;
    if (st == "ACTIVE") status = AccountStatus::Active;
    else if (st == "SUSPENDED") status = AccountStatus::Suspended;
    else if (st == "LOCKED") status = AccountStatus::Locked;
    else { con_.error("Unknown status."); return; }
    if (report(ctx_.admin.setUserStatus(s, id, status))) con_.success("Status updated.");
}

void AdminController::reviewQueue(const Session& s) {
    auto q = ctx_.admin.reviewQueue(s);
    if (!report(q)) return;
    con_.section("Transactions awaiting review (highest risk first)");
    if (q.value().empty()) { con_.info("  (queue is empty)"); return; }
    for (const auto& item : q.value()) con_.info(fmt_.review(item));
    std::string id = con_.prompt("Transaction ID to resolve (Enter to go back)");
    if (id.empty()) return;
    std::string action = con_.prompt("Approve or reject? (a/r)");
    if (action == "a") {
        auto r = ctx_.payments.approveReview(s, id);
        if (report(r)) con_.success("Approved and settled: " + r.value().id());
    } else if (action == "r") {
        std::string reason = con_.prompt("Reason");
        auto r = ctx_.payments.rejectReview(s, id, reason);
        if (report(r)) con_.success("Rejected: " + r.value().id());
    } else {
        con_.error("Please answer a or r.");
    }
}

void AdminController::configureLimits(const Session& s) {
    std::string tierText = con_.prompt("Tier (BASIC, PREMIUM, MERCHANT)");
    LimitTier tier;
    if (tierText == "BASIC") tier = LimitTier::Basic;
    else if (tierText == "PREMIUM") tier = LimitTier::Premium;
    else if (tierText == "MERCHANT") tier = LimitTier::Merchant;
    else { con_.error("Unknown tier."); return; }
    const LimitSettings& cur = ctx_.config.limits.at(tier);
    con_.info("  Current: per-transaction " + cur.perTransaction.toString() + ", daily " + cur.dailyAmount.toString() +
              ", count/day " + std::to_string(cur.dailyCount));
    auto per = con_.promptMoney("New per-transaction limit");
    auto daily = con_.promptMoney("New daily amount limit");
    std::string count = con_.prompt("New daily transaction count");
    if (!per || !daily || count.empty() || count.find_first_not_of("0123456789") != std::string::npos || count.size() > 6) {
        con_.error("Invalid input; limits unchanged.");
        return;
    }
    LimitSettings next{*per, *daily, std::stoi(count)};
    if (report(ctx_.admin.configureLimits(s, tier, next))) con_.success("Limits updated (until restart; edit config/app.json to make them permanent).");
}

void AdminController::auditLog(const Session& s) {
    std::string n = con_.prompt("How many recent events (Enter = 25)");
    std::size_t count = 25;
    if (!n.empty() && n.find_first_not_of("0123456789") == std::string::npos && n.size() < 6) count = std::stoul(n);
    auto r = ctx_.admin.auditLog(s, count);
    if (!report(r)) return;
    for (const auto& e : r.value()) con_.info(fmt_.audit(e));
}

void AdminController::exportCsv(const Session& s) {
    auto csv = ctx_.reports.exportCsv(s);
    if (!report(csv)) return;
    std::string path = con_.prompt("Output file (Enter = transactions.csv)");
    if (path.empty()) path = "transactions.csv";
    std::ofstream out(path);
    if (!out) { con_.error("Cannot write " + path); return; }
    out << csv.value();
    con_.success("Wrote " + path);
}

void AdminController::createAdmin(const Session& s) {
    std::string name = con_.prompt("Name");
    std::string email = con_.prompt("Email");
    std::string pw = con_.promptSecret("Password");
    auto r = ctx_.auth.createAdmin(name, email, pw, &s);
    if (report(r)) con_.success("Admin created: " + r.value());
}

// ====================== top level ======================
void Application::run() {
    Menu m("DIGITAL WALLET & PAYMENT SYSTEM");
    m.add("1", "Register as customer", [&] { registerCustomer(); })
        .add("2", "Register as merchant", [&] { registerMerchant(); })
        .add("3", "Login", [&] { login(); });
    m.run(con_, "0", "Exit", [] { return std::string("Educational simulation - no real money or banking is involved."); });
    con_.info("Goodbye.");
}

void Application::registerCustomer() {
    con_.section("Register customer");
    std::string name = con_.prompt("Full name");
    std::string email = con_.prompt("Email");
    std::string phone = con_.prompt("Phone (optional)");
    std::string pw = con_.promptSecret("Password (8+ chars, letters and digits)");
    std::string tier = con_.prompt("Account tier - basic or premium (Enter = basic)");
    auto r = ctx_.auth.registerCustomer(name, email, phone, pw, (tier == "premium" || tier == "PREMIUM") ? AccountTier::Premium : AccountTier::Basic);
    if (r) con_.success("Account created. Your user ID is " + r.value() + ". You can log in now.");
    else con_.error(r.error().message + " [" + toString(r.error().code) + "]");
}

void Application::registerMerchant() {
    con_.section("Register merchant");
    std::string name = con_.prompt("Owner name");
    std::string email = con_.prompt("Email");
    std::string phone = con_.prompt("Phone (optional)");
    std::string business = con_.prompt("Business name");
    std::string category = con_.prompt("Category (e.g. Food, Retail)");
    std::string pw = con_.promptSecret("Password (8+ chars, letters and digits)");
    auto r = ctx_.auth.registerMerchant(name, email, phone, pw, business, category);
    if (r) {
        auto m = std::dynamic_pointer_cast<Merchant>(ctx_.db->users().findById(r.value()));
        con_.success("Merchant account created. User ID " + r.value() + ", payment ID " + (m ? m->paymentId() : "?") + ".");
    } else {
        con_.error(r.error().message + " [" + toString(r.error().code) + "]");
    }
}

void Application::login() {
    std::string email = con_.prompt("Email");
    std::string pw = con_.promptSecret("Password");
    auto s = ctx_.auth.login(email, pw);
    if (!s) {
        con_.error(s.error().message);
        return;
    }
    switch (s.value().role) {
        case Role::Customer: CustomerController(ctx_, con_).run(s.value()); break;
        case Role::Merchant: MerchantController(ctx_, con_).run(s.value()); break;
        case Role::Admin: AdminController(ctx_, con_).run(s.value()); break;
    }
}

} // namespace wallet
