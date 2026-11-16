#include "Dx7Panel.h"
#include <algorithm>
#include <cmath>

namespace {
const juce::Colour cBg{0xff111318}, cPanel{0xff1a1d24}, cEdge{0xff2a2f3a}, cText{0xffe6e8ee},
    cDim{0xff8a90a0}, cAccent{0xffff8a3d}, cOk{0xff5ad19a}, cErr{0xffff5a6a};

constexpr int kRow1Y = 10, kGlobalY = 50, kPitch = 24, kCellH = 22;
constexpr int kOpHeadY = 260, kEgY = 282, kOpRowsY = 342, kOpPitch = 22, kLabelW = 88, kOpW = 150, kOpX0 = 100;

using zircon::dx7::OpParams;
using zircon::dx7::Voice;

juce::String noteName(int midi) { return juce::MidiMessage::getMidiNoteName(midi, true, true, 3); }

struct OpRow {
    const char* name;
    int lo, hi;
    std::function<int&(Voice&, int)> ref;
    std::function<juce::String(int)> fmt;
};

std::vector<OpRow> makeOpRows() {
    auto curve = [](int v) { static const char* n[4] = {"-LIN", "-EXP", "+EXP", "+LIN"}; return juce::String(n[v & 3]); };
    std::vector<OpRow> r;
    r.push_back({"Output level", 0, 99, [](Voice& v, int o) -> int& { return v.op[o].ol; }, nullptr});
    r.push_back({"Mode", 0, 1, [](Voice& v, int o) -> int& { return v.op[o].mode; },
                 [](int x) { return juce::String(x ? "Fixed Hz" : "Ratio"); }});
    r.push_back({"Coarse", 0, 31, [](Voice& v, int o) -> int& { return v.op[o].fc; }, nullptr});
    r.push_back({"Fine", 0, 99, [](Voice& v, int o) -> int& { return v.op[o].ff; }, nullptr});
    r.push_back({"Detune", 0, 14, [](Voice& v, int o) -> int& { return v.op[o].det; },
                 [](int x) { return juce::String(x - 7); }});
    for (int k = 0; k < 4; ++k)
        r.push_back({k == 0 ? "EG rate 1" : k == 1 ? "EG rate 2" : k == 2 ? "EG rate 3" : "EG rate 4", 0, 99,
                     [k](Voice& v, int o) -> int& { return v.op[o].rate[k]; }, nullptr});
    for (int k = 0; k < 4; ++k)
        r.push_back({k == 0 ? "EG level 1" : k == 1 ? "EG level 2" : k == 2 ? "EG level 3" : "EG level 4", 0, 99,
                     [k](Voice& v, int o) -> int& { return v.op[o].level[k]; }, nullptr});
    r.push_back({"Breakpoint", 0, 99, [](Voice& v, int o) -> int& { return v.op[o].bp; },
                 [](int x) { return noteName(21 + x); }});
    r.push_back({"Left depth", 0, 99, [](Voice& v, int o) -> int& { return v.op[o].ld; }, nullptr});
    r.push_back({"Right depth", 0, 99, [](Voice& v, int o) -> int& { return v.op[o].rd; }, nullptr});
    r.push_back({"Left curve", 0, 3, [](Voice& v, int o) -> int& { return v.op[o].lc; }, curve});
    r.push_back({"Right curve", 0, 3, [](Voice& v, int o) -> int& { return v.op[o].rc; }, curve});
    r.push_back({"Rate scaling", 0, 7, [](Voice& v, int o) -> int& { return v.op[o].rs; }, nullptr});
    r.push_back({"Amp mod sens", 0, 3, [](Voice& v, int o) -> int& { return v.op[o].ams; }, nullptr});
    r.push_back({"Velocity sens", 0, 7, [](Voice& v, int o) -> int& { return v.op[o].kvs; }, nullptr});
    return r;
}
} // namespace

Dx7Panel::Dx7Panel(ZirconProcessor& p) : proc_(p) {
    setSize(kWidth, kHeight);
    auto style = [&](juce::Component& c) { addAndMakeVisible(c); };

    // ---- global voice parameters
    algo_ = addCell("Algorithm", 0, 31, [](Voice& v) -> int& { return v.algo; },
                    [](int x) { return juce::String(x + 1); }, true);
    globals_[0] = algo_;
    globals_[1] = addCell("Feedback", 0, 7, [](Voice& v) -> int& { return v.fb; }, nullptr, true);
    globals_[2] = addCell("Transpose", 0, 48, [](Voice& v) -> int& { return v.trnsp; },
                          [](int x) { return noteName(36 + x); }, true);
    globals_[3] = addCell("Osc key sync", 0, 1, [](Voice& v) -> int& { return v.oks; },
                          [](int x) { return juce::String(x ? "On" : "Off"); }, true);
    lfo_[0] = addCell("LFO speed", 0, 99, [](Voice& v) -> int& { return v.lfs; }, nullptr, true);
    lfo_[1] = addCell("LFO delay", 0, 99, [](Voice& v) -> int& { return v.lfd; }, nullptr, true);
    lfo_[2] = addCell("LFO pitch depth", 0, 99, [](Voice& v) -> int& { return v.lpmd; }, nullptr, true);
    lfo_[3] = addCell("LFO amp depth", 0, 99, [](Voice& v) -> int& { return v.lamd; }, nullptr, true);
    lfo_[4] = addCell("LFO key sync", 0, 1, [](Voice& v) -> int& { return v.lks; },
                      [](int x) { return juce::String(x ? "On" : "Off"); }, true);
    lfo_[5] = addCell("LFO wave", 0, 5, [](Voice& v) -> int& { return v.lfw; },
                      [](int x) { static const char* n[6] = {"Triangle", "Saw down", "Saw up", "Square", "Sine", "S&H"};
                                  return juce::String(n[std::clamp(x, 0, 5)]); }, true);
    lfo_[6] = addCell("LFO pitch sens", 0, 7, [](Voice& v) -> int& { return v.lpms; }, nullptr, true);
    for (int k = 0; k < 4; ++k)
        peg_[k] = addCell("Pitch EG rate " + juce::String(k + 1), 0, 99, [k](Voice& v) -> int& { return v.prate[k]; }, nullptr, true);
    for (int k = 0; k < 4; ++k)
        peg_[4 + k] = addCell("Pitch EG level " + juce::String(k + 1), 0, 99, [k](Voice& v) -> int& { return v.plevel[k]; },
                              [](int x) { return juce::String(x - 50); }, true);

    // ---- operators
    auto rows = makeOpRows();
    for (int o = 0; o < 6; ++o)
        for (size_t r = 0; r < rows.size(); ++r) {
            auto ref = rows[r].ref;
            opCell_[o][r] = addCell(rows[r].name, rows[r].lo, rows[r].hi,
                                    [ref, o](Voice& v) -> int& { return ref(v, o); }, rows[r].fmt, false);
        }

    // ---- host-automatable macro knobs of the patch (Level / Bright / EG Speed / Vibrato)
    for (int i = 0; i < std::min(4, proc_.mappedParamCount()); ++i) {
        auto b = std::make_unique<HostBar>();
        b->slider.setSliderStyle(juce::Slider::LinearBar);
        b->slider.setRange(0.0, 1.0);
        b->slider.setColour(juce::Slider::trackColourId, cOk.withAlpha(0.35f));
        b->slider.setColour(juce::Slider::backgroundColourId, cPanel);
        b->slider.setColour(juce::Slider::textBoxTextColourId, cText);
        b->slider.setColour(juce::Slider::textBoxOutlineColourId, cEdge);
        const juce::String nm = proc_.mappedParamName(i);
        b->slider.textFromValueFunction = [nm](double v) { return nm + "  " + juce::String(juce::roundToInt(v * 100)) + "%"; };
        b->attach = std::make_unique<juce::SliderParameterAttachment>(*proc_.hostParam(i), b->slider);
        addAndMakeVisible(b->slider);
        hostBars_.push_back(std::move(b));
    }

    // ---- cartridge row
    cartLabel_.setText("CARTRIDGE", juce::dontSendNotification);
    cartLabel_.setColour(juce::Label::textColourId, cAccent);
    style(cartLabel_);
    style(voices_);
    voices_.onChange = [this] {
        const int i = voices_.getSelectedId() - 1;
        if (i >= 0) { proc_.selectCartVoice(i); refresh(); }
    };
    load_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load DX7 cartridge (.syx)", juce::File(), "*.syx");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (!f.existsAsFile()) return;
                                  auto e = proc_.loadSysex(f);
                                  status_.setText(e.isEmpty() ? "Loaded " + f.getFileName() : e, juce::dontSendNotification);
                                  status_.setColour(juce::Label::textColourId, e.isEmpty() ? cOk : cErr);
                                  refresh();
                              });
    };
    save_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Save DX7 cartridge (.syx)",
                                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                           .getChildFile("cartridge.syx"), "*.syx");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  auto e = proc_.saveSysex(f.withFileExtension("syx"));
                                  status_.setText(e.isEmpty() ? "Saved " + f.getFileName() : e, juce::dontSendNotification);
                                  status_.setColour(juce::Label::textColourId, e.isEmpty() ? cOk : cErr);
                              });
    };
    init_.onClick = [this] { proc_.initDx7Voice(); refresh(); };
    for (auto* b : {&load_, &save_, &init_}) style(*b);
    name_.setInputRestrictions(10);
    name_.setColour(juce::TextEditor::backgroundColourId, cPanel);
    name_.setColour(juce::TextEditor::textColourId, cText);
    name_.setColour(juce::TextEditor::outlineColourId, cEdge);
    auto commitName = [this] {
        v_.name = name_.getText().toStdString();
        v_.name.resize(10, ' ');
        proc_.setDx7Voice(v_);
        rebuildVoiceList();
    };
    name_.onReturnKey = commitName;
    name_.onFocusLost = commitName;
    style(name_);
    status_.setColour(juce::Label::textColourId, cDim);
    status_.setFont(juce::FontOptions(12.f));
    style(status_);

    proc_.onCartChanged = [this] { refresh(); };
    refresh();
}

Dx7Panel::~Dx7Panel() { proc_.onCartChanged = nullptr; }

Dx7Panel::Cell* Dx7Panel::addCell(const juce::String& name, int lo, int hi, std::function<int&(Voice&)> ref,
                                  std::function<juce::String(int)> fmt, bool showName) {
    auto c = std::make_unique<Cell>();
    c->ref = std::move(ref);
    c->setSliderStyle(juce::Slider::LinearBar);
    c->setRange(lo, hi, 1.0);
    c->setTextBoxIsEditable(false);
    c->setColour(juce::Slider::trackColourId, cAccent.withAlpha(0.40f));
    c->setColour(juce::Slider::backgroundColourId, cPanel);
    c->setColour(juce::Slider::textBoxTextColourId, cText);
    c->setColour(juce::Slider::textBoxOutlineColourId, cEdge);
    c->setMouseDragSensitivity(250);
    c->textFromValueFunction = [name, fmt, showName](double v) {
        const int i = (int)std::lround(v);
        const juce::String s = fmt ? fmt(i) : juce::String(i);
        return showName ? name + "   " + s : s;
    };
    Cell* raw = c.get();
    c->onValueChange = [this, raw] { edit(*raw); };
    addAndMakeVisible(*c);
    cells_.push_back(std::move(c));
    return raw;
}

void Dx7Panel::edit(Cell& c) {
    c.ref(v_) = (int)std::lround(c.getValue());
    proc_.setDx7Voice(v_);
    repaint();
}

void Dx7Panel::rebuildVoiceList() {
    voices_.clear(juce::dontSendNotification);
    for (int i = 0; i < proc_.cartSize(); ++i) voices_.addItem(proc_.cartName(i), i + 1);
    voices_.setSelectedId(proc_.cartIndex() + 1, juce::dontSendNotification);
}

void Dx7Panel::refresh() {
    v_ = proc_.dx7Voice();
    for (auto& c : cells_) c->setValue(c->ref(v_), juce::dontSendNotification);
    name_.setText(juce::String(v_.name).trimEnd(), juce::dontSendNotification);
    rebuildVoiceList();
    repaint();
}

void Dx7Panel::resized() {
    int x = 12;
    cartLabel_.setBounds(x, kRow1Y, 90, 26);
    voices_.setBounds(100, kRow1Y, 250, 26);
    load_.setBounds(358, kRow1Y, 92, 26);
    save_.setBounds(456, kRow1Y, 92, 26);
    init_.setBounds(554, kRow1Y, 92, 26);
    name_.setBounds(656, kRow1Y, 130, 26);
    status_.setBounds(794, kRow1Y, 220, 26);

    auto place = [&](Cell* c, int col, int row) { c->setBounds(12 + col * 175, kGlobalY + row * kPitch, 168, kCellH); };
    for (int i = 0; i < 4; ++i) place(globals_[i], 0, i);
    for (int i = 0; i < (int)hostBars_.size(); ++i)
        hostBars_[(size_t)i]->slider.setBounds(12, kGlobalY + (4 + i) * kPitch, 168, kCellH);
    for (int i = 0; i < 7; ++i) place(lfo_[i], 1, i);
    for (int i = 0; i < 8; ++i) place(peg_[i], 2, i);

    for (int o = 0; o < 6; ++o)
        for (int r = 0; r < 21; ++r)
            opCell_[o][r]->setBounds(kOpX0 + o * kOpW, kOpRowsY + r * kOpPitch, kOpW - 6, kCellH - 2);
}

void Dx7Panel::drawAlgorithm(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setColour(cPanel);
    g.fillRoundedRectangle(area, 8.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(area, 8.f, 1.f);
    const auto& A = zircon::dx7::algorithm(v_.algo);

    int depth[6];
    std::function<int(int, int)> dep = [&](int j, int guard) -> int {
        if (guard > 7) return 0;
        int d = 0;
        for (int i = 0; i < 6; ++i)
            if (i != j && (A.mods[i] & (1 << j))) d = std::max(d, 1 + dep(i, guard + 1));
        return d;
    };
    int maxd = 0;
    for (int j = 0; j < 6; ++j) { depth[j] = dep(j, 0); maxd = std::max(maxd, depth[j]); }

    const float box = 28.f;
    auto inner = area.reduced(14.f, 18.f);
    const float pitch = maxd > 0 ? (inner.getHeight() - box - 14.f) / (float)maxd : 0.f;
    juce::Point<float> pos[6];
    for (int d = 0; d <= maxd; ++d) {
        std::vector<int> row;
        for (int j = 0; j < 6; ++j) if (depth[j] == d) row.push_back(j);
        for (size_t k = 0; k < row.size(); ++k) {
            const float x = inner.getX() + inner.getWidth() * (float)(k + 1) / (float)(row.size() + 1);
            const float y = inner.getBottom() - box - 14.f - (float)d * pitch + box / 2;
            pos[row[k]] = {x, y};
        }
    }
    g.setColour(cDim);
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            if (A.mods[i] & (1 << j))
                g.drawLine(pos[j].x, pos[j].y + box / 2, pos[i].x, pos[i].y - box / 2, 1.5f);
    for (int i = 0; i < 6; ++i)
        if (A.carriers & (1 << i)) {
            g.setColour(cAccent);
            g.drawLine(pos[i].x, pos[i].y + box / 2, pos[i].x, inner.getBottom() - 4.f, 1.5f);
        }
    g.setColour(cAccent);
    g.drawLine(inner.getX(), inner.getBottom() - 4.f, inner.getRight(), inner.getBottom() - 4.f, 1.5f);

    // feedback
    if (A.fbSrc >= 0 && A.fbDst >= 0) {
        g.setColour(cOk);
        juce::Path p;
        auto s = pos[A.fbSrc], d = pos[A.fbDst];
        const float xr = std::max(s.x, d.x) + box / 2 + 8.f;
        p.startNewSubPath(s.x + box / 2, s.y);
        p.lineTo(xr, s.y);
        p.lineTo(xr, d.y - box / 2 - 6.f);
        p.lineTo(d.x, d.y - box / 2 - 6.f);
        p.lineTo(d.x, d.y - box / 2);
        if (A.fbSrc == A.fbDst) {   // self-loop: small box-hugging path
            p.clear();
            p.startNewSubPath(s.x + box / 2, s.y + 4.f);
            p.lineTo(s.x + box / 2 + 9.f, s.y + 4.f);
            p.lineTo(s.x + box / 2 + 9.f, s.y - box / 2 - 7.f);
            p.lineTo(s.x, s.y - box / 2 - 7.f);
            p.lineTo(s.x, s.y - box / 2);
        }
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }
    for (int i = 0; i < 6; ++i) {
        auto r = juce::Rectangle<float>(box, box).withCentre(pos[i]);
        const bool car = A.carriers & (1 << i);
        g.setColour(car ? cAccent : cBg);
        g.fillRoundedRectangle(r, 4.f);
        g.setColour(car ? cAccent : cDim);
        g.drawRoundedRectangle(r, 4.f, 1.5f);
        g.setColour(car ? cBg : cText);
        g.setFont(juce::FontOptions(14.f, juce::Font::bold));
        g.drawText(juce::String(i + 1), r, juce::Justification::centred);
    }
    g.setColour(cDim);
    g.setFont(juce::FontOptions(12.f));
    g.drawText("Algorithm " + juce::String(v_.algo + 1) + "   (orange = carrier, green = feedback)",
               area.withHeight(16.f).translated(0, 2.f), juce::Justification::centred);
}

void Dx7Panel::drawEnvelope(juce::Graphics& g, juce::Rectangle<float> r, const int* rate, const int* level) {
    g.setColour(cPanel);
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(r, 4.f, 1.f);
    auto in = r.reduced(4.f);
    float w[5];
    for (int k = 0; k < 3; ++k) w[k] = 0.10f + 0.90f * float(99 - rate[k]) / 99.f;
    w[3] = 0.6f;
    w[4] = 0.10f + 0.90f * float(99 - rate[3]) / 99.f;
    const float total = w[0] + w[1] + w[2] + w[3] + w[4];
    auto Y = [&](int l) { return in.getBottom() - in.getHeight() * (float)l / 99.f; };
    juce::Path p;
    float x = in.getX();
    p.startNewSubPath(x, Y(level[3]));
    const int lv[5] = {level[0], level[1], level[2], level[2], level[3]};
    for (int k = 0; k < 5; ++k) {
        x += in.getWidth() * w[k] / total;
        p.lineTo(x, Y(lv[k]));
    }
    g.setColour(cAccent);
    g.strokePath(p, juce::PathStrokeType(1.6f));
}

void Dx7Panel::paint(juce::Graphics& g) {
    g.setFont(juce::FontOptions(12.f));
    g.setColour(cDim);

    drawAlgorithm(g, {548.f, (float)kGlobalY, 460.f, 192.f});

    // operator headers + envelope previews
    for (int o = 0; o < 6; ++o) {
        const float x = (float)(kOpX0 + o * kOpW);
        const bool on = v_.op[o].ol > 0;
        g.setColour(zircon::dx7::algorithm(v_.algo).carriers & (1 << o) ? cAccent : cText);
        g.setFont(juce::FontOptions(14.f, juce::Font::bold));
        g.drawText("OP " + juce::String(o + 1) +
                       (zircon::dx7::algorithm(v_.algo).carriers & (1 << o) ? "  (carrier)" : "  (mod)") + (on ? "" : "  off"),
                   juce::Rectangle<float>(x, (float)kOpHeadY, (float)kOpW - 6.f, 20.f), juce::Justification::centredLeft);
        drawEnvelope(g, {x, (float)kEgY, (float)kOpW - 6.f, 54.f}, v_.op[o].rate, v_.op[o].level);
    }

    // row labels
    static const char* labels[21] = {"Output level", "Mode", "Coarse", "Fine", "Detune", "EG rate 1", "EG rate 2",
                                     "EG rate 3", "EG rate 4", "EG level 1", "EG level 2", "EG level 3", "EG level 4",
                                     "Breakpoint", "Left depth", "Right depth", "Left curve", "Right curve",
                                     "Rate scaling", "Amp mod sens", "Velocity sens"};
    g.setColour(cDim);
    g.setFont(juce::FontOptions(12.f));
    for (int r = 0; r < 21; ++r)
        g.drawText(labels[r], juce::Rectangle<int>(10, kOpRowsY + r * kOpPitch, kLabelW, kCellH - 2),
                   juce::Justification::centredLeft);
}
