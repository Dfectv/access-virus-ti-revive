// resetguard.h - E136: several give-back resets within a second (brokers ending one after another during tests) once
// hung the Virus until a power cycle. The resets of all brokers of a user are spaced with a time stamp file: a reset
// less than gap_s seconds after the previous one is skipped (snd-usb-audio then gets the Virus back with the next
// reset or a replug). Fails open: without a usable stamp, or after the clock went back, the reset happens.
// POSIX (Linux broker); the unit test calls it with its own clock values.
#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// 1 = reset now (the stamp now says `now`), 0 = skip it
static inline int vti_reset_guard(const char *path, time_t now, int gap_s)
{
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISREG(st.st_mode) && now >= st.st_mtime && now - st.st_mtime < gap_s)
        return 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW, 0600); // /tmp is shared: never follow a planted link
    if (fd >= 0) {
        struct timespec ts[2] = {{now, 0}, {now, 0}};
        futimens(fd, ts);
        close(fd);
    }
    return 1;
}
