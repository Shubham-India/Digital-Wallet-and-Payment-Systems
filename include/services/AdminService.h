#pragma once
#include <string>
#include <vector>

#include "infrastructure/Config.h"
#include "repositories/Repositories.h"
#include "services/AccessControl.h"
#include "services/AuditLogger.h"
#include "services/FraudReviewQueue.h"
#include "services/Notifier.h"

namespace wallet {

// Administrative operations. Every method authorizes through AccessControl and writes an audit event.
class AdminService {
public:
    AdminService(Database& db, AccessControl& access, AuditLogger& audit, Notifier& notifier,
                 FraudReviewQueue& reviewQueue, AppConfig& config)
        : db_(db), access_(access), audit_(audit), notifier_(notifier), queue_(reviewQueue), config_(config) {}

    Result<std::vector<std::shared_ptr<User>>> listUsers(const Session& admin) const;
    Result<std::shared_ptr<User>> viewUser(const Session& admin, const std::string& userId) const;
    Result<Wallet> viewWallet(const Session& admin, const std::string& userId) const;
    Result<void> freezeWallet(const Session& admin, const std::string& userId);
    Result<void> unfreezeWallet(const Session& admin, const std::string& userId);
    Result<void> setUserStatus(const Session& admin, const std::string& userId, AccountStatus status);  // also unlocks
    Result<std::vector<AuditEvent>> auditLog(const Session& admin, std::size_t lastN = 0) const;
    Result<std::vector<ReviewItem>> reviewQueue(const Session& admin) const;
    Result<void> configureLimits(const Session& admin, LimitTier tier, const LimitSettings& settings);

private:
    Result<std::shared_ptr<User>> target(const std::string& userId) const;
    Database& db_;
    AccessControl& access_;
    AuditLogger& audit_;
    Notifier& notifier_;
    FraudReviewQueue& queue_;
    AppConfig& config_;
};

} // namespace wallet
