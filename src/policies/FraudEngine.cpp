#include "policies/FraudEngine.h"

#include <algorithm>

namespace wallet {

const char* toString(RiskDecision d) {
    switch (d) {
        case RiskDecision::Approve: return "APPROVE";
        case RiskDecision::Review: return "REVIEW";
        case RiskDecision::Reject: return "REJECT";
    }
    return "?";
}

RuleResult LargeAmountRule::evaluate(const RiskContext& ctx) const {
    if (ctx.amount >= rejectAt_)
        return {RiskDecision::Reject, 100, "Amount " + ctx.amount.toString() + " is at or above the reject threshold"};
    if (ctx.amount >= reviewAt_)
        return {RiskDecision::Review, 50, "Large amount " + ctx.amount.toString() + " requires manual review"};
    return {};
}

RuleResult VelocityRule::evaluate(const RiskContext& ctx) const {
    // recent is time-ordered, so walking from the back stops at the first out-of-window entry.
    int attempts = 1;  // the candidate itself
    for (auto it = ctx.recent.rbegin(); it != ctx.recent.rend(); ++it) {
        if (it->createdAt() <= ctx.now - window_) break;
        if (it->isOutgoingFor(ctx.userId)) ++attempts;
    }
    if (attempts >= rejectCount_)
        return {RiskDecision::Reject, 100, std::to_string(attempts) + " payment attempts within " + std::to_string(window_) + "s"};
    if (attempts >= reviewCount_)
        return {RiskDecision::Review, 40, std::to_string(attempts) + " payment attempts within " + std::to_string(window_) + "s"};
    return {};
}

RuleResult RepeatedFailureRule::evaluate(const RiskContext& ctx) const {
    int failures = 0;
    for (auto it = ctx.recent.rbegin(); it != ctx.recent.rend(); ++it) {
        if (it->createdAt() <= ctx.now - window_) break;
        if (it->status() == TxStatus::Failed && it->data().payerUserId == ctx.userId) ++failures;
    }
    if (failures >= reviewCount_)
        return {RiskDecision::Review, 30, std::to_string(failures) + " failed payments within " + std::to_string(window_) + "s"};
    return {};
}

RiskAssessment FraudEngine::assess(const RiskContext& ctx) const {
    RiskAssessment out;
    for (const auto& rule : rules_) {
        RuleResult r = rule->evaluate(ctx);
        if (r.decision == RiskDecision::Approve) continue;
        out.decision = std::max(out.decision, r.decision);
        out.score += r.score;
        out.reasons.push_back(rule->name() + ": " + r.reason);
    }
    out.score = std::min(out.score, 100);
    return out;
}

FraudEngine FraudEngine::standard(const FraudSettings& s) {
    FraudEngine e;
    e.addRule(std::make_unique<LargeAmountRule>(s.reviewAmount, s.rejectAmount));
    e.addRule(std::make_unique<VelocityRule>(s.velocityWindowSeconds, s.velocityReviewCount, s.velocityRejectCount));
    e.addRule(std::make_unique<RepeatedFailureRule>(s.failureWindowSeconds, s.failureReviewCount));
    return e;
}

} // namespace wallet
