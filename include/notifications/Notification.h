#pragma once
#include <map>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <vector>

#include "utils/TimeUtil.h"

namespace wallet {

struct Notification {
    std::string userId;
    std::string recipientName;
    std::string email;
    std::string phone;
    std::string title;
    std::string body;
    Timestamp timestamp = 0;
};

// Abstraction: each channel renders/delivers a notification its own way. Real delivery is
// out of scope; the simulated channels write a formatted line to a stream.
class NotificationService {
public:
    virtual ~NotificationService() = default;
    virtual std::string channel() const = 0;
    virtual void notify(const Notification& n) = 0;
};

class ConsoleNotification : public NotificationService {
public:
    explicit ConsoleNotification(std::ostream& out) : out_(out) {}
    std::string channel() const override { return "CONSOLE"; }
    void notify(const Notification& n) override;
private:
    std::ostream& out_;
};

class EmailNotification : public NotificationService {
public:
    explicit EmailNotification(std::ostream& out) : out_(out) {}
    std::string channel() const override { return "EMAIL"; }
    void notify(const Notification& n) override;
private:
    std::ostream& out_;
};

class SmsNotification : public NotificationService {
public:
    explicit SmsNotification(std::ostream& out) : out_(out) {}
    std::string channel() const override { return "SMS"; }
    void notify(const Notification& n) override;
private:
    std::ostream& out_;
};

class PushNotification : public NotificationService {
public:
    explicit PushNotification(std::ostream& out) : out_(out) {}
    std::string channel() const override { return "PUSH"; }
    void notify(const Notification& n) override;
private:
    std::ostream& out_;
};

// In-app inbox: what the CLI "Notifications" menu shows. Kept in memory for the session.
class InAppNotification : public NotificationService {
public:
    std::string channel() const override { return "IN_APP"; }
    void notify(const Notification& n) override { inbox_[n.userId].push_back(n); }
    const std::vector<Notification>& inboxFor(const std::string& userId) const;
    std::size_t unreadCount(const std::string& userId) const;
    void markAllRead(const std::string& userId) { read_[userId] = inboxFor(userId).size(); }
private:
    std::map<std::string, std::vector<Notification>> inbox_;
    std::map<std::string, std::size_t> read_;
};

// Observer pattern: services publish events; every subscribed channel is told.
// Events are queued and delivered by dispatch(), keeping delivery out of the payment's critical path.
class EventPublisher {
public:
    void subscribe(std::shared_ptr<NotificationService> s) { subscribers_.push_back(std::move(s)); }
    void publish(Notification n) { queue_.push(std::move(n)); }
    void dispatch();
    std::size_t pending() const noexcept { return queue_.size(); }
    std::size_t subscriberCount() const noexcept { return subscribers_.size(); }
private:
    std::vector<std::shared_ptr<NotificationService>> subscribers_;
    std::queue<Notification> queue_;
};

} // namespace wallet
