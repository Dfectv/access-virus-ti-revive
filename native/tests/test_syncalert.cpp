// The original's sync error page ("A sync error occurred. Press Stop and Play to clear.", Info Panel/Page = 1), E205:
// every Sync Information Event from the Virus adds 20 to a level that falls by 1 per idle tick; above 60 while the
// host plays (and Live Mode is off) the page shows; the next Play clears it and the counters since Play.
#include "check.h"
#include "model/syncalert.h"

using namespace vti;

TEST(syncalert_single_report_shows_nothing)
{
    // one late event after a short stall (E128): no page
    SyncAlert a;
    a.play(true);
    a.event();
    CHECK(a.page() == 0);
    CHECK(a.total() == 1 && a.since_play() == 1);
}

TEST(syncalert_burst_while_playing_shows_the_page)
{
    SyncAlert a;
    a.play(true);
    for (int i = 0; i < 3; i++)
        a.event();
    CHECK(a.page() == 0); // level 60: not above 60 yet
    a.event();
    CHECK(a.page() == 1); // level 80
}

TEST(syncalert_level_decays_per_tick)
{
    // reports spread out (one every 25 ticks) never pile up
    SyncAlert a;
    a.play(true);
    for (int i = 0; i < 10; i++) {
        a.event();
        for (int t = 0; t < 25; t++)
            a.tick();
    }
    CHECK(a.page() == 0);
    CHECK(a.level() == 0);
}

TEST(syncalert_not_while_stopped_or_in_live_mode)
{
    // the Virus reports while stopped too (E072, ~9/s); the original shows the page only while playing
    SyncAlert a;
    for (int i = 0; i < 10; i++)
        a.event();
    CHECK(a.page() == 0);
    SyncAlert b;
    b.play(true);
    b.live_mode(true);
    for (int i = 0; i < 10; i++)
        b.event();
    CHECK(b.page() == 0);
}

TEST(syncalert_next_play_clears)
{
    SyncAlert a;
    a.play(true);
    for (int i = 0; i < 5; i++)
        a.event();
    CHECK(a.page() == 1);
    a.play(false); // Stop alone keeps the page
    CHECK(a.page() == 1);
    a.play(true); // Play: cleared, counters since Play and the level reset, the total stays
    CHECK(a.page() == 0 && a.level() == 0 && a.since_play() == 0 && a.total() == 5);
    a.play(true); // still playing: not a new start
    a.event();
    CHECK(a.since_play() == 1);
}

TEST(syncalert_page_changes_are_reported_once)
{
    SyncAlert a;
    a.play(true);
    CHECK(!a.take_change());
    for (int i = 0; i < 4; i++)
        a.event();
    CHECK(a.take_change()); // 0 -> 1
    CHECK(!a.take_change());
    a.event();
    CHECK(!a.take_change()); // stays 1
    a.play(false);
    a.play(true);
    CHECK(a.take_change()); // 1 -> 0
}
