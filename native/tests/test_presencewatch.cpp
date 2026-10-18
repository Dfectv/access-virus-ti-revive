// The Virus gone and back between two polls of the worker (E212: Linux wake, the broker saw LIBUSB_ERROR_NO_DEVICE and
// reopened the Virus 0.27 s later; the plug-in never saw it absent, never restarted the audio and stayed silent).
// The broker client counts arrivals, so a dip the worker missed still reads as a reconnect.
#include "check.h"
#include "device/presencewatch.h"

using namespace vti;

TEST(presencewatch_first_connect_and_loss)
{
    PresenceWatch w;
    CHECK(w.update(false, 0) == Presence::gone); // unknown -> absent: reported once, like the old flag check
    CHECK(w.update(false, 0) == Presence::same);
    CHECK(w.update(true, 1) == Presence::back);
    CHECK(w.update(true, 1) == Presence::same);
    CHECK(w.update(false, 1) == Presence::gone);
    CHECK(w.update(true, 2) == Presence::back);
}

TEST(presencewatch_present_at_start)
{
    PresenceWatch w;
    CHECK(w.update(true, 1) == Presence::back);
    CHECK(w.update(true, 1) == Presence::same);
}

TEST(presencewatch_dip_between_polls_is_a_reconnect)
{
    PresenceWatch w;
    w.update(true, 1);
    CHECK(w.update(true, 2) == Presence::bounced); // went away and came back while the worker was not looking
    CHECK(w.update(true, 2) == Presence::same);
    CHECK(w.update(true, 4) == Presence::bounced); // two dips in one gap: still one reconnect
}

TEST(presencewatch_arrival_counted_while_absent)
{
    // the count may already include the arrival when the worker still reads the old absent flag: no double report
    PresenceWatch w;
    w.update(true, 1);
    CHECK(w.update(false, 2) == Presence::gone);
    CHECK(w.update(true, 2) == Presence::back);
    CHECK(w.update(true, 2) == Presence::same);
}
