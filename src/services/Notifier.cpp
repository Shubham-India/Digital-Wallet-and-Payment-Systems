#include "services/Notifier.h"

namespace wallet {

void Notifier::toUser(const std::string& userId, const std::string& title, const std::string& body) {
    auto user = users_.findById(userId);
    if (!user) return;  // external parties (banks, billers) have no account
    Notification n;
    n.userId = user->id();
    n.recipientName = user->name();
    n.email = user->email();
    n.phone = user->phone();
    n.title = title;
    n.body = body;
    n.timestamp = clock_.now();
    events_.publish(std::move(n));
}

} // namespace wallet
