// nerdmode.h - the Control Center's "Nerd mode" page: what TI Revive is doing, without a terminal.
// Part 1: the plug-in's and the broker's logs live, the plug-in log switch, logs for a bug report. Nothing here
// sends anything to the Virus. A page of the main window, not a window of its own: on Linux, VSTGUI's GTK backend
// left a second window blank (Xvfb, 2026-10-15).
#pragma once
#include "vstgui/lib/cviewcontainer.h"
#include <functional>
#include <string>

namespace tirevive {

constexpr VSTGUI::CCoord kNerdWidth = 860, kNerdHeight = 620;

// The page (kNerdWidth x kNerdHeight at 0,0), hidden. `back` is called by its "Back" button.
VSTGUI::CViewContainer *make_nerd_page(const std::string &version, std::function<void()> back);
// Shows / hides it; it reads the logs only while shown.
void set_nerd_page_active(VSTGUI::CViewContainer *page, bool on);

} // namespace tirevive
