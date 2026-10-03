#include "utils/TimeUtil.h"

#include <cstdio>

namespace wallet {

static std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Civil-from-days algorithm (public domain, H. Hinnant); avoids non-portable gmtime variants.
static void civil(Timestamp t, int& y, unsigned& m, unsigned& d, int& hh, int& mm, int& ss) {
    std::int64_t days = floorDiv(t, kSecondsPerDay);
    std::int64_t rem = t - days * kSecondsPerDay;
    hh = static_cast<int>(rem / 3600);
    mm = static_cast<int>(rem % 3600 / 60);
    ss = static_cast<int>(rem % 60);
    days += 719468;
    std::int64_t era = floorDiv(days, 146097);
    unsigned doe = static_cast<unsigned>(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(yy + (m <= 2 ? 1 : 0));
}

std::string formatDate(Timestamp t) {
    int y, hh, mm, ss;
    unsigned mo, d;
    civil(t, y, mo, d, hh, mm, ss);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", y, mo, d);
    return buf;
}

std::string formatTimestamp(Timestamp t) {
    int y, hh, mm, ss;
    unsigned mo, d;
    civil(t, y, mo, d, hh, mm, ss);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u %02d:%02d:%02d", y, mo, d, hh, mm, ss);
    return buf;
}

Timestamp startOfDay(Timestamp t) { return floorDiv(t, kSecondsPerDay) * kSecondsPerDay; }

} // namespace wallet
