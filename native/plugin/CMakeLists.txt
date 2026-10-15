# class ids (plugin/source/virusplugin.cpp): this platform's id replaces the others in DAW projects (VST3
# moduleinfo "Compatibility", hosts with VST 3.7.5+ support): the original Access plug-in, the TI Revive wrapper
# (Windows/Linux), the TI Revive bridge (macOS) and the test builds' own id
set(VTI_CID_ORIGINAL B49F90D9735F4305AC5397BE8958CD31)
set(VTI_CID_WRAPPER B49F90D9735F4305AC5397BE8958CD6B)
set(VTI_CID_BRIDGE 7A1E5C3B4D2F4E619B0C8A172F64D5E9)
set(VTI_CID_TESTS B49F90D9735F4305AC5397BE8958CD94)
if(APPLE)
    set(VTI_CID_NEW ${VTI_CID_BRIDGE})
    set(VTI_CID_OLD ${VTI_CID_ORIGINAL} ${VTI_CID_WRAPPER} ${VTI_CID_TESTS})
else()
    set(VTI_CID_NEW ${VTI_CID_WRAPPER})
    set(VTI_CID_OLD ${VTI_CID_ORIGINAL} ${VTI_CID_BRIDGE} ${VTI_CID_TESTS})
endif()
list(JOIN VTI_CID_OLD "\", \"" VTI_CID_OLD_JSON)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/compatibility.json "[{\"New\": \"${VTI_CID_NEW}\", \"Old\": [\"${VTI_CID_OLD_JSON}\"]}]\n")
file(RELATIVE_PATH VTI_COMPAT_JSON ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/compatibility.json) # the SDK wants it relative

# bundle "TI Revive Virus Control.vst3" (the product name; the build target stays VirusTINative)
smtg_add_vst3plugin(VirusTINative PACKAGE_NAME "TI Revive Virus Control"
    MODULEINFO_COMPATIBILITY ${VTI_COMPAT_JSON} SOURCES_LIST
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
