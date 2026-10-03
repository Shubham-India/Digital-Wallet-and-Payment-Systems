#include "infrastructure/Logger.h"

namespace wallet {

const char* toString(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

LogLevel parseLogLevel(const std::string& s) {
    if (s == "DEBUG") return LogLevel::Debug;
    if (s == "WARNING" || s == "WARN") return LogLevel::Warning;
    if (s == "ERROR") return LogLevel::Error;
    return LogLevel::Info;
}

void Logger::log(LogLevel level, const std::string& component, const std::string& message) const {
    if (!sink_ || level < min_) return;
    *sink_ << formatTimestamp(clock_.now()) << " [" << toString(level) << "] " << component << ": " << message << '\n';
    sink_->flush();
}

} // namespace wallet
