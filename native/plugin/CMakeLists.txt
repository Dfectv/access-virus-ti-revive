# bundle "TI Revive Virus Control.vst3" (the product name; the build target stays VirusTINative)
smtg_add_vst3plugin(VirusTINative PACKAGE_NAME "TI Revive Virus Control" SOURCES_LIST
    source/entry.cpp
    source/virusplugin.cpp
    source/viruseditor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstguieditor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_link_libraries(VirusTINative PRIVATE sdk vstgui_support vticore)
target_compile_definitions(VirusTINative PRIVATE VTI_NATIVE_VERSION="${PROJECT_VERSION}")
option(VTI_NATIVE_DROPIN "use the original's class id (loads projects saved with the Access plug-in)" OFF)
if(VTI_NATIVE_DROPIN)
    target_compile_definitions(VirusTINative PRIVATE VTI_NATIVE_DROPIN)
endif()
if(MSVC) # crash analysis: debug info + a linker map (function RVAs) next to the build, uploaded by CI
    target_compile_options(VirusTINative PRIVATE /Zi)
    target_link_options(VirusTINative PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP:$<TARGET_FILE_DIR:VirusTINative>/VirusTINative.map)
endif()
