#include "model/sysinfo.h"

namespace vti {

static std::string or_dash(const std::string &s, const char *prefix = "")
{
    return s.empty() ? "-" : prefix + s;
}

std::string system_info_text(const std::string &param, const SystemInfo &info)
{
    static const std::string base = "Internal System Info/";
    if (param.compare(0, base.size(), base) != 0)
        return {};
    const std::string name = param.substr(base.size());
    if (name == "Virus TI Control Version String")
        return or_dash(info.plugin, "TI Revive ");
    if (name == "Audio Driver Version String" || name == "Audio Library Version String" ||
        name == "Audio Host Interface Version String" || name == "MIDI Driver Version String")
        return or_dash(info.broker, "TI Revive ");
    if (name == "TIOS Version String")
        return or_dash(info.tios);
    return {};
}

} // namespace vti
