// nerdmode.cpp - see nerdmode.h.
#include "nerdmode.h"
#include "cc/diag.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include <ctime>

using namespace VSTGUI;

namespace tirevive {
namespace {

const CColor kBack(30, 30, 30), kText(220, 220, 220), kDim(150, 150, 150), kAccent(240, 170, 60), kGreen(10, 110, 30);
constexpr CCoord W = kNerdWidth, H = kNerdHeight, M = 16, kLine = 15;
enum Tag { kBack_ = 1, kPluginTab, kBrokerTab, kPause, kPluginLog, kSave };

#if defined(_WIN32)
const char *kMono = "Consolas";
#elif defined(__APPLE__)
const char *kMono = "Menlo";
#else
const char *kMono = "Monospace";
#endif

class NerdPage : public CViewContainer, public IControlListener {
public:
    NerdPage(std::string version, std::function<void()> back)
        : CViewContainer(CRect(0, 0, W, H)), version_(std::move(version)), back_(std::move(back))
    {
        setBackgroundColor(kBack);
        build();
    }
    ~NerdPage() noexcept override
    {
        if (timer_)
            timer_->stop();
    }

    void activate(bool on)
    {
        setVisible(on);
        if (!on) {
            timer_ = nullptr;
            return;
        }
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) { refresh(); }, 1000, true);
        refresh(true);
    }

    void valueChanged(CControl *c) override
    {
        if (c->getValue() < 0.5f)
            return;
        switch (c->getTag()) {
        case kBack_: back_(); return;
        case kPluginTab: broker_ = false; break;
        case kBrokerTab: broker_ = true; break;
        case kPause: paused_ = !paused_; break;
        case kPluginLog: {
            std::string err;
            if (!vti::cc::set_plugin_log(!vti::cc::plugin_log_enabled(), err))
                message_ = err;
            else
                message_ = vti::cc::plugin_log_enabled()
                               ? "Plug-in log on: it starts with the next start of the plug-in (reload it or restart the DAW)."
                               : "Plug-in log off (from the next start of the plug-in).";
            break;
        }
        case kSave: save(); break;
        }
        refresh(true);
    }

private:
    std::string version_;
    std::function<void()> back_;
    SharedPointer<CVSTGUITimer> timer_;
    CTextButton *tabs_[2] = {}, *pause_ = nullptr, *log_switch_ = nullptr;
    CTextLabel *where_ = nullptr, *state_ = nullptr;
    CMultiLineTextLabel *text_ = nullptr;
    std::string message_;
    bool broker_ = false, paused_ = false;

    CTextLabel *label(const CRect &r, const CColor &color, CFontRef font)
    {
        auto *l = new CTextLabel(r, "");
        l->setFontColor(color);
        l->setFont(font);
        l->setHoriAlign(kLeftText);
        l->setTransparency(true);
        l->setTextTruncateMode(CTextLabel::kTruncateHead);
        addView(l);
        return l;
    }
    CTextButton *button(CCoord x, CCoord y, CCoord w, int tag, const char *title)
    {
        auto *b = new CTextButton(CRect(x, y, x + w, y + 26), this, tag, title);
        addView(b);
        return b;
    }

    void build()
    {
        auto title = makeOwned<CFontDesc>(*kSystemFont);
        title->setSize(16);
        title->setStyle(kBoldFace);
        label(CRect(M, 10, W - M - 100, 34), kText, title)->setText("Nerd mode: what TI Revive is doing");
        button(W - M - 90, 10, 90, kBack_, "Back");
        tabs_[0] = button(M, 44, 110, kPluginTab, "Plug-in log");
        tabs_[1] = button(M + 116, 44, 110, kBrokerTab, "Broker log");
        pause_ = button(M + 240, 44, 80, kPause, "Pause");
        log_switch_ = button(W - M - 330, 44, 160, kPluginLog, "");
        button(W - M - 160, 44, 160, kSave, "Save logs...");
        where_ = label(CRect(M, 78, W - M, 96), kDim, kNormalFontSmall);
        state_ = label(CRect(M, H - 26, W - M, H - 8), kAccent, kNormalFontSmall);
        auto mono = makeOwned<CFontDesc>(kMono, 11);
        text_ = new CMultiLineTextLabel(CRect(M, 102, W - M, H - 32));
        text_->setFontColor(kText);
        text_->setFont(mono);
        text_->setHoriAlign(kLeftText);
        text_->setBackColor(CColor(18, 18, 18));
        text_->setStyle(CTextLabel::kNoFrame);
        text_->setLineLayout(CMultiLineTextLabel::LineLayout::truncate);
        text_->setTextInset({6, 4});
        addView(text_);
    }

    void refresh(bool force = false)
    {
        bool on = vti::cc::plugin_log_enabled();
        log_switch_->setTitle(on ? "Plug-in log: ON" : "Plug-in log: OFF");
        log_switch_->setTextColor(on ? kGreen : kText);
        tabs_[0]->setAlphaValue(broker_ ? 0.55f : 1.f);
        tabs_[1]->setAlphaValue(broker_ ? 1.f : 0.55f);
        pause_->setTitle(paused_ ? "Resume" : "Pause");
        where_->setText((broker_ ? "Broker log: " + vti::cc::broker_log_location()
                                 : "Plug-in log: " + vti::cc::plugin_log_path() +
                                       (on ? "" : "  (off: switch it on to see what the plug-in does)"))
                            .c_str());
        state_->setText(message_.c_str());
        if (paused_ && !force)
            return;
        const size_t n = (size_t)((H - 32 - 102 - 8) / kLine);
        auto lines = broker_ ? vti::cc::broker_log_tail(n) : vti::cc::tail_file(vti::cc::plugin_log_path(), n);
        std::string t;
        for (auto &l : lines)
            t += l + "\n";
        if (lines.empty())
            t = broker_ ? "(no broker log yet: it is written while a program uses the Virus)" : "(no plug-in log yet)";
        text_->setText(t.c_str());
    }

    void save()
    {
        auto sel = owned(CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectSaveFile));
        if (!sel)
            return;
        char stamp[32];
        time_t now = time(nullptr);
        strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M", localtime(&now));
        sel->setTitle("Save logs for a bug report");
        sel->setDefaultSaveName((std::string("TI-Revive-logs-") + stamp + ".txt").c_str());
        sel->setDefaultExtension(CFileExtension("Text", "txt"));
        bool shown = sel->run([this](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::string out = s->getSelectedFile(0), err;
            message_ = vti::cc::collect_logs(out, version_, err)
                           ? "Saved (home folder and user name replaced; please look through it before attaching): " + out
                           : "Not saved: " + err;
            refresh(true);
        });
        if (!shown)
            message_ = "No file dialog could be opened (Linux: install zenity or kdialog).";
    }
};

} // namespace

CViewContainer *make_nerd_page(const std::string &version, std::function<void()> back)
{
    auto *p = new NerdPage(version, std::move(back));
    p->setVisible(false);
    return p;
}

void set_nerd_page_active(CViewContainer *page, bool on)
{
    if (auto *p = dynamic_cast<NerdPage *>(page))
        p->activate(on);
}

} // namespace tirevive
