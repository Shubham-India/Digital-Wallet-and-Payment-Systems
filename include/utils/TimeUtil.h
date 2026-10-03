#pragma once
#include <cstdint>
#include <string>

namespace wallet {

using Timestamp = std::int64_t;  // seconds since Unix epoch (UTC)
constexpr Timestamp kSecondsPerDay = 86400;

std::string formatTimestamp(Timestamp t);  // "YYYY-MM-DD HH:MM:SS"
std::string formatDate(Timestamp t);       // "YYYY-MM-DD"
Timestamp startOfDay(Timestamp t);         // 00:00:00 UTC of the same day

} // namespace wallet
