// presencewatch.h - what happened to the Virus since the worker last looked: gone, back, or gone and back in between
// (E212: after a Linux wake the broker lost the Virus and reopened it 0.27 s later; the worker only polled the present
// flag, never saw it absent, kept its old audio stream and stayed silent). The broker client counts arrivals
// (Broker::device_arrivals), so a dip shorter than the poll still reads as a reconnect. Header only: the plug-in and
// the tests use it.
#pragma once
#include <cstdint>

namespace vti {

enum class Presence { same, gone, back, bounced };

class PresenceWatch {
public:
    // `present` = the broker's flag, `arrivals` = its count of arrivals, both read just now
    Presence update(bool present, uint32_t arrivals)
    {
        const int now = present ? 1 : 0;
        Presence r = Presence::same;
        if (now != seen_)
            r = present ? Presence::back : Presence::gone;
        else if (present && arrivals != arrivals_)
            r = Presence::bounced;
        seen_ = now;
        if (present) // while absent the count may already include the coming arrival: take it when it is there
            arrivals_ = arrivals;
        return r;
    }

private:
    int seen_ = -1; // -1 = unknown
    uint32_t arrivals_ = 0;
};

} // namespace vti
