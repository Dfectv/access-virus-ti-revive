// vtitests: runs every TEST of the linked files; exit 1 on a failure. Skipped tests (no Access files) are listed.
#include "check.h"
#include <cstring>

int main(int argc, char **argv)
{
    int run = 0, skipped = 0;
    for (auto &c : check::cases()) {
        if (argc > 1 && !strstr(c.name, argv[1])) // vtitests <filter>
            continue;
        int before = check::failures();
        try {
            c.fn();
            run++;
            std::printf("%-44s %s\n", c.name, check::failures() == before ? "ok" : "FAILED");
        } catch (const check::Skip &s) {
            skipped++;
            std::printf("%-44s skipped (%s)\n", c.name, s.why.c_str());
        }
    }
    std::printf("\n%d tests, %d skipped, %d failed checks\n", run, skipped, check::failures());
    return check::failures() ? 1 : 0;
}
