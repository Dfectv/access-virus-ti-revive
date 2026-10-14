# virus-ti-native - native rewrite of the Access Virus TI plug-in (docs/plan.md)
cmake_minimum_required(VERSION 3.25)
project(virus-ti-native VERSION 0.0.1 LANGUAGES C CXX)
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
add_library(vticore STATIC src/access/accessfiles.cpp src/model/remotedevice.cpp src/model/state.cpp src/device/broker.cpp src/device/memsync.cpp src/device/audioin.cpp src/editor/editormodel.cpp src/editor/editorrender.cpp src/access/firmware.cpp src/access/library.cpp)
# TI Revive's broker headers (vtiproto.h, vtiaudioshm.h): from the parent tree when this code lives inside TI Revive
# (native/ next to src/), else from the submodule external/ti-revive
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../src/broker/vtiproto.h)
    set(TIREVIVE_BROKER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/broker)
else()
    set(TIREVIVE_BROKER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ti-revive/src/broker)
endif()
target_include_directories(vticore PUBLIC src ${TIREVIVE_BROKER_DIR})
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
add_executable(virussync tools/virussync.cpp)
target_link_libraries(virussync PRIVATE vticore)

add_subdirectory(plugin)

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
               tests/test_access.cpp tests/test_automation.cpp tests/test_upgrade.cpp tests/test_resampler.cpp)
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
