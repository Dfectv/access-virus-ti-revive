// tirbridge_view.h - the bridge's editor: an IPlugView whose NSView shows the helper's rendering of the original
// editor and forwards mouse/keyboard to it (findings/design-macos-bridge.md, "Editor (mirror)").
#pragma once
#include "pluginterfaces/gui/iplugview.h"
#include "tirbridge.h"

struct EditorLink { // implemented by the bridge instance (control channel to the helper)
    virtual bool editor_open(double x, double y, double scale, tirb_editor_reply &r) = 0;
    virtual void editor_close() = 0;
    virtual void editor_move(double x, double y) = 0;
    virtual void editor_input(const tirb_input &in) = 0;
    virtual void editor_view_gone() = 0; // the view was destroyed
    virtual void editor_menu_result(const tirb_menu_result &r) = 0;
    virtual ~EditorLink() = default;
};

// Creates the view; the returned object also receives TIRB_N_EDITOR_RESIZE through bridge_view_resized().
Steinberg::IPlugView *bridge_create_view(EditorLink *link);
void bridge_view_resized(Steinberg::IPlugView *view, const tirb_editor_reply &r); // any thread
void bridge_view_key_focus(Steinberg::IPlugView *view, bool on);                  // any thread
void bridge_view_menu(Steinberg::IPlugView *view, const void *msg, size_t len);   // any thread (TIRB_N_MENU)
