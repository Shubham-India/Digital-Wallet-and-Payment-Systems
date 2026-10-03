#pragma once
#include <ostream>
#include <string>

#include "infrastructure/Clock.h"

namespace wallet {

enum class LogLevel { Debug, Info, Warning, Error };
const char* toString(LogLevel l);
LogLevel parseLogLevel(const std::string& s);

// Application log (diagnostics). Distinct from the AuditLogger (security/business events).
// Rule: callers never pass passwords/PINs/hashes; log IDs and outcomes only.
class Logger {
public:
    Logger(std::ostream* sink, LogLevel min, const IClock& clock) : sink_(sink), min_(min), clock_(clock) {}
    void log(LogLevel level, const std::string& component, const std::string& message) const;
    void debug(const std::string& c, const std::string& m) const { log(LogLevel::Debug, c, m); }
    void info(const std::string& c, const std::string& m) const { log(LogLevel::Info, c, m); }
    void warning(const std::string& c, const std::string& m) const { log(LogLevel::Warning, c, m); }
    void error(const std::string& c, const std::string& m) const { log(LogLevel::Error, c, m); }
private:
    std::ostream* sink_;  // may be null: logging disabled
    LogLevel min_;
    const IClock& clock_;
};

} // namespace wallet
