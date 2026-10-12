// tirbridge_au.h - shared between the Audio Unit (tirbridge_au.cpp) and its Cocoa view factory (tirbridge_au_view.mm).
#pragma once

// private property: the instance's BridgeCore* (the view factory gets only the AudioUnit handle)
enum { kTIRPropertyCore = 0x54495276 /* 'TIRv' */ };

// Objective-C class of the view factory; versioned, because a host may have two TI Revive builds loaded and
// Objective-C class names are process-wide
#define TIR_AU_VIEW_CLASS "TIReviveVTIViewFactory_0_6"
