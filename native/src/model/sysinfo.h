// sysinfo.h - the texts of the PATCH UTILITY page's INFO box ("Internal System Info/... Version String"). The original
// shows its built-in version, the Access driver versions and the Virus' OS (E189 comparison); TI Revive shows its
// plug-in version, the broker's version in the four driver rows (the broker replaces those drivers) and the Virus' OS
// once read. Unknown values show "-".
#pragma once
#include <string>

namespace vti {

struct SystemInfo {
    std::string plugin; // this plug-in's version ("0.8.21")
    std::string broker; // the broker's version, empty if it did not tell (older broker, not connected)
    std::string tios;   // the Virus' OS version, empty until read
};

// the text for one of the INFO box's parameters, or "" for any other parameter
std::string system_info_text(const std::string &param, const SystemInfo &info);

} // namespace vti
