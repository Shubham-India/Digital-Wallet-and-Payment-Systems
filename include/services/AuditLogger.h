#pragma once
#include <map>
#include <string>

#include "infrastructure/Clock.h"
#include "infrastructure/IdGenerator.h"
#include "repositories/Repositories.h"

namespace wallet {

// Security/audit trail: WHO did WHAT to WHICH target with WHAT result. Append-only.
// (A business transaction log records money movements; the audit log also records logins,
// denied attempts and admin actions that move no money.)
class AuditLogger {
public:
    AuditLogger(IAuditRepository& repo, IdGenerator& ids, const IClock& clock) : repo_(repo), ids_(ids), clock_(clock) {}
    void record(const std::string& actor, const std::string& action, const std::string& target,
                const std::string& result, std::map<std::string, std::string> metadata = {});
private:
    IAuditRepository& repo_;
    IdGenerator& ids_;
    const IClock& clock_;
};

} // namespace wallet
