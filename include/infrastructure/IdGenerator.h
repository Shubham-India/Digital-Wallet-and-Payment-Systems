#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace wallet {

// Sequential, human-readable IDs ("TX-000042"). Counters are re-seeded from persisted
// IDs on startup via observe(), so IDs stay unique across runs.
class IdGenerator {
public:
    std::string next(const std::string& prefix, int width = 4);
    void observe(const std::string& existingId);
private:
    std::unordered_map<std::string, std::int64_t> counters_;
};

} // namespace wallet
