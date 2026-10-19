// The original's two start-up pages (startprogress.h): "Starting up" with a filling ring while the project goes to
// the Virus (VirusControl/RecallProgress, ProgressBar.png: 16 frames), and "Syncing memory" only while banks are
// really fetched (InitialSyncProgress). 0..126 shows a page, 127 hides it (editordevice.xml conditions).
#include "check.h"
#include "device/startprogress.h"

using namespace vti;

TEST(startprogress_recall_follows_the_queue)
{
    RecallMeter m;
    CHECK(m.update(0) == 0);    // nothing queued yet: the page shows, empty
    CHECK(m.update(200) == 0);  // the state arrives: 200 messages to go
    CHECK(m.update(100) == 60); // half sent
    CHECK(m.update(250) == 0);  // a new state replaces the old one: the total grows, the ring starts again
    CHECK(m.update(0) == RecallMeter::SENT);
    CHECK(RecallMeter::SENT < 127); // the page stays until the Virus answers the ready probe
}

TEST(startprogress_sync_page_only_when_fetching)
{
    SyncPage s;
    CHECK(s.progress(0, false) == 127);   // the cache check: no page
    CHECK(s.progress(300, false) == 127);
    CHECK(s.progress(301, true) == 301 * 126 / 512); // the first real fetch shows it
    CHECK(s.shown());
    CHECK(s.progress(302, false) == 302 * 126 / 512); // and it stays until the sync is done
    CHECK(s.progress(512, false) == 126);
}

TEST(startprogress_sync_page_at_once_when_a_bank_is_missing)
{
    // a bank without a cache file is read in full: the page follows "Starting up" directly, without showing the
    // editor for the moment the bank check takes (user, L4)
    SyncPage s;
    s.expect();
    CHECK(s.shown());
    CHECK(s.progress(0, false) == 0);
    CHECK(s.progress(256, false) == 63);
}
