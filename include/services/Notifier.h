#pragma once
#include <string>

#include "infrastructure/Clock.h"
#include "notifications/Notification.h"
#include "repositories/Repositories.h"

namespace wallet {

// Small helper so services say "tell user X" without knowing channels or contact details.
class Notifier {
public:
    Notifier(IUserRepository& users, EventPublisher& events, const IClock& clock)
        : users_(users), events_(events), clock_(clock) {}
    void toUser(const std::string& userId, const std::string& title, const std::string& body);
    void flush() { events_.dispatch(); }
private:
    IUserRepository& users_;
    EventPublisher& events_;
    const IClock& clock_;
};

} // namespace wallet
