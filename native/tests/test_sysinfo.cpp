// The PATCH UTILITY page's INFO box ("Internal System Info/... Version String"): the original shows its own version
// (built in, FUN_180078060), the Access driver versions and the Virus' OS read from its flash (FUN_1800ac7c0). TI Revive
// shows its plug-in version, the broker's version in the four driver rows (the broker replaces the Access drivers) and
// the OS version once it is known; "-" while something is unknown.
#include "check.h"
#include "model/sysinfo.h"
using namespace vti;

TEST(sysinfo_version_texts)
{
    SystemInfo s;
    s.plugin = "0.8.21";
    s.broker = "0.8.21";
    s.tios = "5.1.7.00";
    CHECK(system_info_text("Internal System Info/Virus TI Control Version String", s) == "TI Revive 0.8.21");
    for (const char *p : {"Internal System Info/Audio Driver Version String", "Internal System Info/Audio Library Version String",
                          "Internal System Info/Audio Host Interface Version String",
                          "Internal System Info/MIDI Driver Version String"})
        CHECK(system_info_text(p, s) == "TI Revive 0.8.21");
    CHECK(system_info_text("Internal System Info/TIOS Version String", s) == "5.1.7.00");
    CHECK(system_info_text("Osc/Shape", s).empty()); // not ours: the caller falls back to the value text
}

TEST(sysinfo_unknown_shows_dash)
{
    SystemInfo s;
    s.plugin = "0.8.21"; // a broker without the version message, no OS read yet
    CHECK(system_info_text("Internal System Info/Audio Driver Version String", s) == "-");
    CHECK(system_info_text("Internal System Info/TIOS Version String", s) == "-");
    CHECK(system_info_text("Internal System Info/Virus TI Control Version String", s) == "TI Revive 0.8.21");
}
