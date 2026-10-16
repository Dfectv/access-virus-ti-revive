// The Linux broker's give-back reset guard (E136): resets of all brokers of a user are at least `gap` seconds apart,
// remembered in a time stamp file. Hardware-free: the decision and the stamp only.
#include "check.h"
extern "C" {
#include "resetguard.h"
}
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static std::string stamp_path(const char *name)
{
    std::string p = "/tmp/vtitests-resetguard-" + std::to_string(getpid()) + "-" + name;
    unlink(p.c_str());
    return p;
}

TEST(resetguard_spacing)
{
    std::string p = stamp_path("spacing");
    CHECK(vti_reset_guard(p.c_str(), 1000, 5) == 1); // first reset: allowed, stamp written
    struct stat st;
    CHECK(stat(p.c_str(), &st) == 0 && st.st_mtime == 1000);
    CHECK(vti_reset_guard(p.c_str(), 1002, 5) == 0); // 2 s later: skipped, stamp unchanged
    CHECK(stat(p.c_str(), &st) == 0 && st.st_mtime == 1000);
    CHECK(vti_reset_guard(p.c_str(), 1004, 5) == 0); // still inside the gap
    CHECK(vti_reset_guard(p.c_str(), 1005, 5) == 1); // the gap is over
    CHECK(stat(p.c_str(), &st) == 0 && st.st_mtime == 1005);
    unlink(p.c_str());
}

TEST(resetguard_fails_open)
{
    // a clock that went back must not block resets for good; no usable stamp file must not block them either
    std::string p = stamp_path("clock");
    CHECK(vti_reset_guard(p.c_str(), 5000, 5) == 1);
    CHECK(vti_reset_guard(p.c_str(), 100, 5) == 1);
    unlink(p.c_str());
    CHECK(vti_reset_guard("/nonexistent-dir/stamp", 1000, 5) == 1);
    CHECK(vti_reset_guard("/nonexistent-dir/stamp", 1001, 5) == 1);
}

TEST(resetguard_ignores_symlinks)
{
    // /tmp is shared: a symlink planted at the stamp path is not followed (the target keeps its time)
    std::string target = stamp_path("target"), link = stamp_path("link");
    FILE *f = fopen(target.c_str(), "w");
    CHECK(f != nullptr);
    if (f)
        fclose(f);
    CHECK(symlink(target.c_str(), link.c_str()) == 0);
    struct stat before, after;
    stat(target.c_str(), &before);
    vti_reset_guard(link.c_str(), 123, 5);
    stat(target.c_str(), &after);
    CHECK(after.st_mtime == before.st_mtime);
    unlink(link.c_str());
    unlink(target.c_str());
}
