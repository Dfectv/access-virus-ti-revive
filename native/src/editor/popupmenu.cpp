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

std::vector<PopupMenu::Rect> PopupMenu::set_hot(int i)
{
    std::vector<Rect> dirty;
    if (i == hot)
        return dirty;
    for (int k : {hot, i})
        if (k >= 0 && k < (int)items_.size()) {
            Rect r = item_rect(k);
            dirty.push_back({x + r.x, y + r.y, r.w, r.h});
        }
    hot = i;
    return dirty;
}

std::vector<PopupMenu::Rect> PopupMenu::key_dirty(Key k)
{
    const int before = hot;
    key(k);
    const int after = hot;
    hot = before;
    return set_hot(after);
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

PopupCascade::PopupCascade(std::vector<PopupItem> items, int checked,
                           std::function<int(const std::string &)> text_width, int per_column, int area_w, int area_h)
    : root_(std::move(items)), text_width_(std::move(text_width)), checked_(checked), per_column_(per_column),
      area_w_(area_w), area_h_(area_h)
{
}

void PopupCascade::open_at(int ax, int ay)
{
    menus_.clear();
    from_.assign(1, -1);
    pending_ = false;
    PopupMenu m(root_, checked_, text_width_);
    m.layout(per_column_, area_h_);
    m.place(ax, ay, area_w_, area_h_);
    menus_.push_back(std::move(m));
}

int PopupCascade::level_at(int px, int py) const
{
    for (int l = levels() - 1; l >= 0; l--)
        if (menus_[l].contains(px, py))
            return l;
    return -1;
}

void PopupCascade::close_after(int level, std::vector<Rect> &dirty)
{
    while (levels() > level + 1) {
        dirty.push_back(menus_.back().bounds());
        menus_.pop_back();
        from_.pop_back();
    }
}

void PopupCascade::open_sub(int level, std::vector<Rect> &dirty)
{
    PopupMenu &parent = menus_[level];
    if (parent.hot < 0)
        return;
    const PopupItem &it = parent.items()[parent.hot];
    if (!it.has_sub() || !it.enabled)
        return;
    PopupMenu m(it.sub, -1, text_width_);
    m.layout(0, area_h_);
    // like a Windows submenu: right of the parent, its first row level with the entry; on the left when there is no
    // room on the right
    const Rect r = parent.item_rect(parent.hot);
    int x = parent.x + parent.width();
    if (x + m.width() > area_w_ && parent.x - m.width() >= 0)
        x = parent.x - m.width();
    else if (x + m.width() > area_w_) {
        // room on neither side (the original's menus are windows and may leave the editor, ours may not): the parent
        // moves left over the menus further back until both fit side by side - a submenu never covers its parent
        const int shift = std::min(parent.x, x + m.width() - area_w_);
        if (shift > 0) {
            dirty.push_back(parent.bounds());
            parent.x -= shift;
            dirty.push_back(parent.bounds());
            x -= shift;
        }
    }
    m.place(x, parent.y + r.y - (PopupMenu::kBorder + PopupMenu::kPad), area_w_, area_h_);
    dirty.push_back(m.bounds());
    from_.push_back(parent.hot);
    menus_.push_back(std::move(m));
}

PopupCascade::Result PopupCascade::choose(int level, int i)
{
    Result res;
    res.kind = Result::Chosen;
    for (int l = 1; l <= level; l++) // the submenus shown, not what the pointer crossed on its way (hover)
        res.path.push_back(from_[l]);
    res.path.push_back(i);
    for (auto &m : menus_)
        res.dirty.push_back(m.bounds());
    return res;
}

void PopupCascade::settle(int level, std::vector<Rect> &dirty)
{
    // the pointer reached a menu below the waiting change (the submenu it was heading for): the change is dropped
    // and the parent's entry of the open submenu is highlighted again
    if (!pending_ || level <= pending_level_)
        return;
    pending_ = false;
    if (pending_level_ + 1 < levels())
        for (auto &r : menus_[pending_level_].set_hot(from_[pending_level_ + 1]))
            dirty.push_back(r);
}

std::vector<PopupCascade::Rect> PopupCascade::hover(int px, int py, long long now)
{
    std::vector<Rect> dirty;
    const int l = level_at(px, py);
    if (l < 0)
        return dirty;
    const int i = menus_[l].item_at(px, py);
    if (i < 0) // a border or a greyed entry
        return dirty;
    settle(l, dirty);
    const bool open_here = levels() > l + 1 && from_[l + 1] == i; // its submenu is the one open
    if (delay_ <= 0) {
        if (open_here)
            return dirty;
        close_after(l, dirty);
        for (auto &r : menus_[l].set_hot(i))
            dirty.push_back(r);
        open_sub(l, dirty);
        return dirty;
    }
    for (auto &r : menus_[l].set_hot(i))
        dirty.push_back(r);
    const bool change = !open_here && (levels() > l + 1 || menus_[l].items()[i].has_sub());
    if (!change) {
        if (pending_ && pending_level_ == l)
            pending_ = false; // back on the entry whose submenu is open
        return dirty;
    }
    if (!pending_ || pending_level_ != l || pending_item_ != i) { // a new entry starts the wait, moves on it do not
        pending_ = true;
        pending_level_ = l;
        pending_item_ = i;
        deadline_ = now + delay_;
    }
    return dirty;
}

std::vector<PopupCascade::Rect> PopupCascade::tick(long long now)
{
    std::vector<Rect> dirty;
    if (!pending_ || now < deadline_)
        return dirty;
    pending_ = false;
    if (pending_level_ >= levels() || menus_[pending_level_].hot != pending_item_)
        return dirty;
    close_after(pending_level_, dirty);
    open_sub(pending_level_, dirty);
    return dirty;
}

PopupCascade::Result PopupCascade::click(int px, int py)
{
    Result res;
    const int l = level_at(px, py);
    if (l < 0) { // a click outside only closes the menu, like a native one
        res.kind = Result::Closed;
        for (auto &m : menus_)
            res.dirty.push_back(m.bounds());
        return res;
    }
    const int i = menus_[l].item_at(px, py);
    if (i < 0)
        return res;
    settle(l, res.dirty);
    if (menus_[l].items()[i].has_sub()) { // opens the submenu at once (if it is not open yet), chooses nothing
        pending_ = false;
        if (levels() > l + 1 && from_[l + 1] == i)
            return res;
        close_after(l, res.dirty);
        for (auto &r : menus_[l].set_hot(i))
            res.dirty.push_back(r);
        open_sub(l, res.dirty);
        return res;
    }
    return choose(l, i);
}

PopupCascade::Result PopupCascade::key(Key k)
{
    Result res;
    if (menus_.empty())
        return res;
    settle(levels(), res.dirty); // keys act on the menus shown, at once
    const int top = levels() - 1;
    PopupMenu &m = menus_[top];
    const bool sub = m.hot >= 0 && m.items()[m.hot].has_sub() && m.items()[m.hot].enabled;
    auto enter = [&]() { // opens the highlighted entry's submenu at its first entry
        open_sub(top, res.dirty);
        for (auto &r : menus_.back().key_dirty(PopupMenu::Key::Down))
            res.dirty.push_back(r);
    };
    auto move = [&](PopupMenu::Key pk) {
        for (auto &r : m.key_dirty(pk))
            res.dirty.push_back(r);
    };
    switch (k) {
    case Key::Up: move(PopupMenu::Key::Up); break;
    case Key::Down: move(PopupMenu::Key::Down); break;
    case Key::Home: move(PopupMenu::Key::Home); break;
    case Key::End: move(PopupMenu::Key::End); break;
    case Key::Right:
        if (sub)
            enter();
        else
            move(PopupMenu::Key::Right);
        break;
    case Key::Left: {
        const int before = m.hot;
        move(PopupMenu::Key::Left);
        if (m.hot == before && top > 0) // the first column: back to the parent
            close_after(top - 1, res.dirty);
        break;
    }
    case Key::Return:
        if (sub)
            enter();
        else if (m.hot >= 0 && m.items()[m.hot].enabled)
            return choose(top, m.hot);
        break;
    case Key::Escape:
        if (top > 0) {
            close_after(top - 1, res.dirty);
            break;
        }
        res.kind = Result::Closed;
        res.dirty.push_back(m.bounds());
        break;
    }
    return res;
}

std::vector<int> popup_add_path(std::vector<PopupItem> &root, const std::string &name, PopupItem entry,
                                std::map<std::string, std::vector<int>> &folders)
{
    std::vector<int> path;
    size_t from = 0;
    for (size_t slash; (slash = name.find('/', from)) != std::string::npos; from = slash + 1) {
        auto it = folders.find(name.substr(0, slash));
        if (it == folders.end()) {
            std::vector<PopupItem> *m = &root;
            for (int k : path)
                m = &(*m)[k].sub;
            m->push_back({name.substr(from, slash - from)});
            path.push_back((int)m->size() - 1);
            it = folders.emplace(name.substr(0, slash), path).first;
        }
        path = it->second;
    }
    std::vector<PopupItem> *m = &root;
    for (int k : path)
        m = &(*m)[k].sub;
    entry.text = name.substr(from);
    m->push_back(std::move(entry));
    path.push_back((int)m->size() - 1);
    return path;
}

} // namespace vti
