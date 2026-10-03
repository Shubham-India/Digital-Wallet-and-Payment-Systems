#include "infrastructure/Config.h"

#include <fstream>
#include <sstream>

#include "infrastructure/Json.h"

namespace wallet {

const char* toString(LimitTier t) {
    switch (t) {
        case LimitTier::Basic: return "BASIC";
        case LimitTier::Premium: return "PREMIUM";
        case LimitTier::Merchant: return "MERCHANT";
    }
    return "?";
}

static Money rupees(std::int64_t r) { return Money::fromMinor(r * 100); }

AppConfig AppConfig::defaults() {
    AppConfig c;
    c.limits[LimitTier::Basic] = {rupees(25000), rupees(50000), 20};
    c.limits[LimitTier::Premium] = {rupees(100000), rupees(200000), 50};
    c.limits[LimitTier::Merchant] = {rupees(200000), rupees(1000000), 200};
    c.fraud.reviewAmount = rupees(10000);
    c.fraud.rejectAmount = rupees(150000);
    return c;
}

// Config amounts are whole rupees to keep the file readable; fractional limits are not needed.
static void readMoney(const Json& j, const char* key, Money& target) {
    if (j.has(key)) {
        std::int64_t v = j.integer(key, -1);
        if (v >= 0 && v <= Money::kMaxMinor / 100) target = rupees(v);
    }
}
static void readInt(const Json& j, const char* key, int& target) {
    std::int64_t v = j.integer(key, -1);
    if (v > 0 && v < 100000000) target = static_cast<int>(v);
}

Result<AppConfig> AppConfig::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return fail(ErrorCode::PersistenceFailure, "Cannot open config file " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    auto parsed = Json::parse(ss.str());
    if (!parsed) return fail(parsed.error());
    const Json& root = parsed.value();

    AppConfig c = defaults();
    if (const Json* limits = root.find("limits")) {
        for (LimitTier tier : {LimitTier::Basic, LimitTier::Premium, LimitTier::Merchant}) {
            if (const Json* t = limits->find(toString(tier))) {
                auto& s = c.limits[tier];
                readMoney(*t, "perTransactionRupees", s.perTransaction);
                readMoney(*t, "dailyAmountRupees", s.dailyAmount);
                readInt(*t, "dailyCount", s.dailyCount);
            }
        }
    }
    if (const Json* f = root.find("fraud")) {
        readMoney(*f, "reviewAmountRupees", c.fraud.reviewAmount);
        readMoney(*f, "rejectAmountRupees", c.fraud.rejectAmount);
        readInt(*f, "velocityWindowSeconds", c.fraud.velocityWindowSeconds);
        readInt(*f, "velocityReviewCount", c.fraud.velocityReviewCount);
        readInt(*f, "velocityRejectCount", c.fraud.velocityRejectCount);
        readInt(*f, "failureWindowSeconds", c.fraud.failureWindowSeconds);
        readInt(*f, "failureReviewCount", c.fraud.failureReviewCount);
    }
    readInt(root, "maxFailedLogins", c.maxFailedLogins);
    readInt(root, "passwordIterations", c.passwordIterations);
    c.logLevel = root.str("logLevel", c.logLevel);
    c.dataDir = root.str("dataDir", c.dataDir);
    return c;
}

} // namespace wallet
