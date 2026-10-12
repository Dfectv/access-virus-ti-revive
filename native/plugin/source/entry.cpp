// entry.cpp - VST3 factory of the native Virus TI plug-in.
#include "public.sdk/source/main/pluginfactory.h"
#include "virusplugin.h"

#define VTI_NATIVE_VERSION "0.0.1"

BEGIN_FACTORY_DEF("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kVirusTIClassId), PClassInfo::kManyInstances, kVstAudioEffectClass,
#ifdef VTI_NATIVE_DROPIN
           "Virus TI",
#else
           "Virus TI (native)",
#endif
           0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION, kVstVersionString, vti::VirusPlugin::create)

END_FACTORY
