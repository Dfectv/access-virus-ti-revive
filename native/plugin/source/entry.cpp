// entry.cpp - VST3 factory of the native Virus TI plug-in.
#include "public.sdk/source/main/pluginfactory.h"
#include "virusplugin.h"

#ifndef VTI_NATIVE_VERSION
#define VTI_NATIVE_VERSION "0.0.0" // set by CMake (project version)
#endif
#ifdef VTI_NATIVE_DROPIN
#define VTI_NATIVE_NAME "Virus TI"
#else
#define VTI_NATIVE_NAME "TI Revive Virus Control"
#endif

BEGIN_FACTORY_DEF("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kVirusTIClassId), PClassInfo::kManyInstances, kVstAudioEffectClass, VTI_NATIVE_NAME,
           0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION, kVstVersionString, vti::VirusPlugin::create)
#ifndef VTI_NATIVE_DROPIN // (the drop-in build has the original's id already)
// the same plug-in under the original Access plug-in's id: projects made with the original open with it in hosts that
// ignore moduleinfo compatibility (Ableton Live 11, E221)
DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kOriginalClassId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           VTI_NATIVE_NAME " (original projects)", 0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION,
           kVstVersionString, vti::VirusPlugin::create)
#endif

END_FACTORY
