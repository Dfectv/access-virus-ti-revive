// vtitests: runs every TEST of the linked files; exit 1 on a failure. Skipped tests (no Access files) are listed.
#include "check.h"
#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char **argv)
{
    int run = 0, skipped = 0;
    for (auto &c : check::cases()) {
        if (argc > 1 && !strstr(c.name, argv[1])) // vtitests <filter>
            continue;
        int before = check::failures();
        // the name first and flushed: a crash then shows which test ran (CI logs lose buffered output)
        std::printf("%-44s ", c.name);
        std::fflush(stdout);
        try {
            c.fn();
            run++;
            std::printf("%s\n", check::failures() == before ? "ok" : "FAILED");
        } catch (const check::Skip &s) {
            skipped++;
            std::printf("skipped (%s)\n", s.why.c_str());
        } catch (const std::exception &e) { // e.g. std::filesystem on Windows: a failure, not an abort
            run++;
            check::failures()++;
            std::printf("FAILED (exception: %s)\n", e.what());
        }
        std::fflush(stdout);
    }
    std::printf("\n%d tests, %d skipped, %d failed checks\n", run, skipped, check::failures());
    return check::failures() ? 1 : 0;
}
