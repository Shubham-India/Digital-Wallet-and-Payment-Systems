#pragma once
// Educational rule-based risk checks. NOT a real fraud model: thresholds are arbitrary
// configuration values, not derived from real-world fraud data.
#include <memory>
#include <string>
#include <vector>

#include "domain/Money.h"
#include "domain/Transaction.h"
#include "infrastructure/Config.h"

namespace wallet {

enum class RiskDecision { Approve = 0, Review = 1, Reject = 2 };
const char* toString(RiskDecision d);

struct RiskContext {
    std::string userId;
    Money amount;
    Timestamp now = 0;
    std::vector<Transaction> recent;  // the payer's transactions, oldest first, excluding the candidate
};

struct RuleResult {
    RiskDecision decision = RiskDecision::Approve;
    int score = 0;  // 0..100 contribution
    std::string reason;
};

struct RiskAssessment {
    RiskDecision decision = RiskDecision::Approve;
    int score = 0;  // sum of rule scores, capped at 100; orders the review queue
    std::vector<std::string> reasons;
};

// Strategy interface: each rule inspects the context independently.
class FraudRule {
public:
    virtual ~FraudRule() = default;
    virtual RuleResult evaluate(const RiskContext& ctx) const = 0;
    virtual std::string name() const = 0;
};

class LargeAmountRule : public FraudRule {
public:
    LargeAmountRule(Money reviewAt, Money rejectAt) : reviewAt_(reviewAt), rejectAt_(rejectAt) {}
    RuleResult evaluate(const RiskContext& ctx) const override;
    std::string name() const override { return "LargeAmountRule"; }
private:
    Money reviewAt_, rejectAt_;
};

// Sliding window over the payer's recent attempts (time-ordered, scanned from the newest).
class VelocityRule : public FraudRule {
public:
    VelocityRule(int windowSeconds, int reviewCount, int rejectCount)
        : window_(windowSeconds), reviewCount_(reviewCount), rejectCount_(rejectCount) {}
    RuleResult evaluate(const RiskContext& ctx) const override;
    std::string name() const override { return "VelocityRule"; }
private:
    int window_, reviewCount_, rejectCount_;
};

class RepeatedFailureRule : public FraudRule {
public:
    RepeatedFailureRule(int windowSeconds, int reviewCount) : window_(windowSeconds), reviewCount_(reviewCount) {}
    RuleResult evaluate(const RiskContext& ctx) const override;
    std::string name() const override { return "RepeatedFailureRule"; }
private:
    int window_, reviewCount_;
};

// Composite: runs every rule; the strictest decision wins, scores add up.
class FraudEngine {
public:
    void addRule(std::unique_ptr<FraudRule> rule) { rules_.push_back(std::move(rule)); }
    RiskAssessment assess(const RiskContext& ctx) const;
    std::size_t ruleCount() const noexcept { return rules_.size(); }
    static FraudEngine standard(const FraudSettings& settings);
private:
    std::vector<std::unique_ptr<FraudRule>> rules_;
};

} // namespace wallet
