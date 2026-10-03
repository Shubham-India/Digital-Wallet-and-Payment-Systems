#pragma once
#include <string>

#include "infrastructure/Clock.h"
#include "infrastructure/Config.h"
#include "infrastructure/IdGenerator.h"
#include "infrastructure/Logger.h"
#include "infrastructure/PasswordHasher.h"
#include "repositories/Repositories.h"
#include "services/AccessControl.h"
#include "services/AuditLogger.h"

namespace wallet {

// Registration, login/logout and account lockout.
// Educational authentication: passwords are salted + PBKDF2-hashed, never stored or logged in clear,
// but there is no MFA, rate limiting by IP, or secure transport. Not production-grade security.
class AuthService {
public:
    AuthService(Database& db, SessionManager& sessions, const PasswordHasher& hasher, AuditLogger& audit,
                IdGenerator& ids, const IClock& clock, const AppConfig& config, const Logger& log)
        : db_(db), sessions_(sessions), hasher_(hasher), audit_(audit), ids_(ids), clock_(clock), config_(config), log_(log) {}

    Result<std::string> registerCustomer(const std::string& name, const std::string& email, const std::string& phone,
                                         const std::string& password, AccountTier tier = AccountTier::Basic);
    Result<std::string> registerMerchant(const std::string& name, const std::string& email, const std::string& phone,
                                         const std::string& password, const std::string& businessName,
                                         const std::string& category);
    // Admins are created by the system bootstrap or by another admin; never by self-registration.
    Result<std::string> createAdmin(const std::string& name, const std::string& email, const std::string& password,
                                    const Session* creator = nullptr);

    Result<Session> login(const std::string& email, const std::string& password);
    void logout(const Session& session);

private:
    Result<void> validateCommon(const std::string& name, const std::string& email, const std::string& phone,
                                const std::string& password) const;
    Credentials makeCredentials(const std::string& password) const;
    Result<std::string> finishRegistration(std::shared_ptr<User> user, bool withWallet);

    Database& db_;
    SessionManager& sessions_;
    const PasswordHasher& hasher_;
    AuditLogger& audit_;
    IdGenerator& ids_;
    const IClock& clock_;
    const AppConfig& config_;
    const Logger& log_;
};

} // namespace wallet
