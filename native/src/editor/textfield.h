// textfield.h - the font of the editor's text fields (patch name, the REMOTE page's names) from the field's height.
//
// Why: the patch name fields are 10 px high in the layout and the name edit kept VSTGUI's default 12 pt; macOS' native
// text field showed the text overflowing the field (round 0.8.28, user). The REMOTE page's fields (10-13 px) already
// used this rule on every platform.
#pragma once
#include <algorithm>

namespace vti {

inline double text_field_font(double field_h) { return std::max(7.0, field_h - 1); } // points, VSTGUI scales with zoom

} // namespace vti
