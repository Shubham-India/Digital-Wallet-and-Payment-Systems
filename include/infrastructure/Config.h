#pragma once
#include <map>
#include <string>

#include "domain/Money.h"
#include "domain/Result.h"

namespace wallet {

// Which limit set applies to an account: Customers use Basic/Premium, merchants use Merchant.
enum class LimitTier { Basic, Premium, Merchant };
const char* toString(LimitTier t);

struct LimitSettings {
    Money perTransaction;
    Money dailyAmount;
    int dailyCount = 0;
};

struct FraudSettings {
    Money reviewAmount;           // >= this: REVIEW
    Money rejectAmount;           // >= this: REJECT
    int velocityWindowSeconds = 60;
    int velocityReviewCount = 5;  // this many outgoing attempts inside the window: REVIEW
    int velocityRejectCount = 10;
    int failureWindowSeconds = 600;
    int failureReviewCount = 3;   // this many failed attempts inside the window: REVIEW
};

// All tunable business values. Defaults are compiled in; config/app.json overrides them.
struct AppConfig {
    std::map<LimitTier, LimitSettings> limits;
    FraudSettings fraud;
    int maxFailedLogins = 5;
    int passwordIterations = 20000;  // PBKDF2 iterations (educational default)
    std::string logLevel = "INFO";
    std::string dataDir = "data";

    static AppConfig defaults();
    static Result<AppConfig> loadFromFile(const std::string& path);  // missing file => error
};

} // namespace wallet
