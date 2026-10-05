/**
 * @file minitest.hpp
 * @brief Dependency-free test runner (one header, ~60 lines) so host tests build offline.
 *
 * Usage:
 *   TEST(name) { EXPECT_EQ(a, b); EXPECT_TRUE(x); }
 *   int main() { return minitest::run(); }        // or link minitest_main.hpp
 *
 * Swap for doctest/gtest later by replacing this header; the TEST/EXPECT_* vocabulary matches.
 */
#pragma once
#include <cstdio>
#include <cstring>

namespace minitest {

struct Case { const char* name; void (*fn)(); Case* next; };
inline Case*& registry() { static Case* head = nullptr; return head; }
inline int& failures() { static int n = 0; return n; }
inline const char*& current() { static const char* c = ""; return c; }

struct Registrar {
    Registrar(Case* c) { c->next = registry(); registry() = c; }
};

inline void fail(const char* file, int line, const char* what) {
    std::printf("  FAIL %s:%d  [%s]  %s\n", file, line, current(), what);
    ++failures();
}

inline int run() {
    int total = 0, failedCases = 0;
    // registry is LIFO; run in file order by walking to a vector-free reversed list
    Case* rev = nullptr;
    for (Case* c = registry(); c; ) { Case* n = c->next; c->next = rev; rev = c; c = n; }
    for (Case* c = rev; c; c = c->next) {
        ++total;
        int before = failures();
        current() = c->name;
        c->fn();
        if (failures() != before) ++failedCases;
    }
    std::printf("%d test(s), %d failed, %d assertion failure(s)\n", total, failedCases, failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)

#define TEST(name)                                                              \
    static void MT_CAT(mt_fn_, name)();                                         \
    static minitest::Case MT_CAT(mt_case_, name){#name, &MT_CAT(mt_fn_, name), nullptr}; \
    static minitest::Registrar MT_CAT(mt_reg_, name)(&MT_CAT(mt_case_, name)); \
    static void MT_CAT(mt_fn_, name)()

#define EXPECT_TRUE(x)  do { if (!(x)) minitest::fail(__FILE__, __LINE__, "expected true: " #x); } while (0)
#define EXPECT_FALSE(x) do { if (x)    minitest::fail(__FILE__, __LINE__, "expected false: " #x); } while (0)
#define EXPECT_EQ(a, b) do { if (!((a) == (b))) minitest::fail(__FILE__, __LINE__, "expected equal: " #a " == " #b); } while (0)
#define EXPECT_NE(a, b) do { if ((a) == (b))    minitest::fail(__FILE__, __LINE__, "expected not equal: " #a " != " #b); } while (0)
#define EXPECT_STREQ(a, b) do { if (std::strcmp((a), (b)) != 0) minitest::fail(__FILE__, __LINE__, "expected string equal: " #a " == " #b); } while (0)

#ifndef MINITEST_NO_MAIN
int main() { return minitest::run(); }
#endif
