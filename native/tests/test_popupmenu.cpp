// the editor's own popup menu (E194): columns like the original's Windows menus, placement inside the editor, mouse
// and keyboard
#include "check.h"
#include "editor/popupmenu.h"
using namespace vti;

namespace {
int width6(const std::string &s) { return 6 * (int)s.size(); } // 6 px per character

std::vector<PopupItem> numbered(int n)
{
    std::vector<PopupItem> v;
    for (int i = 0; i < n; i++)
        v.push_back({"Item " + std::to_string(i), true});
    return v;
}
} // namespace

// the Wavetable list: 100 entries in 4 columns of 25 (the original's 296..778 x 146..627 menu, E189)
TEST(popup_columns)
{
    auto items = numbered(100);
    items[30].text = "A longer name"; // column 1 is wider
    PopupMenu m(items, 0, width6);
    m.layout(25, 1000);
    CHECK(m.columns() == 4 && m.rows() == 25);
    CHECK(m.height() == 2 * (PopupMenu::kBorder + PopupMenu::kPad) + 25 * PopupMenu::kRow); // 481
    const int narrow = PopupMenu::kTextLeft + 6 * 7 + PopupMenu::kTextRight; // "Item 99"
    const int wide = PopupMenu::kTextLeft + 6 * 13 + PopupMenu::kTextRight;
    CHECK(m.column_x().size() == 4);
    CHECK(m.column_x()[0] == PopupMenu::kBorder + 1); // the text at 22 like the original's (318 - 296)
    CHECK(m.column_x()[1] == m.column_x()[0] + narrow + PopupMenu::kSeparator);
    CHECK(m.column_x()[2] == m.column_x()[1] + wide + PopupMenu::kSeparator);
    CHECK(m.width() == m.column_x()[3] + narrow + PopupMenu::kBorder);

    PopupMenu::Rect r0 = m.item_rect(0), r26 = m.item_rect(26);
    CHECK(r0.x == m.column_x()[0] && r0.y == PopupMenu::kBorder + PopupMenu::kPad && r0.w == narrow &&
          r0.h == PopupMenu::kRow);
    CHECK(r26.x == m.column_x()[1] && r26.y == r0.y + PopupMenu::kRow && r26.w == wide);
}

TEST(popup_one_column)
{
    PopupMenu m(numbered(24), -1, width6); // the 24 categories
    m.layout(25, 1000);
    CHECK(m.columns() == 1 && m.rows() == 24);
    PopupMenu z(numbered(40), -1, width6);
    z.layout(0, 1000); // 0 = no columns
    CHECK(z.columns() == 1 && z.rows() == 40);
}

// the editor is 513 px high at 100 %: a list taller than that wraps into more columns instead of being cut off
TEST(popup_columns_fit_the_area)
{
    PopupMenu m(numbered(40), -1, width6);
    m.layout(0, 300);
    const int fit = (300 - 2 * (PopupMenu::kBorder + PopupMenu::kPad)) / PopupMenu::kRow; // 15
    CHECK(m.rows() == fit && m.columns() == 3);
    CHECK(m.height() <= 300);
}

// opened at the control's corner like the original, but moved inside the editor where it would stick out
TEST(popup_place)
{
    PopupMenu m(numbered(10), -1, width6);
    m.layout(25, 513);
    const int w = m.width(), h = m.height();
    m.place(296, 146, 907, 513);
    CHECK(m.x == 296 && m.y == 146);
    m.place(900, 500, 907, 513);
    CHECK(m.x == 907 - w && m.y == 513 - h);
    m.place(-5, -5, 907, 513);
    CHECK(m.x == 0 && m.y == 0);
    PopupMenu big(numbered(100), -1, width6);
    big.layout(25, 513);
    big.place(296, 146, 300, 513); // wider than the area: the left edge stays visible
    CHECK(big.x == 0 && big.y == 513 - big.height());
}

TEST(popup_item_at)
{
    auto items = numbered(30);
    items[3].enabled = false; // a greyed entry ("-G", E158) cannot be chosen
    PopupMenu m(items, -1, width6);
    m.layout(25, 513);
    m.place(100, 50, 907, 513);
    PopupMenu::Rect r = m.item_rect(2);
    CHECK(m.item_at(m.x + r.x + 5, m.y + r.y + 5) == 2);
    CHECK(m.item_at(m.x + r.x, m.y + r.y + r.h - 1) == 2); // the row's last line
    r = m.item_rect(3);
    CHECK(m.item_at(m.x + r.x + 5, m.y + r.y + 5) == -1);
    r = m.item_rect(27);
    CHECK(m.item_at(m.x + r.x + 5, m.y + r.y + 5) == 27);
    // column 1 has 5 entries: below them, inside the menu, nothing
    CHECK(m.item_at(m.x + r.x + 5, m.y + m.item_rect(29).y + PopupMenu::kRow + 2) == -1);
    CHECK(m.contains(m.x + r.x + 5, m.y + m.item_rect(29).y + PopupMenu::kRow + 2));
    // the separator between the columns and the border: inside, no entry
    CHECK(m.item_at(m.x + m.column_x()[1] - 1, m.y + 20) == -1);
    CHECK(m.item_at(m.x, m.y + 20) == -1 && m.contains(m.x, m.y + 20));
    CHECK(!m.contains(m.x - 1, m.y) && !m.contains(m.x + m.width(), m.y) && m.item_at(m.x - 1, m.y + 20) == -1);
}

TEST(popup_keys)
{
    auto items = numbered(60);
    items[1].enabled = false;
    PopupMenu m(items, 0, width6);
    m.layout(25, 1000);
    CHECK(m.hot == -1 && m.checked() == 0); // nothing highlighted until the mouse or a key moves, like the original
    m.key(PopupMenu::Key::Down);
    CHECK(m.hot == 0);
    m.key(PopupMenu::Key::Down);
    CHECK(m.hot == 2); // over the greyed entry
    m.key(PopupMenu::Key::Up);
    CHECK(m.hot == 0);
    m.key(PopupMenu::Key::Up);
    CHECK(m.hot == 59); // wraps
    m.key(PopupMenu::Key::Down);
    CHECK(m.hot == 0);
    m.key(PopupMenu::Key::Right);
    CHECK(m.hot == 25); // same row, next column
    m.key(PopupMenu::Key::Right);
    CHECK(m.hot == 50);
    m.key(PopupMenu::Key::Right);
    CHECK(m.hot == 50); // last column: stays
    m.hot = 49;
    m.key(PopupMenu::Key::Right);
    CHECK(m.hot == 59); // row 24 of a 10-entry column: its last entry
    m.key(PopupMenu::Key::Left);
    CHECK(m.hot == 34);
    m.key(PopupMenu::Key::Home);
    CHECK(m.hot == 0);
    m.key(PopupMenu::Key::End);
    CHECK(m.hot == 59);

    PopupMenu none(numbered(5), -1, width6);
    none.layout(25, 1000);
    CHECK(none.hot == -1);
    none.key(PopupMenu::Key::Down);
    CHECK(none.hot == 0);
    PopupMenu none2(numbered(5), -1, width6);
    none2.key(PopupMenu::Key::Up);
    CHECK(none2.hot == 4);
}
