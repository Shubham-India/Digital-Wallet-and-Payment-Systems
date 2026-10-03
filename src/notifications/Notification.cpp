#include "notifications/Notification.h"

#include <algorithm>

namespace wallet {

void ConsoleNotification::notify(const Notification& n) {
    out_ << "  [notification] " << n.recipientName << ": " << n.title << " - " << n.body << '\n';
}

void EmailNotification::notify(const Notification& n) {
    out_ << formatTimestamp(n.timestamp) << " EMAIL to=" << n.email << " subject=\"" << n.title << "\" body=\"" << n.body << "\"\n";
}

void SmsNotification::notify(const Notification& n) {
    if (n.phone.empty()) return;  // no phone on file: nothing to send
    out_ << formatTimestamp(n.timestamp) << " SMS to=" << n.phone << " text=\"" << n.title << ": " << n.body << "\"\n";
}

void PushNotification::notify(const Notification& n) {
    out_ << formatTimestamp(n.timestamp) << " PUSH user=" << n.userId << " title=\"" << n.title << "\"\n";
}

const std::vector<Notification>& InAppNotification::inboxFor(const std::string& userId) const {
    static const std::vector<Notification> kEmpty;
    auto it = inbox_.find(userId);
    return it == inbox_.end() ? kEmpty : it->second;
}

std::size_t InAppNotification::unreadCount(const std::string& userId) const {
    auto it = read_.find(userId);
    std::size_t read = it == read_.end() ? 0 : it->second;
    return inboxFor(userId).size() - std::min(read, inboxFor(userId).size());
}

void EventPublisher::dispatch() {
    while (!queue_.empty()) {
        Notification n = std::move(queue_.front());
        queue_.pop();
        for (auto& s : subscribers_) s->notify(n);
    }
}

} // namespace wallet
