// check.h - a minimal test harness (no dependency, runs wherever the plug-in builds): TEST(name) { CHECK(...); }
// SKIP("why") ends a test that needs something missing (the user's Access files: CI has none).
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace check {
struct Case {
    const char *name;
    void (*fn)();
};
inline std::vector<Case> &cases()
{
    static std::vector<Case> c;
    return c;
}
inline int &failures()
{
    static int f = 0;
    return f;
}
struct Reg {
    Reg(const char *n, void (*f)()) { cases().push_back({n, f}); }
};
struct Skip {
    std::string why;
};
} // namespace check

#define TEST(name)                                                                                                     \
    static void name();                                                                                                \
    static check::Reg reg_##name(#name, name);                                                                         \
    static void name()
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                              \
            check::failures()++;                                                                                       \
        }                                                                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        auto va_ = (a);                                                                                                \
        auto vb_ = (b);                                                                                                \
        if (!(va_ == vb_)) {                                                                                           \
            std::fprintf(stderr, "  %s:%d: CHECK_EQ failed: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,   \
                         (long long)va_, (long long)vb_);                                                              \
            check::failures()++;                                                                                       \
        }                                                                                                              \
    } while (0)
#define SKIP(why) throw check::Skip{why}
