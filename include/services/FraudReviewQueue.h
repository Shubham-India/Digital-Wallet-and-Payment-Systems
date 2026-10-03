#pragma once
#include <optional>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "domain/Transaction.h"

namespace wallet {

struct ReviewItem {
    std::string transactionId;
    int riskScore = 0;
    Timestamp queuedAt = 0;
};

// Priority queue of transactions awaiting manual review: highest risk first, oldest first on ties.
// Resolved items are removed lazily (hash set of active ids), so remove() is O(1) and
// top()/pending() skip stale heap entries.
class FraudReviewQueue {
public:
    void push(const std::string& txId, int riskScore, Timestamp at);
    void remove(const std::string& txId) { active_.erase(txId); }
    bool contains(const std::string& txId) const { return active_.count(txId) > 0; }
    std::size_t size() const noexcept { return active_.size(); }
    std::optional<ReviewItem> top();                 // amortised O(log n)
    std::vector<ReviewItem> pending() const;         // O(n log n), priority order
    void rebuildFrom(const std::vector<Transaction>& txs);  // after a restart: PendingReview transactions

private:
    struct Lower {
        bool operator()(const ReviewItem& a, const ReviewItem& b) const {
            if (a.riskScore != b.riskScore) return a.riskScore < b.riskScore;
            return a.queuedAt > b.queuedAt;
        }
    };
    std::priority_queue<ReviewItem, std::vector<ReviewItem>, Lower> heap_;
    std::unordered_set<std::string> active_;
};

} // namespace wallet
