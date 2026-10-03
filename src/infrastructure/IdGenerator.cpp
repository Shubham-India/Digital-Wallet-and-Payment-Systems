#include "infrastructure/IdGenerator.h"

#include <algorithm>
#include <cctype>

namespace wallet {

std::string IdGenerator::next(const std::string& prefix, int width) {
    std::string num = std::to_string(++counters_[prefix]);
    if (static_cast<int>(num.size()) < width) num.insert(0, width - num.size(), '0');
    return prefix + "-" + num;
}

void IdGenerator::observe(const std::string& id) {
    auto dash = id.rfind('-');
    if (dash == std::string::npos || dash + 1 >= id.size()) return;
    std::string digits = id.substr(dash + 1);
    if (digits.size() > 15 ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return;
    auto& counter = counters_[id.substr(0, dash)];
    counter = std::max<std::int64_t>(counter, std::stoll(digits));
}

} // namespace wallet
