#pragma once
// Tiny dependency-free test harness (no Catch2/GoogleTest needed on this toolchain).
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
// A failing CHECK records the failure and the test keeps running; an escaping exception fails the test.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Counters {
    int checks = 0;
    int failedChecks = 0;
};
inline Counters& counters() {
    static Counters c;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <class A, class B>
inline void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    ++counters().checks;
    if (!(a == b)) {
        ++counters().failedChecks;
        std::ostringstream o;
        o << "  FAIL " << file << ":" << line << "  CHECK_EQ(" << ea << ", " << eb << ")";
        std::cout << o.str() << '\n';
    }
}

inline void check(bool ok, const char* expr, const char* file, int line) {
    ++counters().checks;
    if (!ok) {
        ++counters().failedChecks;
        std::cout << "  FAIL " << file << ":" << line << "  CHECK(" << expr << ")\n";
    }
}

}  // namespace testfw

#define TEST(name)                                                  \
    static void name();                                             \
    static testfw::Registrar registrar_##name(#name, name);         \
    static void name()

#define CHECK(expr) testfw::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) testfw::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
