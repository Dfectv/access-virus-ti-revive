// editormodel.h - the original editor's layout from the user's editor.xml (docs/formats.md): an element tree with
// positions relative to the parent, PNG backgrounds, parameter bindings and visibility conditions. Conditions come
// from remotedevice.xml (device parameters) and editordevice.xml (editor state such as VirusControl/SelectedPage).
#pragma once
#include "model/remotedevice.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vti {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Element {
    std::string type; // Container, Knob, RadioButton, Button, Menu, StaticText, ... (editor.xml element name)
    std::map<std::string, std::string> attr;
    int x = 0, y = 0, w = 0, h = 0; // relative to the parent
    Rgba back, fore;
    std::vector<Element> children;

    const std::string &get(const char *k) const
    {
        static const std::string none;
        auto it = attr.find(k);
        return it == attr.end() ? none : it->second;
    }
    int part() const { return get("Part").empty() ? -1 : atoi(get("Part").c_str()); } // -1 = current part
};

struct EditorLayout {
    int width = 0, height = 0;
    std::string frame_parameter; // the part selector ("Multi Parameters/Panel Part")
    std::vector<Element> elements;
    std::map<std::string, std::string> bitmaps; // id (bmpNNNNN) -> PNG name (most elements use PNG names directly)
    bool load(const std::vector<uint8_t> &xml, std::string &error);
};

// Parameter values for conditions and controls: (path, part) -> value. part -1 = not part-specific.
using ValueFn = std::function<int(const std::string &path, int part)>;

// Condition evaluation over both device descriptions (remotedevice + editordevice). Several definitions with the
// same name are alternatives (any of them true -> true). Unknown condition names count as true.
class Conditions {
public:
    Conditions(const RemoteDevice &device, const RemoteDevice &editor) : dev_(device), ed_(editor) {}
    bool eval(const std::string &name, const ValueFn &values, int part) const;
    int default_value(const std::string &path) const; // parameter default from either description (0 if unknown)
    const Parameter *find(const std::string &path) const;

private:
    const RemoteDevice &dev_, &ed_;
};

} // namespace vti
