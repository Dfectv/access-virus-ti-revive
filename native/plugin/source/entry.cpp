// Build smoke test for the toolchain (VST3 SDK + VSTGUI on this platform); replaced by the real plug-in in R3.
#include "public.sdk/source/main/pluginfactory.h"
#include "vstgui/vstgui.h"

BEGIN_FACTORY("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "", Steinberg::PFactoryInfo::kUnicode)
END_FACTORY
