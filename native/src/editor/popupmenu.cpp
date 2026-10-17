#include "popupmenu.h"
#include <algorithm>

namespace vti {

PopupMenu::PopupMenu(std::vector<PopupItem> items, int checked, std::function<int(const std::string &)> text_width)
    : items_(std::move(items)), checked_(checked), text_width_(std::move(text_width))
{
    if (checked_ < 0 || checked_ >= (int)items_.size())
        checked_ = -1;
}

void PopupMenu::layout(int per_column, int area_h)
{
    const int n = (int)items_.size(), frame = 2 * (kBorder + kPad);
    rows_ = per_column > 0 ? std::min(per_column, n) : n;
    // a list taller than the editor wraps into more columns: a menu inside the editor cannot stick out of it
    // (the original's Windows menus are windows of their own and can)
    rows_ = std::max(1, std::min(rows_, (area_h - frame) / kRow));
    const int cols = n > 0 ? (n + rows_ - 1) / rows_ : 0;
    col_x_.assign(cols, 0);
    col_w_.assign(cols, 0);
    int cx = kBorder + 1; // 1 px more on the left than inside the border, like the original
    for (int c = 0; c < cols; c++) {
        int widest = 0;
        for (int i = c * rows_; i < std::min(n, (c + 1) * rows_); i++)
            widest = std::max(widest, text_width_(items_[i].text));
        col_x_[c] = cx;
        col_w_[c] = kTextLeft + widest + kTextRight;
        cx += col_w_[c] + (c + 1 < cols ? kSeparator : 0);
    }
    width_ = cx + kBorder;
    height_ = frame + rows_ * kRow;
}

void PopupMenu::place(int ax, int ay, int area_w, int area_h)
{
    x = std::max(0, std::min(ax, area_w - width_));
    y = std::max(0, std::min(ay, area_h - height_));
}

PopupMenu::Rect PopupMenu::item_rect(int i) const
{
    if (i < 0 || i >= (int)items_.size() || rows_ <= 0)
        return {};
    const int c = i / rows_;
    return {col_x_[c], kBorder + kPad + (i % rows_) * kRow, col_w_[c], kRow};
}

bool PopupMenu::contains(int px, int py) const
{
    return px >= x && px < x + width_ && py >= y && py < y + height_;
}

int PopupMenu::item_at(int px, int py) const
{
    if (!contains(px, py) || rows_ <= 0)
        return -1;
    const int mx = px - x, my = py - y - kBorder - kPad;
    if (my < 0 || my >= rows_ * kRow)
        return -1;
    for (int c = 0; c < columns(); c++) {
        if (mx < col_x_[c] || mx >= col_x_[c] + col_w_[c])
            continue;
        const int i = c * rows_ + my / kRow;
        return i < (int)items_.size() && items_[i].enabled ? i : -1;
    }
    return -1;
}

int PopupMenu::step(int from, int dir) const
{
    const int n = (int)items_.size();
    for (int k = 1; k <= n; k++) {
        const int i = ((from + dir * k) % n + n) % n;
        if (items_[i].enabled)
            return i;
    }
    return -1;
}

void PopupMenu::key(Key k)
{
    const int n = (int)items_.size();
    if (n == 0)
        return;
    switch (k) {
    case Key::Down:
        hot = step(hot < 0 ? -1 : hot, 1);
        break;
    case Key::Up:
        hot = step(hot < 0 ? n : hot, -1);
        break;
    case Key::Home:
        hot = step(-1, 1);
        break;
    case Key::End:
        hot = step(n, -1);
        break;
    case Key::Left:
    case Key::Right: {
        if (hot < 0 || rows_ <= 0)
            break;
        const int c = hot / rows_ + (k == Key::Right ? 1 : -1);
        if (c < 0 || c >= columns())
            break;
        int i = std::min(c * rows_ + hot % rows_, n - 1); // a shorter last column: its last entry
        while (i >= c * rows_ && !items_[i].enabled)
            i--;
        if (i >= c * rows_)
            hot = i;
        break;
    }
    }
}

} // namespace vti
