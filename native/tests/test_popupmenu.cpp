// the editor's own popup menu (E194): columns like the original's Windows menus, placement inside the editor, mouse
// and keyboard
#include "check.h"
#include "editor/popupmenu.h"
#include <map>
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

// a hover repaints only the two rows that change, not the editor below the menu (E194: the Wavetable list was slow
// on the Mac because every mouse move repainted the whole editor)
TEST(popup_repaint_areas)
{
    PopupMenu m(numbered(30), -1, width6);
    m.layout(25, 513);
    m.place(100, 20, 907, 513);
    PopupMenu::Rect b = m.bounds();
    CHECK(b.x == 100 && b.y == 20 && b.w == m.width() && b.h == m.height());

    auto dirty = m.set_hot(2);
    CHECK(dirty.size() == 1); // nothing was highlighted: the new row only
    PopupMenu::Rect r2 = m.item_rect(2);
    CHECK(dirty[0].x == m.x + r2.x && dirty[0].y == m.y + r2.y && dirty[0].w == r2.w && dirty[0].h == r2.h);
    CHECK(m.hot == 2);
    CHECK(m.set_hot(2).empty()); // no change, no repaint
    dirty = m.set_hot(27);
    CHECK(dirty.size() == 2 && dirty[0].y == m.y + r2.y && dirty[1].x == m.x + m.item_rect(27).x);
    dirty = m.set_hot(-1);
    CHECK(dirty.size() == 1 && m.hot == -1);
    // keys report their rows the same way
    dirty = m.key_dirty(PopupMenu::Key::Down);
    CHECK(m.hot == 0 && dirty.size() == 1 && dirty[0].y == m.y + m.item_rect(0).y);
}

namespace {
// "Device > RAM A, RAM B" and "Personal Libraries > Folder > Lib" like the patch menus (E195)
std::vector<PopupItem> tree()
{
    PopupItem dev{"Virus TI", true, {{"RAM A", true, numbered(128)}, {"RAM B", true, numbered(128)}}};
    PopupItem folder{"Folder", true, {{"Lib", true, numbered(3)}}};
    PopupItem libs{"Personal Libraries", true, {folder, {"Lone", true, numbered(2)}}};
    return {dev, libs, {"Leaf", true, {}}};
}
} // namespace

// submenus open next to their entry on hover; another entry switches or closes them
TEST(popup_cascade_hover)
{
    PopupCascade c(tree(), -1, width6, 0, 907, 513);
    c.open_at(100, 50);
    CHECK(c.levels() == 1 && c.level(0).x == 100 && c.level(0).y == 50);
    CHECK(c.level(0).items()[0].has_sub() && !c.level(0).items()[2].has_sub());

    PopupMenu::Rect r0 = c.level(0).item_rect(0);
    auto dirty = c.hover(c.level(0).x + r0.x + 5, c.level(0).y + r0.y + 5);
    CHECK(c.levels() == 2 && c.level(0).hot == 0);
    const PopupMenu &sub = c.level(1);
    CHECK(sub.items().size() == 2 && sub.hot == -1);
    // to the right of its parent, the first row level with the parent's entry
    CHECK(sub.x == c.level(0).x + c.level(0).width());
    CHECK(sub.y + sub.item_rect(0).y == c.level(0).y + r0.y);
    bool sub_repainted = false;
    for (auto &d : dirty)
        sub_repainted |= d.x == sub.x && d.y == sub.y && d.w == sub.width() && d.h == sub.height();
    CHECK(sub_repainted);

    // into the bank list: 128 patches wrap into columns that fit the editor's height
    PopupMenu::Rect ra = sub.item_rect(0);
    c.hover(sub.x + ra.x + 5, sub.y + ra.y + 5);
    CHECK(c.levels() == 3 && c.level(2).items().size() == 128);
    CHECK(c.level(2).height() <= 513 && c.level(2).columns() > 1);
    CHECK(c.level(2).y >= 0 && c.level(2).y + c.level(2).height() <= 513);

    // back on the root's "Leaf": the submenus close
    PopupMenu::Rect r2 = c.level(0).item_rect(2);
    c.hover(c.level(0).x + r2.x + 5, c.level(0).y + r2.y + 5);
    CHECK(c.levels() == 1 && c.level(0).hot == 2);
    // the mouse beside every menu changes nothing
    c.hover(5, 5);
    CHECK(c.levels() == 1 && c.level(0).hot == 2);
}

// no room on the right: the submenu opens on the left of its parent
TEST(popup_cascade_flips_left)
{
    PopupCascade c(tree(), -1, width6, 0, 907, 513);
    c.open_at(880, 50); // moved left to fit
    CHECK(c.level(0).x + c.level(0).width() <= 907);
    PopupMenu::Rect r1 = c.level(0).item_rect(1);
    c.hover(c.level(0).x + r1.x + 5, c.level(0).y + r1.y + 5);
    CHECK(c.levels() == 2 && c.level(1).x + c.level(1).width() == c.level(0).x);
}

TEST(popup_cascade_click)
{
    PopupCascade c(tree(), -1, width6, 0, 907, 513);
    c.open_at(100, 50);
    PopupMenu::Rect r1 = c.level(0).item_rect(1);
    // a click on an entry with a submenu opens it and chooses nothing
    auto res = c.click(c.level(0).x + r1.x + 5, c.level(0).y + r1.y + 5);
    CHECK(res.kind == PopupCascade::Result::None && c.levels() == 2);
    PopupMenu::Rect f = c.level(1).item_rect(0); // Folder
    c.hover(c.level(1).x + f.x + 5, c.level(1).y + f.y + 5);
    CHECK(c.levels() == 3);
    PopupMenu::Rect l = c.level(2).item_rect(0); // Lib
    c.hover(c.level(2).x + l.x + 5, c.level(2).y + l.y + 5);
    CHECK(c.levels() == 4);
    PopupMenu::Rect p = c.level(3).item_rect(2);
    res = c.click(c.level(3).x + p.x + 5, c.level(3).y + p.y + 5);
    CHECK(res.kind == PopupCascade::Result::Chosen);
    CHECK((res.path == std::vector<int>{1, 0, 0, 2}));

    PopupCascade d(tree(), -1, width6, 0, 907, 513);
    d.open_at(100, 50);
    CHECK(d.click(2, 2).kind == PopupCascade::Result::Closed); // outside every menu
    PopupCascade e(tree(), -1, width6, 0, 907, 513);
    e.open_at(100, 50);
    PopupMenu::Rect leaf = e.level(0).item_rect(2);
    res = e.click(e.level(0).x + leaf.x + 5, e.level(0).y + leaf.y + 5);
    CHECK(res.kind == PopupCascade::Result::Chosen && (res.path == std::vector<int>{2}));
}

// keys like a Windows menu: Right / Return open a submenu at its first entry, Left / Esc close it, Esc on the root
// closes the menu
TEST(popup_cascade_keys)
{
    using K = PopupCascade::Key;
    PopupCascade c(tree(), -1, width6, 0, 907, 513);
    c.open_at(100, 50);
    c.key(K::Down);
    CHECK(c.levels() == 1 && c.level(0).hot == 0);
    c.key(K::Right);
    CHECK(c.levels() == 2 && c.level(1).hot == 0);
    c.key(K::Down);
    CHECK(c.level(1).hot == 1);
    c.key(K::Left);
    CHECK(c.levels() == 1 && c.level(0).hot == 0);
    c.key(K::Return);
    CHECK(c.levels() == 2 && c.level(1).hot == 0);
    c.key(K::Return);
    CHECK(c.levels() == 3 && c.level(2).hot == 0);
    c.key(K::Down);
    auto res = c.key(K::Return);
    CHECK(res.kind == PopupCascade::Result::Chosen && (res.path == std::vector<int>{0, 0, 1}));

    PopupCascade d(tree(), -1, width6, 0, 907, 513);
    d.open_at(100, 50);
    d.key(K::Down);
    d.key(K::Right);
    CHECK(d.key(K::Escape).kind == PopupCascade::Result::None && d.levels() == 1);
    CHECK(d.key(K::Left).kind == PopupCascade::Result::None && d.levels() == 1); // the root stays
    CHECK(d.key(K::Escape).kind == PopupCascade::Result::Closed);

    // inside a submenu with columns Left/Right move between columns first
    PopupCascade e(tree(), -1, width6, 0, 907, 513);
    e.open_at(10, 10);
    e.key(K::Down);
    e.key(K::Right); // Virus TI
    e.key(K::Right); // RAM A, 128 patches in columns
    CHECK(e.levels() == 3 && e.level(2).hot == 0 && e.level(2).columns() > 1);
    e.key(K::Right);
    CHECK(e.levels() == 3 && e.level(2).hot == e.level(2).rows());
    e.key(K::Left);
    CHECK(e.levels() == 3 && e.level(2).hot == 0);
    e.key(K::Left);
    CHECK(e.levels() == 2);
}

// a plain list (no submenus) behaves like PopupMenu: the root's columns and the check mark
TEST(popup_cascade_flat)
{
    PopupCascade c(numbered(100), 7, width6, 25, 907, 513);
    c.open_at(296, 146);
    CHECK(c.levels() == 1 && c.level(0).columns() == 4 && c.level(0).checked() == 7);
    c.key(PopupCascade::Key::Down);
    auto res = c.key(PopupCascade::Key::Return);
    CHECK(res.kind == PopupCascade::Result::Chosen && (res.path == std::vector<int>{0}));
}

// libraries named with folders ("Factory/Rom/A") become submenus; each library's entry path is returned, and a folder
// and a library may share a name
TEST(popup_library_folders)
{
    std::vector<PopupItem> root;
    std::map<std::string, std::vector<int>> folders;
    auto a = popup_add_path(root, "Factory/Rom/A", {"", true, numbered(2)}, folders);
    auto mine = popup_add_path(root, "Mine", {"", true, numbered(1)}, folders);
    auto b = popup_add_path(root, "Factory/Rom/B", {"", true, numbered(1)}, folders);
    auto f = popup_add_path(root, "Factory", {"", true, numbered(1)}, folders); // a library called like the folder
    auto c = popup_add_path(root, "Factory/C", {""}, folders);
    CHECK((a == std::vector<int>{0, 0, 0}) && (mine == std::vector<int>{1}) && (b == std::vector<int>{0, 0, 1}));
    CHECK((f == std::vector<int>{2}) && (c == std::vector<int>{0, 1}));
    CHECK(root.size() == 3 && root[0].text == "Factory" && root[0].sub.size() == 2 && root[0].sub[0].text == "Rom");
    CHECK(root[0].sub[0].sub[1].text == "B" && root[0].sub[1].text == "C" && !root[0].sub[1].has_sub());
    CHECK(root[2].text == "Factory" && root[2].sub.size() == 1);
}
