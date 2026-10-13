// entry.cpp - VST3 factory of the native Virus TI plug-in.
#include "public.sdk/source/main/pluginfactory.h"
#include "virusplugin.h"

#define VTI_NATIVE_VERSION "0.0.1"
#ifdef VTI_NATIVE_DROPIN
#define VTI_NATIVE_NAME "Virus TI"
#else
#define VTI_NATIVE_NAME "Virus TI (native)"
#endif

BEGIN_FACTORY_DEF("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kVirusTIClassId), PClassInfo::kManyInstances, kVstAudioEffectClass, VTI_NATIVE_NAME,
           0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION, kVstVersionString, vti::VirusPlugin::create)

END_FACTORY
