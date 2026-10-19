// startprogress.h - the values behind the original's two start-up pages.
//
// Why: the original shows "Starting up" with a ring that fills (VirusControl/RecallProgress, ProgressBar.png, 16
// frames) while the project goes to the Virus, and "Syncing memory with Virus" (InitialSyncProgress) while it reads
// the RAM banks; each page shows while its value is 0..126 and hides at 127 (conditions in editordevice.xml).
// TI Revive showed the syncing page for every start, also when the bank check found nothing to fetch (user,
// 2026-10-19: "only show the syncing if it really happens").
#pragma once

namespace vti {

// RecallProgress from the queued start-up messages and state; the largest count seen is the total (a project's
// state replaces the default one after it), so the ring never runs backwards except when a bigger state arrives
struct RecallMeter {
    static constexpr int SENT = 120; // all sent; the last part fills when the Virus answers the ready probe
    int total = 0;
    int update(int left)
    {
        if (left < 0)
            left = 0;
        if (left > total)
            total = left;
        return total == 0 ? 0 : (total - left) * SENT / total;
    }
};

// InitialSyncProgress: hidden (127) during the bank check, shown from the first patch that is really fetched
struct SyncPage {
    bool on = false;
    bool shown() const { return on; }
    int progress(int done, bool fetched) // done = patches checked so far (of 512)
    {
        if (fetched)
            on = true;
        if (!on)
            return 127;
        return done >= 512 ? 126 : done * 126 / 512;
    }
};

} // namespace vti
