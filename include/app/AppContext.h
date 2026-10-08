#pragma once
// Composition root: the only place that knows every concrete class and wires the object graph
// (dependency injection by hand, no global state, no singletons).
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "infrastructure/Config.h"
#include "infrastructure/Logger.h"
#include "infrastructure/PasswordHasher.h"
#include "notifications/Notification.h"
#include "policies/FraudEngine.h"
#include "repositories/Repositories.h"
#include "services/AccessControl.h"
#include "services/AdminService.h"
#include "services/AuditLogger.h"
#include "services/AuthService.h"
#include "services/FraudReviewQueue.h"
#include "services/Notifier.h"
#include "services/PaymentService.h"
#include "services/QueryService.h"
#include "services/RefundService.h"
#include "services/ReportService.h"
#include "services/SettlementService.h"

namespace wallet {

struct AppOptions {
    std::string dataDir = "data";
    std::string configPath = "config/app.json";
    bool inMemory = false;           // tests: no files at all
    bool consoleNotifications = false;  // demo: also print notifications to stdout
    bool fileLogging = true;
    int passwordIterations = 0;      // 0 = use config value; tests lower it for speed
};

// Default demo administrator created on first start (documented in the README; change it in real use).
inline const char* kDefaultAdminEmail = "admin@wallet.local";
inline const char* kDefaultAdminPassword = "Admin@1234";

class AppContext {
public:
    // Loads config + data, seeds id counters, rebuilds the review queue, ensures an admin exists.
    static Result<std::unique_ptr<AppContext>> create(const AppOptions& options, const IClock& clock);

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    AppConfig config;
    const IClock& clock;
    IdGenerator ids;
    std::unique_ptr<Database> db;
    std::ofstream logFile;
    std::ofstream outboxFile;
    std::ostringstream discardedOutbox;
    Logger log;
    Pbkdf2PasswordHasher hasher;
    SessionManager sessions;
    AccessControl access;
    AuditLogger audit;
    EventPublisher events;
    std::shared_ptr<InAppNotification> inbox;
    Notifier notifier;
    SettlementService settlement;
    FraudEngine fraud;
    FraudReviewQueue reviewQueue;
    AuthService auth;
    PaymentService payments;
    RefundService refunds;
    AdminService admin;
    QueryService query;
    ReportService reports;

private:
    AppContext(AppConfig cfg, const IClock& clk, std::unique_ptr<Database> database, const AppOptions& options);
    static std::ostream* openLog(std::ofstream& f, const AppOptions& o, const std::string& dir);
};

}
