// A tiny, dependency-free test framework.
//
// Usage:
//   TEST_CASE("name") { ... CHECK(expr); REQUIRE(expr); ... }
//   int main() { return fum_test::run_all(); }
//
// CHECK records a failure and continues; REQUIRE records a failure and aborts
// the current test case (via exception).  Both print file:line on failure.

#ifndef FUM_TEST_FRAMEWORK_HPP
#define FUM_TEST_FRAMEWORK_HPP

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace fum_test {

struct RequireFailure : std::exception {};

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline int& check_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

inline void report_failure(const char* file, int line, const char* expr,
                           const char* extra) {
    ++failure_count();
    std::printf("  FAILED: %s:%d: %s%s\n", file, line, expr,
                extra ? extra : "");
}

inline int run_all() {
    int failed_cases = 0;
    for (const TestCase& test : registry()) {
        const int failures_before = failure_count();
        try {
            test.body();
        } catch (const RequireFailure&) {
            // already reported
        } catch (const std::exception& error) {
            report_failure(__FILE__, __LINE__, "unexpected exception: ",
                           error.what());
        } catch (...) {
            report_failure(__FILE__, __LINE__, "unexpected non-std exception",
                           "");
        }
        const bool passed = failure_count() == failures_before;
        if (!passed) ++failed_cases;
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", test.name.c_str());
    }
    std::printf("\n%d/%zu test cases passed, %d checks, %d failures\n",
                static_cast<int>(registry().size()) - failed_cases,
                registry().size(), check_count(), failure_count());
    return failure_count() == 0 ? 0 : 1;
}

}  // namespace fum_test

// Token pasting helpers for unique identifiers.
#define FUM_TEST_CAT_INNER(a, b) a##b
#define FUM_TEST_CAT(a, b) FUM_TEST_CAT_INNER(a, b)

#define TEST_CASE(name)                                                     \
    static void FUM_TEST_CAT(fum_test_body_, __LINE__)();                   \
    static ::fum_test::Registrar FUM_TEST_CAT(fum_test_reg_, __LINE__)(     \
        name, &FUM_TEST_CAT(fum_test_body_, __LINE__));                     \
    static void FUM_TEST_CAT(fum_test_body_, __LINE__)()

#define CHECK(expr)                                                         \
    do {                                                                    \
        ++::fum_test::check_count();                                        \
        if (!(expr)) {                                                      \
            ::fum_test::report_failure(__FILE__, __LINE__, #expr, "");      \
        }                                                                   \
    } while (false)

#define REQUIRE(expr)                                                       \
    do {                                                                    \
        ++::fum_test::check_count();                                        \
        if (!(expr)) {                                                      \
            ::fum_test::report_failure(__FILE__, __LINE__, #expr,           \
                                       " (REQUIRE)");                       \
            throw ::fum_test::RequireFailure{};                            \
        }                                                                   \
    } while (false)

#define CHECK_THROWS_AS(expr, exception_type)                               \
    do {                                                                    \
        ++::fum_test::check_count();                                        \
        bool fum_threw_expected = false;                                    \
        try {                                                               \
            (void)(expr);                                                   \
        } catch (const exception_type&) {                                   \
            fum_threw_expected = true;                                      \
        } catch (...) {                                                     \
        }                                                                   \
        if (!fum_threw_expected) {                                          \
            ::fum_test::report_failure(__FILE__, __LINE__,                  \
                                       #expr " did not throw " #exception_type, \
                                       "");                                 \
        }                                                                   \
    } while (false)

#endif  // FUM_TEST_FRAMEWORK_HPP
