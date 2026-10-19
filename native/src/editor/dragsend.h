// dragsend.h - one parameter message per value during a mouse drag, not one per mouse move.
//
// Why: the editor called ui_set() on every mouse move of a knob, slider or arpeggiator step drag, also while the value
// stayed inside one step. Automatable parameters pass the host and the automation limit (250/s), but the others
// (menus, globals, arpeggiator steps) go straight into the queue to the Virus, which the byte budget does not cover
// (review 2026-10-19; AGENTS.md rule 2: a parameter flood can hang the Virus' audio). A drag now sends a value only
// when it differs from the last one it sent: at most one message per value step.
#pragma once
#include <string>

namespace vti {

class DragSend {
public:
    void reset() { path_.clear(); } // a new drag: its first value always goes out
    // true when (path, part, value) differs from the last one sent in this drag; it then counts as sent
    bool changed(const std::string &path, int part, int value)
    {
        if (!path_.empty() && path == path_ && part == part_ && value == value_)
            return false;
        path_ = path, part_ = part, value_ = value;
        return true;
    }

private:
    std::string path_;
    int part_ = 0, value_ = 0;
};

} // namespace vti
