// popupmenu.h - the editor's own popup menu for value lists: laid out like the original's Windows menus (E189, E194) -
// columns of n entries with a separator line between them, opened at the control's corner - and drawn by the plug-in
// the same way on Windows, macOS and Linux (VSTGUI's native menus have columns only on Windows). Geometry, mouse and
// keyboard logic only; no drawing, no platform code.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace vti {

struct PopupItem {
    std::string text;
    bool enabled = true;
};

class PopupMenu {
public:
    // measured on the original's Wavetable menu (Windows 10, a menu with columns): 1 px border, 2 px inside it,
    // 19 px rows, the text 20 px after a column's left edge (the check mark lives there) and 26 px before its right
    // one, columns 2 px apart (a dark and a light line)
    static constexpr int kBorder = 1, kPad = 2, kRow = 19, kTextLeft = 20, kTextRight = 26, kSeparator = 2;

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
    };

    // `checked`: the entry with the check mark (the current value), -1 for none
    PopupMenu(std::vector<PopupItem> items, int checked, std::function<int(const std::string &)> text_width);

    // columns of `per_column` entries (0 = one column); fewer per column when the area is too low for them
    void layout(int per_column, int area_h);
    // the top-left corner at (ax, ay), moved left and up as far as needed to stay inside the area (0, 0, w, h)
    void place(int ax, int ay, int area_w, int area_h);

    int x = 0, y = 0; // the top-left corner in area coordinates (place)
    int width() const { return width_; }
    int height() const { return height_; }
    int columns() const { return (int)col_x_.size(); }
    int rows() const { return rows_; }
    // x of each column's left edge, relative to the menu (the separators lie in the 2 px before columns 1..n-1)
    const std::vector<int> &column_x() const { return col_x_; }
    Rect item_rect(int i) const; // relative to the menu
    const std::vector<PopupItem> &items() const { return items_; }
    int checked() const { return checked_; }

    bool contains(int px, int py) const;    // area coordinates
    int item_at(int px, int py) const;      // the enabled entry under an area point, or -1
    int hot = -1;                           // the highlighted entry (mouse or keys), -1 for none

    enum class Key { Up, Down, Left, Right, Home, End };
    // moves `hot` like a Windows menu: up/down within the list (wrapping, over disabled entries), left/right to the
    // same row of the neighbouring column
    void key(Key k);

private:
    std::vector<PopupItem> items_;
    int checked_;
    std::function<int(const std::string &)> text_width_;
    std::vector<int> col_x_, col_w_;
    int rows_ = 0, width_ = 0, height_ = 0;
    int step(int from, int dir) const; // the next enabled entry from `from` in direction dir (+-1), wrapping
};

} // namespace vti
