// vtihelper_editor.h - the original plug-in's editor inside the helper, mirrored into the bridge
// (findings/design-macos-bridge.md, "Editor (mirror)"). All functions run on the helper's main thread.
#pragma once
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "tirbridge.h"

struct HelperEditor;
typedef void (*editor_notify_cb)(void *ctx, uint32_t type, const void *payload, uint32_t len); // TIRB_N_*

// Opens the original's editor in an invisible, click-through window whose top-left is at (x, y) in Cocoa screen
// coordinates, renders it at `scale` into a pixel shm named after shm_prefix. Returns nullptr if there is no editor.
HelperEditor *editor_open(Steinberg::Vst::IEditController *ctl, const char *shm_prefix, double x, double y,
                          double scale, tirb_editor_reply &reply, editor_notify_cb notify, void *ctx);
void editor_close(HelperEditor *e);
void editor_move(HelperEditor *e, double x, double y);
void editor_input(HelperEditor *e, const tirb_input &in);

// TIRB_MENU_RESULT from the bridge (any thread): ends the popup menu the original is waiting in
void editor_menu_result(HelperEditor *e, const tirb_menu_result &r);
