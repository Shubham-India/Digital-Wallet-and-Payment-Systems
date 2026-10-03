#include <chrono>
#include <cstring>

#include "TestFramework.h"
#include "domain/Result.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    for (const auto& t : testfw::registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = testfw::counters().failedChecks;
        try {
            t.fn();
        } catch (const wallet::WalletException& e) {
            ++testfw::counters().failedChecks;
            std::cout << "  FAIL unexpected WalletException: " << e.what() << '\n';
        } catch (const std::exception& e) {
            ++testfw::counters().failedChecks;
            std::cout << "  FAIL unexpected exception: " << e.what() << '\n';
        }
        ++ran;
        bool failed = testfw::counters().failedChecks != before;
        if (failed) ++failedTests;
        std::cout << (failed ? "[FAIL] " : "[ ok ] ") << t.name << '\n';
    }
    std::cout << "\n" << ran << " tests run, " << (ran - failedTests) << " passed, " << failedTests << " failed ("
              << testfw::counters().checks << " checks)\n";
    return failedTests == 0 ? 0 : 1;
}
