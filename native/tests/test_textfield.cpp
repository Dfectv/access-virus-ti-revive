// The font of the editor's text fields from the field's height (textfield.h): the patch name fields are 10 px high
// and got VSTGUI's default 12 pt, which overflowed the field on macOS (round 0.8.28, user).
#include "check.h"
#include "editor/textfield.h"

using namespace vti;

TEST(textfield_font_fits_the_field)
{
    CHECK(text_field_font(10) == 9.0); // the patch name (60 x 10 px in the layout)
    CHECK(text_field_font(13) == 12.0); // the REMOTE page's highest field
    CHECK(text_field_font(4) == 7.0);  // never below 7 pt
}
