#include "services/AuditLogger.h"

namespace wallet {

void AuditLogger::record(const std::string& actor, const std::string& action, const std::string& target,
                         const std::string& result, std::map<std::string, std::string> metadata) {
    AuditEvent e;
    e.id = ids_.next("AUD", 6);
    e.timestamp = clock_.now();
    e.actor = actor.empty() ? "SYSTEM" : actor;
    e.action = action;
    e.target = target;
    e.result = result;
    e.metadata = std::move(metadata);
    repo_.append(e);
}

} // namespace wallet
