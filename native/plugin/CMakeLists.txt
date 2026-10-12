smtg_add_vst3plugin(VirusTINative
    source/entry.cpp
    source/virusplugin.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_link_libraries(VirusTINative PRIVATE sdk vstgui_support vticore)
option(VTI_NATIVE_DROPIN "use the original's class id (loads projects saved with the Access plug-in)" OFF)
if(VTI_NATIVE_DROPIN)
    target_compile_definitions(VirusTINative PRIVATE VTI_NATIVE_DROPIN)
endif()
