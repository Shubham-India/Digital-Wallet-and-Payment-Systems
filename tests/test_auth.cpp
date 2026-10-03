// Registration, login, lockout, sessions, RBAC.
#include "TestHelpers.h"

using namespace testenv;

TEST(register_customer_creates_wallet_and_hashes_password) {
    Env e;
    auto id = e.addCustomer("Alice", "alice@example.com");
    auto user = e.ctx->db->users().findById(id);
    CHECK(user != nullptr);
    CHECK(user->role() == Role::Customer);
    CHECK(!user->walletId().empty());
    CHECK(e.ctx->db->wallets().find(user->walletId()).has_value());
    CHECK(user->credentials().hash() != kPassword);               // never stored in clear
    CHECK(user->credentials().hash().find(kPassword) == std::string::npos);
    CHECK_EQ(user->credentials().hash().size(), 64u);
}

TEST(register_validates_input) {
    Env e;
    auto& a = e.ctx->auth;
    CHECK(a.registerCustomer("", "a@b.com", "", kPassword).error().code == ErrorCode::ValidationFailed);
    CHECK(a.registerCustomer("A", "not-an-email", "", kPassword).error().code == ErrorCode::ValidationFailed);
    CHECK(a.registerCustomer("A", "a@b.com", "12ab", kPassword).error().code == ErrorCode::ValidationFailed);
    CHECK(a.registerCustomer("A", "a@b.com", "", "short1").error().code == ErrorCode::ValidationFailed);
    CHECK(a.registerCustomer("A", "a@b.com", "", "onlyletters").error().code == ErrorCode::ValidationFailed);
    CHECK(a.registerMerchant("M", "m@b.com", "", kPassword, "", "Food").error().code == ErrorCode::ValidationFailed);
}

TEST(register_rejects_duplicate_email_case_insensitively) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    auto r = e.ctx->auth.registerCustomer("Other", "ALICE@Example.com", "", kPassword);
    CHECK(!r.ok());
    CHECK(r.error().code == ErrorCode::DuplicateUser);
}

TEST(login_success_and_logout) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    auto s = e.ctx->auth.login("alice@example.com", kPassword);
    CHECK(s.ok());
    CHECK(e.ctx->sessions.valid(s.value()));
    e.ctx->auth.logout(s.value());
    CHECK(!e.ctx->sessions.valid(s.value()));
}

TEST(login_invalid_credentials_do_not_reveal_which_part_was_wrong) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    auto wrongPw = e.ctx->auth.login("alice@example.com", "WrongPass1");
    auto unknown = e.ctx->auth.login("nobody@example.com", kPassword);
    CHECK(wrongPw.error().code == ErrorCode::InvalidCredentials);
    CHECK(unknown.error().code == ErrorCode::InvalidCredentials);
    CHECK_EQ(wrongPw.error().message, unknown.error().message);
}

TEST(account_locks_after_repeated_failures_and_admin_can_unlock) {
    Env e;
    auto id = e.addCustomer("Alice", "alice@example.com");
    int max = e.ctx->config.maxFailedLogins;
    for (int i = 1; i < max; ++i)
        CHECK(e.ctx->auth.login("alice@example.com", "WrongPass1").error().code == ErrorCode::InvalidCredentials);
    CHECK(e.ctx->auth.login("alice@example.com", "WrongPass1").error().code == ErrorCode::AccountLocked);
    // even the right password is refused now
    CHECK(e.ctx->auth.login("alice@example.com", kPassword).error().code == ErrorCode::AccountLocked);

    Session admin = e.admin();
    CHECK(e.ctx->admin.setUserStatus(admin, id, AccountStatus::Active).ok());
    CHECK(e.ctx->auth.login("alice@example.com", kPassword).ok());
}

TEST(successful_login_resets_failed_counter) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    for (int i = 0; i < e.ctx->config.maxFailedLogins - 1; ++i) (void)e.ctx->auth.login("alice@example.com", "WrongPass1");
    CHECK(e.ctx->auth.login("alice@example.com", kPassword).ok());
    for (int i = 0; i < e.ctx->config.maxFailedLogins - 1; ++i) (void)e.ctx->auth.login("alice@example.com", "WrongPass1");
    CHECK(e.ctx->auth.login("alice@example.com", kPassword).ok());  // still not locked
}

TEST(suspended_user_cannot_login) {
    Env e;
    auto id = e.addCustomer("Alice", "alice@example.com");
    CHECK(e.ctx->admin.setUserStatus(e.admin(), id, AccountStatus::Suspended).ok());
    CHECK(e.ctx->auth.login("alice@example.com", kPassword).error().code == ErrorCode::AccountInactive);
}

TEST(default_admin_is_bootstrapped_and_self_registration_cannot_make_admins) {
    Env e;
    CHECK(e.ctx->auth.login(kDefaultAdminEmail, kDefaultAdminPassword).ok());
    e.addCustomer("Alice", "alice@example.com");
    Session alice = e.login("alice@example.com");
    CHECK(e.ctx->auth.createAdmin("Evil", "evil@example.com", kPassword, &alice).error().code == ErrorCode::Unauthorized);
    Session admin = e.admin();
    CHECK(e.ctx->auth.createAdmin("Second", "second@example.com", kPassword, &admin).ok());
}

TEST(rbac_matrix_matches_documented_permissions) {
    using wallet::AccessControl;
    CHECK(AccessControl::roleHas(Role::Customer, Permission::Transfer));
    CHECK(AccessControl::roleHas(Role::Customer, Permission::RequestRefund));
    CHECK(!AccessControl::roleHas(Role::Customer, Permission::IssueRefund));
    CHECK(!AccessControl::roleHas(Role::Customer, Permission::FreezeWallets));
    CHECK(AccessControl::roleHas(Role::Merchant, Permission::IssueRefund));
    CHECK(!AccessControl::roleHas(Role::Merchant, Permission::Transfer));
    CHECK(!AccessControl::roleHas(Role::Merchant, Permission::ViewAudit));
    CHECK(AccessControl::roleHas(Role::Admin, Permission::ViewAudit));
    CHECK(AccessControl::roleHas(Role::Admin, Permission::ReviewFraud));
    CHECK(!AccessControl::roleHas(Role::Admin, Permission::Transfer));
}

TEST(rbac_enforced_by_services) {
    Env e;
    auto aliceId = e.addCustomer("Alice", "alice@example.com");
    e.addMerchant("Cafe", "cafe@example.com");
    Session alice = e.login("alice@example.com");
    Session cafe = e.login("cafe@example.com");
    Session admin = e.admin();

    CHECK(e.ctx->admin.freezeWallet(alice, aliceId).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->admin.auditLog(alice).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->admin.listUsers(cafe).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->reports.summary(alice).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->payments.transfer(cafe, "alice@example.com", rupees(10), e.key()).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->payments.transfer(admin, "alice@example.com", rupees(10), e.key()).error().code == ErrorCode::Unauthorized);
    CHECK(e.ctx->admin.listUsers(admin).ok());
}

TEST(logged_out_session_is_rejected_everywhere) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    e.addCustomer("Bob", "bob@example.com");
    Session alice = e.login("alice@example.com");
    e.ctx->auth.logout(alice);
    auto r = e.ctx->payments.transfer(alice, "bob@example.com", rupees(10), e.key());
    CHECK(r.error().code == ErrorCode::NotAuthenticated);
    CHECK(e.ctx->query.myWallet(alice).error().code == ErrorCode::NotAuthenticated);
    Session forged = alice;
    forged.token = "SES-forged";
    CHECK(!e.ctx->sessions.valid(forged));
}

TEST(audit_log_records_login_events_without_secrets) {
    Env e;
    e.addCustomer("Alice", "alice@example.com");
    (void)e.ctx->auth.login("alice@example.com", "WrongPass1");
    Session s = e.login("alice@example.com");
    e.ctx->auth.logout(s);
    auto events = e.ctx->db->audit().all();
    bool failedLogin = false, okLogin = false, logout = false;
    for (const auto& ev : events) {
        failedLogin |= ev.action == "LOGIN" && ev.result == "FAILURE";
        okLogin |= ev.action == "LOGIN" && ev.result == "SUCCESS";
        logout |= ev.action == "LOGOUT";
        for (const auto& [k, v] : ev.metadata) {
            CHECK(v.find("WrongPass1") == std::string::npos);
            CHECK(v.find(kPassword) == std::string::npos);
        }
    }
    CHECK(failedLogin); CHECK(okLogin); CHECK(logout);
}
