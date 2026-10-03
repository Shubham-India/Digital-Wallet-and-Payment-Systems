#pragma once
#include <memory>
#include <string>

#include "TestFramework.h"
#include "app/AppContext.h"

namespace testenv {

using namespace wallet;

inline Money rupees(std::int64_t r) { return Money::fromMinor(r * 100); }
inline const std::string kPassword = "Passw0rd!";

// A fresh in-memory application with a controllable clock. Every helper advances the clock a little
// so tests do not accidentally trip the velocity rule unless they mean to.
struct Env {
    ManualClock clock;
    std::unique_ptr<AppContext> ctx;
    int keyCounter = 0;

    Env() {
        AppOptions o;
        o.inMemory = true;
        o.passwordIterations = 10;
        auto r = AppContext::create(o, clock);
        ctx = std::move(r).value();
    }

    std::string key() { return "key-" + std::to_string(++keyCounter); }
    void tick(Timestamp s = 20) { clock.advance(s); }

    std::string addCustomer(const std::string& name, const std::string& email, AccountTier tier = AccountTier::Basic) {
        return ctx->auth.registerCustomer(name, email, "9876543210", kPassword, tier).value();
    }
    std::string addMerchant(const std::string& name, const std::string& email) {
        return ctx->auth.registerMerchant(name, email, "9123456780", kPassword, name + " Store", "Retail").value();
    }
    Session login(const std::string& email) { return ctx->auth.login(email, kPassword).value(); }
    Session admin() { return ctx->auth.login(kDefaultAdminEmail, kDefaultAdminPassword).value(); }

    std::string paymentIdOf(const std::string& merchantUserId) {
        return std::dynamic_pointer_cast<Merchant>(ctx->db->users().findById(merchantUserId))->paymentId();
    }
    Money balanceOf(const std::string& userId) {
        return ctx->db->wallets().find(ctx->db->users().findById(userId)->walletId())->balance();
    }

    // Adds money in chunks below the Basic per-transaction limit.
    void fund(const Session& s, std::int64_t totalRupees) {
        auto method = PaymentMethodFactory::create("BANK_ACCOUNT", "123456789012").value();
        while (totalRupees > 0) {
            std::int64_t chunk = totalRupees > 20000 ? 20000 : totalRupees;
            tick(1);
            auto r = ctx->payments.addMoney(s, rupees(chunk), *method, key());
            CHECK(r.ok());
            totalRupees -= chunk;
        }
    }
};

}  // namespace testenv
