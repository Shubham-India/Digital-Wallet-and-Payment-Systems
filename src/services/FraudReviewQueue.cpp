#include "services/FraudReviewQueue.h"

namespace wallet {

void FraudReviewQueue::push(const std::string& txId, int riskScore, Timestamp at) {
    if (!active_.insert(txId).second) return;  // already queued
    heap_.push({txId, riskScore, at});
}

std::optional<ReviewItem> FraudReviewQueue::top() {
    while (!heap_.empty() && !active_.count(heap_.top().transactionId)) heap_.pop();
    if (heap_.empty()) return std::nullopt;
    return heap_.top();
}

std::vector<ReviewItem> FraudReviewQueue::pending() const {
    auto copy = heap_;
    std::vector<ReviewItem> out;
    while (!copy.empty()) {
        if (active_.count(copy.top().transactionId)) out.push_back(copy.top());
        copy.pop();
    }
    return out;
}

void FraudReviewQueue::rebuildFrom(const std::vector<Transaction>& txs) {
    heap_ = {};
    active_.clear();
    for (const auto& t : txs) {
        if (t.status() != TxStatus::PendingReview) continue;
        std::string score = t.meta("riskScore");
        push(t.id(), score.empty() ? 0 : std::stoi(score), t.createdAt());
    }
}

} // namespace wallet
