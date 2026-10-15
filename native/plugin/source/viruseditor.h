// viruseditor.h - the plug-in editor: the original layout (editor.xml + PNGs from the user's Access installation)
// drawn by the shared EditorRenderer into a VSTGUI view; knobs/sliders by vertical drag, radio buttons and buttons by
// click. Menus, text edits and the custom displays follow (R4).
#pragma once
#include "editor/editorrender.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cview.h"
#include <map>
#include <memory>

namespace vti {

class VirusPlugin;

class VirusEditor : public Steinberg::Vst::VSTGUIEditor {
public:
    explicit VirusEditor(VirusPlugin *plugin);
    bool PLUGIN_API open(void *parent, const VSTGUI::PlatformType &platformType) override;
    void PLUGIN_API close() override;

private:
    VirusPlugin *plugin_;
};

} // namespace vti
