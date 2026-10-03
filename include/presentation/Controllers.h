#pragma once
#include <optional>
#include <string>

#include "app/AppContext.h"
#include "presentation/Console.h"
#include "presentation/Formatter.h"

namespace wallet {

// Controllers translate keystrokes into service calls and service results into text.
// They contain NO business rules: every decision (limits, risk, permissions) lives in services.
class ControllerBase {
protected:
    ControllerBase(AppContext& ctx, Console& console) : ctx_(ctx), con_(console), fmt_(ctx) {}
    std::string newKey();                                   // fresh random idempotency key per user action
    template <class R>
    bool report(const R& r) {
        if (r.ok()) return true;
        con_.error(r.error().message + " [" + toString(r.error().code) + "]");
        return false;
    }
    void showOutcome(const Result<PaymentOutcome>& r);
    TransactionFilter promptFilter();
    void showNotifications(const Session& s);
    void showProfile(const Session& s);
    void showHistory(const Session& s);
    void showTransaction(const Session& s);
    std::string unreadHeader(const Session& s);

    AppContext& ctx_;
    Console& con_;
    Formatter fmt_;
};

class CustomerController : ControllerBase {
public:
    CustomerController(AppContext& ctx, Console& console) : ControllerBase(ctx, console) {}
    void run(const Session& s);
private:
    void addMoney(const Session& s);
    void transfer(const Session& s);
    void payMerchant(const Session& s);
    void payQr(const Session& s);
    void payBill(const Session& s);
    void requestRefund(const Session& s);
    void myRefundRequests(const Session& s);
};

class MerchantController : ControllerBase {
public:
    MerchantController(AppContext& ctx, Console& console) : ControllerBase(ctx, console) {}
    void run(const Session& s);
private:
    void showPaymentId(const Session& s);
    void issueRefund(const Session& s);
    void refundRequests(const Session& s);
};

class AdminController : ControllerBase {
public:
    AdminController(AppContext& ctx, Console& console) : ControllerBase(ctx, console) {}
    void run(const Session& s);
private:
    void viewUser(const Session& s);
    void freeze(const Session& s, bool freezeIt);
    void setStatus(const Session& s);
    void reviewQueue(const Session& s);
    void configureLimits(const Session& s);
    void auditLog(const Session& s);
    void exportCsv(const Session& s);
    void createAdmin(const Session& s);
};

// Top-level menu: register / login, then hand over to the role's controller.
class Application {
public:
    Application(AppContext& ctx, Console& console) : ctx_(ctx), con_(console) {}
    void run();
private:
    void registerCustomer();
    void registerMerchant();
    void login();
    AppContext& ctx_;
    Console& con_;
};

} // namespace wallet
