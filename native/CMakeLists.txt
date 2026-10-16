# virus-ti-native - native rewrite of the Access Virus TI plug-in (docs/plan.md)
cmake_minimum_required(VERSION 3.25)
project(virus-ti-native VERSION 0.8.12 LANGUAGES C CXX) # = the TI Revive release it ships with
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows: static C/C++ runtime, so the plug-in and the ASIO driver run without the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "" FORCE)

# VST3 SDK (MIT) + VSTGUI (BSD), pinned submodule external/vst3sdk (v3.8.1_build_84)
set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(vst3sdk_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk)
add_subdirectory(external/vst3sdk ${CMAKE_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# pugixml (MIT) for the Access XML files
include(FetchContent)
FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml.git GIT_TAG v1.15 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(pugixml)

# core library: Access files + device model (no Access material in this repo; loaded at runtime)
add_library(vticore STATIC src/access/accessfiles.cpp src/model/remotedevice.cpp src/model/state.cpp src/device/broker.cpp src/device/memsync.cpp src/device/audioin.cpp src/device/sidechain.cpp src/editor/editormodel.cpp src/editor/editorrender.cpp src/access/firmware.cpp src/access/library.cpp src/cc/controlcenter.cpp src/cc/diag.cpp src/cc/decode.cpp src/cc/ccskin.cpp src/model/panelfollow.cpp src/editor/upscale.cpp)
# TI Revive's broker headers (vtiproto.h, vtiaudioshm.h) from the repository's src/broker
target_include_directories(vticore PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../src/broker)
find_package(Threads REQUIRED)
target_link_libraries(vticore PUBLIC Threads::Threads $<$<PLATFORM_ID:Linux>:rt>)
if(APPLE)
    target_link_libraries(vticore PUBLIC "-framework CoreAudio" "-framework CoreFoundation")
endif()
target_link_libraries(vticore PUBLIC pugixml::pugixml)
set_target_properties(vticore PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(vtimodel tools/vtimodel.cpp)
target_link_libraries(vtimodel PRIVATE vticore)
add_executable(virusctl tools/virusctl.cpp)
target_link_libraries(virusctl PRIVATE vticore)
add_executable(virusmon tools/virusmon.cpp) # read-only traffic monitor (broker observer)
target_link_libraries(virusmon PRIVATE vticore)
add_executable(virussync tools/virussync.cpp)
target_link_libraries(virussync PRIVATE vticore)
add_executable(tirevive-cc tools/tirevive-cc.cpp)
target_link_libraries(tirevive-cc PRIVATE vticore)
target_compile_definitions(tirevive-cc PRIVATE TIREVIVE_VERSION="${PROJECT_VERSION}")
# Control Center app (ccapp/, docs/controlcenter.md): a window around src/cc, VSTGUI's standalone library
vstgui_add_executable(TIReviveControlCenter "ccapp/ccapp.cpp;ccapp/nerdmode.cpp;ccapp/skinpage.cpp;ccapp/skinview.cpp")
target_link_libraries(TIReviveControlCenter PRIVATE vticore)
target_include_directories(TIReviveControlCenter PRIVATE external/vst3sdk/vstgui4)
target_compile_definitions(TIReviveControlCenter PRIVATE TIREVIVE_VERSION="${PROJECT_VERSION}")
# one place for every generator (the generator expression keeps MSVC/Xcode from adding a config folder):
# <build>/ccapp/TIReviveControlCenter[.exe], on macOS <build>/ccapp/TI Revive Control Center.app
set_target_properties(TIReviveControlCenter PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ccapp/$<0:>")
if(APPLE)
    set(TIREVIVE_APP_VERSION ${PROJECT_VERSION})
    configure_file(ccapp/Info.plist ${CMAKE_CURRENT_BINARY_DIR}/ccapp-Info.plist @ONLY)
    set_target_properties(TIReviveControlCenter PROPERTIES OUTPUT_NAME "TI Revive Control Center"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/ccapp-Info.plist)
endif()

add_subdirectory(plugin)

# macOS: the Audio Unit (AUv2) for Logic / GarageBand - Steinberg's AU wrapper around the VST3 plug-in, with the
# codes of the classic TI Revive Audio Unit (au/tirevive_au.mm). Apple's AudioUnitSDK (Apache 2.0) is built from
# source here, so no Xcode project is needed.
if(APPLE)
    option(VTI_NATIVE_AU "build the Audio Unit" ON)
endif()
if(APPLE AND VTI_NATIVE_AU)
    include(FetchContent)
    FetchContent_Declare(audiounitsdk GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
                         GIT_TAG AudioUnitSDK-1.4.0 GIT_SHALLOW TRUE)
    FetchContent_GetProperties(audiounitsdk)
    if(NOT audiounitsdk_POPULATED)
        FetchContent_Populate(audiounitsdk)
    endif()
    file(GLOB VTI_AUSDK_SOURCES ${audiounitsdk_SOURCE_DIR}/src/AudioUnitSDK/*.cpp)
    add_library(vti_ausdk STATIC ${VTI_AUSDK_SOURCES})
    target_include_directories(vti_ausdk PUBLIC ${audiounitsdk_SOURCE_DIR}/include)
    set_target_properties(vti_ausdk PROPERTIES CXX_STANDARD 23) # AudioUnitSDK 1.4: C++23 (std::expected, std::span)

    if(NOT TARGET sdk_hosting)
        smtg_create_public_sdk_hosting_target()
    endif()
    set(VTI_AU_SDK ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper)
    add_library(VirusTINative_AU MODULE au/tirevive_au.mm ${VTI_AU_SDK}/aucocoaview.mm ${VTI_AU_SDK}/NSDataIBStream.mm)
    target_compile_definitions(VirusTINative_AU PRIVATE SMTG_AUWRAPPER_USES_AUSDK CA_USE_AUDIO_PLUGIN_ONLY=0
                               SMTG_AUCocoaUIBase_CLASS_NAME=TIReviveVirusControlAUCocoaView)
    target_compile_options(VirusTINative_AU PRIVATE -Wno-deprecated-declarations)
    target_include_directories(VirusTINative_AU PRIVATE ${vst3sdk_SOURCE_DIR})
    target_link_libraries(VirusTINative_AU PRIVATE vti_ausdk sdk_hosting "-framework AudioUnit" "-framework CoreMIDI"
                          "-framework AudioToolbox" "-framework CoreFoundation" "-framework Cocoa" "-framework CoreAudio")
    # version as the classic Audio Unit encoded it: major << 16 | minor << 8 | patch
    math(EXPR VTI_AU_VERSION "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
    configure_file(au/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/au-Info.plist @ONLY)
    set_target_properties(VirusTINative_AU PROPERTIES CXX_STANDARD 23
        BUNDLE TRUE BUNDLE_EXTENSION component OUTPUT_NAME "TI Revive Virus Control"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/au-Info.plist
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/AU)
    add_dependencies(VirusTINative_AU VirusTINative)
    # the component carries the VST3 plug-in it wraps (a copy, not a link: the bundle is installed on its own)
    get_target_property(VTI_VST3_PACKAGE VirusTINative SMTG_PLUGIN_PACKAGE_PATH)
    add_custom_command(TARGET VirusTINative_AU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:VirusTINative_AU>/Contents/Resources/plugin.vst3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:VirusTINative_AU>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VTI_VST3_PACKAGE}"
                "$<TARGET_BUNDLE_DIR:VirusTINative_AU>/Contents/Resources/plugin.vst3")
endif()

# Windows: the Virus as an ASIO sound card (asio/asiodriver.cpp), register with regsvr32
if(WIN32)
    add_library(VirusTINative_ASIO SHARED asio/asiodriver.cpp asio/asiodriver.def)
    target_link_libraries(VirusTINative_ASIO PRIVATE vticore avrt ole32 advapi32 user32)
    add_executable(asiohost tools/asiohost.c) # test host: loads the driver, runs N s, reports switches and peaks
    target_link_libraries(asiohost PRIVATE ole32)
endif()

# unit tests (no Virus needed; tests on the Access files skip without them): ctest --test-dir build
enable_testing()
add_executable(vtitests tests/main.cpp tests/test_usbmidi.cpp tests/test_state.cpp tests/test_model.cpp
               tests/test_access.cpp tests/test_automation.cpp tests/test_upgrade.cpp tests/test_resampler.cpp tests/test_sidechain.cpp
               tests/test_library.cpp tests/test_decode.cpp tests/test_ccskin.cpp tests/test_memsync.cpp
               tests/test_cc.cpp tests/test_broker.cpp tests/test_audioin.cpp tests/test_panelfollow.cpp tests/test_editor.cpp
               tests/test_accessfiles.cpp tests/test_firmware.cpp tests/test_diag.cpp
               tests/test_protocol.cpp tests/test_upscale.cpp)
target_link_libraries(vtitests PRIVATE vticore)
add_test(NAME vtitests COMMAND vtitests)

# test host for the native plug-in (R3 checks)
if(NOT TARGET sdk_hosting)
    smtg_create_public_sdk_hosting_target()
endif()
set(_h ${vst3sdk_SOURCE_DIR}/public.sdk/source)
add_executable(nativehost tools/nativehost.cpp ${_h}/common/memorystream.cpp ${_h}/vst/hosting/module.cpp ${_h}/vst/hosting/eventlist.cpp
    $<$<PLATFORM_ID:Linux>:${_h}/vst/hosting/module_linux.cpp> $<$<PLATFORM_ID:Darwin>:${_h}/vst/hosting/module_mac.mm>
    $<$<PLATFORM_ID:Windows>:${_h}/vst/hosting/module_win32.cpp>)
target_link_libraries(nativehost PRIVATE sdk_hosting $<$<PLATFORM_ID:Linux>:X11> $<$<PLATFORM_ID:Linux>:Xtst>)
if(APPLE)
    set_source_files_properties(${_h}/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
    target_link_libraries(nativehost PRIVATE "-framework Cocoa")
endif()

# offscreen render of the original editor layout (R4 checks)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(CAIRO IMPORTED_TARGET cairo)
endif()
if(CAIRO_FOUND)
    add_executable(vtirender tools/vtirender.cpp)
    target_link_libraries(vtirender PRIVATE vticore PkgConfig::CAIRO)
endif()
