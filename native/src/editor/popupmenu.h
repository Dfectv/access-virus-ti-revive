// popupmenu.h - the editor's own popup menus: laid out like the original's Windows menus (E189, E194) - columns of n
// entries with a separator line between them, opened at the control's corner, submenus next to their entry (E195) -
// and drawn by the plug-in the same way on Windows, macOS and Linux (VSTGUI's native menus have columns only on
// Windows). Geometry, mouse and keyboard logic only; no drawing, no platform code.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vti {

struct PopupItem {
    PopupItem(std::string t, bool on = true, std::vector<PopupItem> s = {})
        : text(std::move(t)), enabled(on), sub(std::move(s))
    {
    }
    std::string text;
    bool enabled = true;
    std::vector<PopupItem> sub; // a submenu (the patch and bank menus, E195); empty for a plain entry
    bool separator = false;     // a separator line (Windows' MF_SEPARATOR, E217): never chosen
    bool has_sub() const { return !sub.empty(); }
    static PopupItem line()
    {
        PopupItem it("", false);
        it.separator = true;
        return it;
    }
};

class PopupMenu {
public:
    // measured on the original's Wavetable menu (Windows 10, a menu with columns): 1 px border, 2 px inside it,
    // 19 px rows, the text 20 px after a column's left edge (the check mark lives there) and 26 px before its right
    // one, columns 2 px apart (a dark and a light line)
    static constexpr int kBorder = 1, kPad = 2, kRow = 19, kTextLeft = 20, kTextRight = 26, kSeparator = 2;
    // Windows 10 draws a menu with columns as above, a menu without them in its themed look (E219, measured with a
    // test menu in the win10 VM): 1 px border + 2 px inside it, 22 px rows over the full width, a 28 px band for the
    // check mark, the text 32 px into the row and 54 px of room after it; separator rows 9 px (columns) / 7 px (themed)
    static constexpr int kLine = 9, kThemedFrame = 3, kThemedRow = 22, kThemedLine = 7, kThemedGutter = 28,
                         kThemedTextLeft = 32, kThemedTextRight = 54;

    enum class Key { Up, Down, Left, Right, Home, End };

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
    bool themed() const { return themed_; } // one column: Windows 10's themed menu look
    // x of each column's left edge, relative to the menu (the separators lie in the 2 px before columns 1..n-1)
    const std::vector<int> &column_x() const { return col_x_; }
    Rect item_rect(int i) const; // relative to the menu
    const std::vector<PopupItem> &items() const { return items_; }
    int checked() const { return checked_; }

    Rect bounds() const { return {x, y, width_, height_}; } // the menu in area coordinates
    // highlights entry i (-1: none) and returns the area rectangles to repaint: the rows that changed, nothing else
    std::vector<Rect> set_hot(int i);
    std::vector<Rect> key_dirty(Key k); // key() + the rows to repaint

    bool contains(int px, int py) const;    // area coordinates
    int item_at(int px, int py) const;      // the enabled entry under an area point, or -1
    int hot = -1;                           // the highlighted entry (mouse or keys), -1 for none

    // moves `hot` like a Windows menu: up/down within the list (wrapping, over disabled entries), left/right to the
    // same row of the neighbouring column
    void key(Key k);

private:
    std::vector<PopupItem> items_;
    int checked_;
    std::function<int(const std::string &)> text_width_;
    std::vector<int> col_x_, col_w_;
    int rows_ = 0, width_ = 0, height_ = 0;
    bool themed_ = false;
    std::vector<int> row_y_; // each entry's top within its column (variable: separator rows are lower)
    int row_h(int i) const;
    int step(int from, int dir) const; // the next enabled entry from `from` in direction dir (+-1), wrapping
};

// a menu with submenus: the open menus from the root to the deepest submenu. A submenu opens when the mouse rests on
// its entry (after the submenu delay, like Windows' menu show delay; at once with delay 0), by a click on it, or by
// Right / Return; Left / Esc close the deepest. With a delay the highlight follows the mouse at once, but submenus
// open, switch and close only when the wait is over (tick); reaching the open submenu before that keeps it.
// Submenus have one column, wrapped into more when taller than the area (the original's submenus get no column
// count, E195).
class PopupCascade {
public:
    using Rect = PopupMenu::Rect;
    enum class Key { Up, Down, Left, Right, Home, End, Return, Escape };
    struct Result {
        enum Kind { None, Chosen, Closed } kind = None;
        std::vector<int> path;   // Chosen: the entry's index in each menu from the root
        std::vector<Rect> dirty; // the area rectangles to repaint
    };

    // `checked` / `per_column`: the root menu's, see PopupMenu; the area (0, 0, w, h) the menus must stay inside
    PopupCascade(std::vector<PopupItem> items, int checked, std::function<int(const std::string &)> text_width,
                 int per_column, int area_w, int area_h);

    void open_at(int ax, int ay); // the root's top-left corner (moved to fit, PopupMenu::place)
    int levels() const { return (int)menus_.size(); }
    const PopupMenu &level(int i) const { return menus_[i]; }
    int from(int i) const { return from_[i]; } // the parent's entry of menu i (-1 for the root)

    void set_submenu_delay(int ms) { delay_ = ms; }
    // `now`: a clock in ms (only differences count)
    std::vector<Rect> hover(int px, int py, long long now = 0);
    std::vector<Rect> tick(long long now); // a waiting submenu change once its time has come
    bool pending() const { return pending_; }
    Result click(int px, int py);
    Result key(Key k);

private:
    std::vector<PopupItem> root_;
    std::function<int(const std::string &)> text_width_;
    std::vector<PopupMenu> menus_;
    std::vector<int> from_; // from_[l]: the entry of menus_[l - 1] that menus_[l] belongs to (from_[0] unused)
    int checked_, per_column_, area_w_, area_h_;
    int delay_ = 0;
    bool pending_ = false;     // a submenu change waits: level pending_level_'s highlighted entry gets its submenu
    int pending_level_ = 0, pending_item_ = -1;
    long long deadline_ = 0;

    int level_at(int px, int py) const; // the deepest open menu containing the point, or -1
    void close_after(int level, std::vector<Rect> &dirty);
    void open_sub(int level, std::vector<Rect> &dirty); // the submenu of level's highlighted entry, if it has one
    void settle(int level, std::vector<Rect> &dirty);   // the pointer in `level`: a wait above it ends, see hover
    Result choose(int level, int i);
};

// adds `entry` as the library `name` below its folders ("Factory/Rom/A" -> submenus "Factory" > "Rom", created on
// demand like in the original) and returns its index path inside `root`; the entry gets the name's last part.
// Entries are only appended, so earlier paths stay valid; `folders` maps a folder's name path to its index path
// (a library and a folder may share a name).
std::vector<int> popup_add_path(std::vector<PopupItem> &root, const std::string &name, PopupItem entry,
                                std::map<std::string, std::vector<int>> &folders);

} // namespace vti
