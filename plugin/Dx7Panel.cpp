#include "Dx7Panel.h"
#include <algorithm>
#include <cmath>

namespace {
using zircon::dx7::Voice;

const juce::Colour kPlateTop{0xff232326}, kPlateBot{0xff0d0d0f}, kEdge{0xff39393f},
    kText{0xffe9e6dc}, kDim{0xff9a978c}, kOrange{0xffe8761f}, kRed{0xffd23b2a}, kPlum{0xff7f3b8a},
    kLcdHi{0xffc6d293}, kLcdLo{0xffa5b476}, kLcdInk{0xff262d14}, kOk{0xff6fd49a}, kErr{0xffff6a6a};

constexpr int kContentY = 196;
constexpr int kContentH = 444;

juce::String noteName(int midi) { return juce::MidiMessage::getMidiNoteName(midi, true, true, 3); }
juce::Font mono(float h, bool bold = true) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font sans(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }

void groupTitle(juce::Graphics& g, const juce::String& t, int x, int y, int w) {
    g.setColour(kDim);
    g.setFont(sans(11.5f, true));
    const int tw = (int)sans(11.5f, true).getStringWidthFloat(t.toUpperCase()) + 12;
    g.drawText(t.toUpperCase(), x, y, tw, 14, juce::Justification::centredLeft);
    g.setColour(kEdge.brighter(0.3f));
    g.drawHorizontalLine(y + 7, (float)(x + tw), (float)(x + w));
}
} // namespace

// ============================================================================ look
Dx7Look::Dx7Look() {
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d1f12));
    setColour(juce::ComboBox::textColourId, kLcdHi);
    setColour(juce::ComboBox::outlineColourId, kEdge);
    setColour(juce::ComboBox::arrowColourId, kLcdHi);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1d1f12));
    setColour(juce::PopupMenu::textColourId, kLcdHi);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kOrange.withAlpha(0.5f));
    setColour(juce::Slider::textBoxTextColourId, kLcdHi);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::textColourId, kLcdInk);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::highlightColourId, kLcdInk.withAlpha(0.25f));
    setColour(juce::CaretComponent::caretColourId, kLcdInk);
}

juce::Label* Dx7Look::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(sans(12.5f, true));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, kLcdHi);
    return l;
}

void Dx7Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
    const float d = std::min(b.getWidth(), b.getHeight()) - 20.f;
    auto c = b.withSizeKeepingCentre(d, d);
    const auto ctr = c.getCentre();
    const float ang = a0 + pos * (a1 - a0);

    for (int i = 0; i <= 10; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / 10.f;
        const bool major = (i % 5) == 0;
        g.setColour(kDim.withAlpha(major ? 0.95f : 0.6f));
        g.drawLine({ctr.getPointOnCircumference(d / 2 + 5.f, a), ctr.getPointOnCircumference(d / 2 + (major ? 11.f : 8.f), a)},
                   major ? 1.8f : 1.1f);
    }
    juce::Path arc;
    arc.addCentredArc(ctr.x, ctr.y, d / 2 + 2.5f, d / 2 + 2.5f, 0.f, a0, ang, true);
    g.setColour(kOrange.withAlpha(0.9f));
    g.strokePath(arc, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(c.translated(0.f, 3.f).expanded(2.f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff595b62), c.getX() + d * 0.3f, c.getY(),
                                           juce::Colour(0xff0b0b0d), c.getRight(), c.getBottom(), false));
    g.fillEllipse(c);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    for (int i = 0; i < 36; ++i) {
        const float a = juce::MathConstants<float>::twoPi * (float)i / 36.f;
        g.drawLine({ctr.getPointOnCircumference(d / 2 - 1.f, a), ctr.getPointOnCircumference(d / 2 - 4.f, a)}, 1.f);
    }
    auto top = c.reduced(d * 0.14f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34353a), top.getX(), top.getY(),
                                           juce::Colour(0xff121214), top.getRight(), top.getBottom(), false));
    g.fillEllipse(top);
    g.setColour(juce::Colours::white.withAlpha(0.13f));
    g.drawEllipse(top, 1.f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.20f), top.getX() + 4, top.getY() + 2,
                                           juce::Colours::transparentWhite, top.getCentreX(), top.getCentreY(), true));
    g.fillEllipse(top.reduced(2.f));
    g.setColour(juce::Colour(0xfff4f1e6));
    g.drawLine({ctr.getPointOnCircumference(d * 0.10f, ang), ctr.getPointOnCircumference(d * 0.40f, ang)}, 3.f);
}

void Dx7Look::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                               juce::Slider::SliderStyle, juce::Slider&) {
    const float cx = (float)x + (float)w * 0.5f;
    const float top = (float)y + 22.f, bot = (float)(y + h) - 22.f;
    for (int i = 0; i <= 10; ++i) {
        const float ty = top + (bot - top) * (float)i / 10.f;
        const bool major = (i % 5) == 0;
        g.setColour(kDim.withAlpha(0.7f));
        g.drawHorizontalLine((int)ty, cx - (major ? 14.f : 10.f), cx - 6.f);
        g.drawHorizontalLine((int)ty, cx + 6.f, cx + (major ? 14.f : 10.f));
    }
    auto groove = juce::Rectangle<float>(cx - 3.f, top - 4.f, 6.f, bot - top + 8.f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(groove, 3.f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(groove.expanded(0.5f), 3.f, 1.f);
    g.setColour(kOrange.withAlpha(0.8f));
    g.fillRoundedRectangle(juce::Rectangle<float>(cx - 1.5f, pos, 3.f, std::max(0.f, bot + 4.f - pos)), 1.5f);
    auto cap = juce::Rectangle<float>(26.f, 36.f).withCentre({cx, pos});
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(cap.translated(0, 2.5f).expanded(1.f), 3.f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff92949a), cap.getX(), cap.getY(),
                                           juce::Colour(0xff3f4045), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle(cap, 3.f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawHorizontalLine((int)cap.getY() + 1, cap.getX() + 3, cap.getRight() - 3);
    for (int i = -2; i <= 2; ++i) {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawHorizontalLine((int)(cap.getCentreY() + (float)i * 4.f), cap.getX() + 3, cap.getRight() - 3);
    }
    g.setColour(juce::Colour(0xfff4f1e6));
    g.fillRect(juce::Rectangle<float>(cap.getX() + 3, cap.getCentreY() - 1.f, cap.getWidth() - 6, 2.f));
}

void Dx7Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    const bool on = b.getToggleState();
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0, 1.5f), 4.f);
    juce::Colour a = on ? juce::Colour(0xffe9e5d6) : juce::Colour(0xffb4b1a4);
    juce::Colour c = on ? juce::Colour(0xffc5c0ae) : juce::Colour(0xff7f7d73);
    if (down) { a = a.darker(0.2f); c = c.darker(0.2f); }
    else if (over) { a = a.brighter(0.08f); c = c.brighter(0.08f); }
    g.setGradientFill(juce::ColourGradient(a, r.getX(), r.getY(), c, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 4, r.getRight() - 4);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(r, 4.f, 1.f);
    if (b.getProperties().contains("led") || b.getProperties().contains("nav")) {
        auto led = juce::Rectangle<float>(7.f, 7.f).withCentre({r.getX() + 11.f, r.getCentreY()});
        g.setColour(on ? kOrange : juce::Colour(0xff3a2a20));
        g.fillEllipse(led);
        if (on) {
            g.setColour(kOrange.withAlpha(0.35f));
            g.fillEllipse(led.expanded(3.f));
        }
    }
}

void Dx7Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(juce::Colour(0xff1f1e1a));
    g.setFont(sans(11.5f, true));
    auto r = b.getLocalBounds();
    if (b.getProperties().contains("led") || b.getProperties().contains("nav")) r = r.withTrimmedLeft(16);
    g.drawText(b.getButtonText().toUpperCase(), r, juce::Justification::centred);
}

// ============================================================================ panel
Dx7Panel::Dx7Panel(ZirconProcessor& p) : proc_(p) {
    setLookAndFeel(&look_);

    using Row = std::function<int&(Voice&, int)>;
    auto op = [this](Row r) { return std::function<int&(Voice&)>([this, r](Voice& v) -> int& { return r(v, curOp_); }); };
    auto curve = [](int v) { static const char* n[4] = {"-LIN", "-EXP", "+EXP", "+LIN"}; return juce::String(n[v & 3]); };

    // ---------------- MASTER / VOICE
    knob("Algorithm", 0, 31, [](Voice& v) -> int& { return v.algo; }, [](int x) { return juce::String(x + 1); }, 0, 0, 585, 30);
    knob("Feedback", 0, 7, [](Voice& v) -> int& { return v.fb; }, nullptr, 0, 0, 705, 30);
    knob("Transpose", 0, 48, [](Voice& v) -> int& { return v.trnsp; }, [](int x) { return noteName(36 + x); }, 0, 0, 825, 30);
    toggle("OSC SYNC OFF", "OSC SYNC ON", [](Voice& v) -> int& { return v.oks; }, 0, 0, 585, 200);

    // ---------------- MASTER / LFO + PITCH EG
    knob("Speed", 0, 99, [](Voice& v) -> int& { return v.lfs; }, nullptr, 0, 1, 24, 30);
    knob("Delay", 0, 99, [](Voice& v) -> int& { return v.lfd; }, nullptr, 0, 1, 116, 30);
    knob("Pitch depth", 0, 99, [](Voice& v) -> int& { return v.lpmd; }, nullptr, 0, 1, 208, 30);
    knob("Amp depth", 0, 99, [](Voice& v) -> int& { return v.lamd; }, nullptr, 0, 1, 300, 30);
    knob("Wave", 0, 5, [](Voice& v) -> int& { return v.lfw; },
         [](int x) { static const char* n[6] = {"TRI", "SAW DN", "SAW UP", "SQUARE", "SINE", "S&H"}; return juce::String(n[std::clamp(x, 0, 5)]); },
         0, 1, 392, 30);
    knob("Pitch sens", 0, 7, [](Voice& v) -> int& { return v.lpms; }, nullptr, 0, 1, 484, 30);
    toggle("LFO SYNC OFF", "LFO SYNC ON", [](Voice& v) -> int& { return v.lks; }, 0, 1, 24, 180);
    for (int k = 0; k < 4; ++k)
        fader("R" + juce::String(k + 1), 0, 99, [k](Voice& v) -> int& { return v.prate[k]; }, nullptr, 0, 1, 612 + k * 46, 36);
    for (int k = 0; k < 4; ++k)
        fader("L" + juce::String(k + 1), 0, 99, [k](Voice& v) -> int& { return v.plevel[k]; },
              [](int x) { return juce::String(x - 50); }, 0, 1, 612 + 4 * 46 + 14 + k * 46, 36);

    // ---------------- OPERATORS / TUNE
    knob("Output level", 0, 99, op([](Voice& v, int o) -> int& { return v.op[o].ol; }), nullptr, 1, 0, 24, 30);
    knob("Coarse", 0, 31, op([](Voice& v, int o) -> int& { return v.op[o].fc; }), nullptr, 1, 0, 134, 30);
    knob("Fine", 0, 99, op([](Voice& v, int o) -> int& { return v.op[o].ff; }), nullptr, 1, 0, 244, 30);
    knob("Detune", 0, 14, op([](Voice& v, int o) -> int& { return v.op[o].det; }), [](int x) { return juce::String(x - 7); }, 1, 0, 354, 30);
    toggle("MODE: RATIO", "MODE: FIXED", op([](Voice& v, int o) -> int& { return v.op[o].mode; }), 1, 0, 24, 215);
    knob("Velocity sens", 0, 7, op([](Voice& v, int o) -> int& { return v.op[o].kvs; }), nullptr, 1, 0, 200, 190);
    knob("Amp mod sens", 0, 3, op([](Voice& v, int o) -> int& { return v.op[o].ams; }), nullptr, 1, 0, 310, 190);

    // ---------------- OPERATORS / ENVELOPE
    for (int k = 0; k < 4; ++k)
        fader("R" + juce::String(k + 1), 0, 99, op([k](Voice& v, int o) -> int& { return v.op[o].rate[k]; }), nullptr, 1, 1, 24 + k * 56, 36);
    for (int k = 0; k < 4; ++k)
        fader("L" + juce::String(k + 1), 0, 99, op([k](Voice& v, int o) -> int& { return v.op[o].level[k]; }), nullptr, 1, 1, 24 + 4 * 56 + 24 + k * 56, 36);
    knob("Rate scaling", 0, 7, op([](Voice& v, int o) -> int& { return v.op[o].rs; }), nullptr, 1, 1, 520, 290);

    // ---------------- OPERATORS / SCALING
    knob("Breakpoint", 0, 99, op([](Voice& v, int o) -> int& { return v.op[o].bp; }), [](int x) { return noteName(21 + x); }, 1, 2, 24, 30);
    knob("Left depth", 0, 99, op([](Voice& v, int o) -> int& { return v.op[o].ld; }), nullptr, 1, 2, 134, 30);
    knob("Right depth", 0, 99, op([](Voice& v, int o) -> int& { return v.op[o].rd; }), nullptr, 1, 2, 244, 30);
    knob("Left curve", 0, 3, op([](Voice& v, int o) -> int& { return v.op[o].lc; }), curve, 1, 2, 354, 30);
    knob("Right curve", 0, 3, op([](Voice& v, int o) -> int& { return v.op[o].rc; }), curve, 1, 2, 464, 30);

    // ---------------- MASTER / FX + OUTPUT (host-automatable)
    for (int i = 0; i < std::min(4, proc_.mappedParamCount()); ++i)
        hostKnob(proc_.hostParam(i), proc_.mappedParamName(i), 24 + i * 110, 30);
    static const char* fxn[9] = {"Time", "Feedback", "Mix", "Size", "Damping", "Mix", "Rate", "Depth", "Mix"};
    for (int i = 0; i < 3; ++i) hostKnob(proc_.fxParam(i), fxn[i], 24 + i * 100, 215);
    HostCtl* chorusRate = nullptr;
    for (int i = 0; i < 3; ++i) {
        auto* k = hostKnob(proc_.fxParam(6 + i), fxn[6 + i], 340 + i * 100, 215);
        if (i == 0) chorusRate = k;
    }
    for (int i = 0; i < 3; ++i) hostKnob(proc_.fxParam(3 + i), fxn[3 + i], 656 + i * 100, 215);
    chorusRate->textFromValueFunction = [](double v) { return juce::String(0.1 * std::pow(50.0, v), 2) + " Hz"; };
    chorusRate->updateText();

    // ---------------- header: nav + cartridge
    for (int i = 0; i < 7; ++i) {
        auto& b = topBtn_[i];
        b.setButtonText(i == 0 ? "MASTER" : "OP " + juce::String(i));
        b.setClickingTogglesState(true);
        b.setRadioGroupId(101);
        b.getProperties().set("nav", true);
        b.onClick = [this, i] { curTop_ = i; curOp_ = std::max(0, i - 1); showPage(); };
        addAndMakeVisible(b);
    }
    topBtn_[0].setToggleState(true, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i) {
        auto& b = subBtn_[i];
        b.setClickingTogglesState(true);
        b.setRadioGroupId(102);
        b.getProperties().set("nav", true);
        b.onClick = [this, i] { curSub_ = i; showPage(); };
        addAndMakeVisible(b);
    }
    subBtn_[0].setToggleState(true, juce::dontSendNotification);

    addAndMakeVisible(voices_);
    voices_.onChange = [this] {
        const int i = voices_.getSelectedId() - 1;
        if (i >= 0) { proc_.selectCartVoice(i); refresh(); }
    };
    prev_.onClick = [this] {
        const int n = proc_.cartSize();
        if (n > 0) { proc_.selectCartVoice((proc_.cartIndex() + n - 1) % n); refresh(); }
    };
    next_.onClick = [this] {
        const int n = proc_.cartSize();
        if (n > 0) { proc_.selectCartVoice((proc_.cartIndex() + 1) % n); refresh(); }
    };
    load_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load DX7 cartridge (.syx)", juce::File(), "*.syx");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (!f.existsAsFile()) return;
                                  auto e = proc_.loadSysex(f);
                                  status_.setText(e.isEmpty() ? "Loaded " + f.getFileName() : e, juce::dontSendNotification);
                                  status_.setColour(juce::Label::textColourId, e.isEmpty() ? kOk : kErr);
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
                                  status_.setColour(juce::Label::textColourId, e.isEmpty() ? kOk : kErr);
                              });
    };
    init_.onClick = [this] { proc_.initDx7Voice(); refresh(); };
    factory_.onClick = [this] { proc_.restoreFactoryCart(); refresh(); };
    for (auto* b : {&prev_, &next_, &load_, &save_, &init_, &factory_}) addAndMakeVisible(*b);

    name_.setInputRestrictions(10);
    name_.setFont(mono(26.f));
    name_.setIndents(2, 0);
    auto commitName = [this] {
        v_.name = name_.getText().toStdString();
        v_.name.resize(10, ' ');
        proc_.setDx7Voice(v_);
        rebuildVoiceList();
        repaint();
    };
    name_.onReturnKey = commitName;
    name_.onFocusLost = commitName;
    addAndMakeVisible(name_);
    status_.setFont(sans(11.5f));
    status_.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(status_);

    proc_.onCartChanged = [this] { refresh(); };
    built_ = true;
    setSize(kWidth, kHeight);
    refresh();
}

Dx7Panel::~Dx7Panel() {
    proc_.onCartChanged = nullptr;
    setLookAndFeel(nullptr);
}

Dx7Panel::Ctl* Dx7Panel::knob(const juce::String& name, int lo, int hi, std::function<int&(Voice&)> ref,
                              std::function<juce::String(int)> fmt, int top, int sub, int x, int y) {
    auto c = std::make_unique<Ctl>();
    c->ref = std::move(ref);
    c->name = name;
    c->top = top; c->sub = sub;
    c->cell = {x, kContentY + y, 100, 128};
    c->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    c->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    c->setTextBoxIsEditable(false);
    c->setRange(lo, hi, 1.0);
    c->setMouseDragSensitivity(220);
    c->textFromValueFunction = [fmt](double v) { const int i = (int)std::lround(v); return fmt ? fmt(i) : juce::String(i); };
    c->setBounds(c->cell.getX() + 5, c->cell.getY() + 16, 90, 112);
    Ctl* raw = c.get();
    c->onValueChange = [this, raw] { edit(*raw); };
    addChildComponent(*c);
    ctls_.push_back(std::move(c));
    return raw;
}

Dx7Panel::Ctl* Dx7Panel::fader(const juce::String& name, int lo, int hi, std::function<int&(Voice&)> ref,
                               std::function<juce::String(int)> fmt, int top, int sub, int x, int y) {
    auto c = std::make_unique<Ctl>();
    c->ref = std::move(ref);
    c->name = name;
    c->top = top; c->sub = sub;
    c->cell = {x, kContentY + y, 46, 300};
    c->setSliderStyle(juce::Slider::LinearVertical);
    c->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 18);
    c->setTextBoxIsEditable(false);
    c->setRange(lo, hi, 1.0);
    c->setMouseDragSensitivity(200);
    c->textFromValueFunction = [fmt](double v) { const int i = (int)std::lround(v); return fmt ? fmt(i) : juce::String(i); };
    c->setBounds(c->cell.getX(), c->cell.getY() + 16, 46, 284);
    Ctl* raw = c.get();
    c->onValueChange = [this, raw] { edit(*raw); };
    addChildComponent(*c);
    ctls_.push_back(std::move(c));
    return raw;
}

Dx7Panel::Sw* Dx7Panel::toggle(const juce::String& offText, const juce::String& onText, std::function<int&(Voice&)> ref,
                               int top, int sub, int x, int y) {
    auto s = std::make_unique<Sw>();
    s->ref = std::move(ref);
    s->offText = offText; s->onText = onText;
    s->top = top; s->sub = sub;
    s->setClickingTogglesState(true);
    s->getProperties().set("led", true);
    s->setButtonText(offText);
    s->setBounds(x, kContentY + y, 150, 30);
    Sw* raw = s.get();
    s->onClick = [this, raw] {
        raw->ref(v_) = raw->getToggleState() ? 1 : 0;
        raw->setButtonText(raw->getToggleState() ? raw->onText : raw->offText);
        proc_.setDx7Voice(v_);
        repaint();
    };
    addChildComponent(*s);
    sws_.push_back(std::move(s));
    return raw;
}

Dx7Panel::HostCtl* Dx7Panel::hostKnob(juce::RangedAudioParameter* p, const juce::String& name, int x, int y) {
    auto h = std::make_unique<HostCtl>();
    h->name = name;
    h->sub = 2;
    h->cell = {x, kContentY + y, 100, 128};
    h->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    h->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    h->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    h->setTextBoxIsEditable(false);
    h->setMouseDragSensitivity(220);
    h->attach = std::make_unique<juce::SliderParameterAttachment>(*p, *h);
    h->textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; };
    h->updateText();
    h->setBounds(h->cell.getX() + 5, h->cell.getY() + 16, 90, 112);
    addChildComponent(*h);
    hosts_.push_back(std::move(h));
    return hosts_.back().get();
}

void Dx7Panel::edit(Ctl& c) {
    c.ref(v_) = (int)std::lround(c.getValue());
    proc_.setDx7Voice(v_);
    repaint();
}

void Dx7Panel::showPage() {
    static const char* masterSubs[3] = {"VOICE / ALGORITHM", "LFO / PITCH EG", "FX / OUTPUT"};
    static const char* opSubs[3] = {"TUNE", "ENVELOPE", "SCALING"};
    for (int i = 0; i < 3; ++i) subBtn_[i].setButtonText(curTop_ == 0 ? masterSubs[i] : opSubs[i]);
    subBtn_[curSub_].setToggleState(true, juce::dontSendNotification);
    const int top = curTop_ > 0 ? 1 : 0;
    for (auto& c : ctls_) c->setVisible(c->top == top && c->sub == curSub_);
    for (auto& s : sws_) s->setVisible(s->top == top && s->sub == curSub_);
    for (auto& h : hosts_) h->setVisible(top == 0 && curSub_ == 2);
    for (auto& c : ctls_) { c->setValue(c->ref(v_), juce::dontSendNotification); c->updateText(); }
    for (auto& s : sws_) {
        s->setToggleState(s->ref(v_) != 0, juce::dontSendNotification);
        s->setButtonText(s->getToggleState() ? s->onText : s->offText);
    }
    repaint();
}

void Dx7Panel::rebuildVoiceList() {
    voices_.clear(juce::dontSendNotification);
    for (int i = 0; i < proc_.cartSize(); ++i) voices_.addItem(proc_.cartName(i), i + 1);
    voices_.setSelectedId(proc_.cartIndex() + 1, juce::dontSendNotification);
}

void Dx7Panel::refresh() {
    v_ = proc_.dx7Voice();
    name_.setText(juce::String(v_.name).trimEnd(), juce::dontSendNotification);
    rebuildVoiceList();
    showPage();
}

void Dx7Panel::resized() {
    if (!built_) return;
    name_.setBounds(350, 18, 340, 34);
    prev_.setBounds(716, 14, 38, 28);
    next_.setBounds(758, 14, 38, 28);
    voices_.setBounds(802, 14, 186, 28);
    load_.setBounds(716, 50, 64, 28);
    save_.setBounds(784, 50, 64, 28);
    init_.setBounds(852, 50, 52, 28);
    factory_.setBounds(908, 50, 80, 28);
    status_.setBounds(716, 82, 272, 18);
    for (int i = 0; i < 7; ++i) topBtn_[i].setBounds(24 + i * 138, 116, 132, 30);
    for (int i = 0; i < 3; ++i) subBtn_[i].setBounds(24 + i * 184, 154, 178, 28);
}

juce::String Dx7Panel::frequencyText() const {
    const auto& o = v_.op[std::clamp(curOp_, 0, 5)];
    if (o.mode == 0) {
        const double r = (o.fc == 0 ? 0.5 : (double)o.fc) * (1.0 + o.ff / 100.0);
        return "RATIO " + juce::String(r, 2);
    }
    const double hz = std::pow(10.0, (double)(o.fc & 3) + o.ff / 100.0);
    return "FIXED " + juce::String(hz, 1) + "Hz";
}

void Dx7Panel::drawLcd(juce::Graphics& g) {
    auto r = juce::Rectangle<float>(340.f, 12.f, 360.f, 90.f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r.expanded(4.f), 6.f);
    g.setGradientFill(juce::ColourGradient(kLcdHi, r.getX(), r.getY(), kLcdLo, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(juce::Colours::black.withAlpha(0.06f));
    for (float yy = r.getY(); yy < r.getBottom(); yy += 3.f) g.drawHorizontalLine((int)yy, r.getX(), r.getRight());
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawRoundedRectangle(r, 3.f, 1.5f);

    const auto& A = zircon::dx7::algorithm(v_.algo);
    g.setColour(kLcdInk);
    g.setFont(mono(14.f));
    g.drawText("VOICE " + juce::String(proc_.cartIndex() + 1).paddedLeft('0', 2) + "/" +
                   juce::String(proc_.cartSize()).paddedLeft('0', 2) + "  ALG " + juce::String(v_.algo + 1).paddedLeft('0', 2) +
                   "  FB " + juce::String(v_.fb),
               350, 54, 340, 18, juce::Justification::centredLeft);
    juce::String page;
    if (curTop_ == 0) {
        static const char* n[3] = {"MASTER  VOICE", "MASTER  LFO/PEG", "MASTER  FX"};
        page = n[curSub_];
    } else {
        static const char* n[3] = {"TUNE", "ENVELOPE", "SCALING"};
        page = "OP" + juce::String(curTop_) + (A.carriers & (1 << (curTop_ - 1)) ? " CAR " : " MOD ") + n[curSub_];
        if (curSub_ == 0) page += " " + frequencyText();
    }
    g.drawText(page, 350, 74, 340, 18, juce::Justification::centredLeft);
}

void Dx7Panel::drawAlgorithm(juce::Graphics& g, juce::Rectangle<float> area, int hl) {
    g.setColour(juce::Colour(0xff0d0e10));
    g.fillRoundedRectangle(area, 8.f);
    g.setColour(kEdge);
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

    const float box = area.getHeight() > 300.f ? 38.f : 30.f;
    auto inner = area.reduced(26.f, 30.f);
    const float pitch = maxd > 0 ? (inner.getHeight() - box - 16.f) / (float)maxd : 0.f;
    juce::Point<float> pos[6];
    for (int d = 0; d <= maxd; ++d) {
        std::vector<int> row;
        for (int j = 0; j < 6; ++j) if (depth[j] == d) row.push_back(j);
        for (size_t k = 0; k < row.size(); ++k) {
            const float x = inner.getX() + inner.getWidth() * (float)(k + 1) / (float)(row.size() + 1);
            const float y = inner.getBottom() - box - 16.f - (float)d * pitch + box / 2;
            pos[row[k]] = {x, y};
        }
    }
    g.setColour(kDim);
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            if (A.mods[i] & (1 << j))
                g.drawLine(pos[j].x, pos[j].y + box / 2, pos[i].x, pos[i].y - box / 2, 2.f);
    for (int i = 0; i < 6; ++i)
        if (A.carriers & (1 << i)) {
            g.setColour(kOrange);
            g.drawLine(pos[i].x, pos[i].y + box / 2, pos[i].x, inner.getBottom() - 6.f, 2.f);
        }
    g.setColour(kOrange);
    g.drawLine(inner.getX(), inner.getBottom() - 6.f, inner.getRight(), inner.getBottom() - 6.f, 2.f);
    g.setFont(sans(11.f, true));
    g.drawText("OUT", juce::Rectangle<float>(inner.getRight() - 36.f, inner.getBottom() - 4.f, 40.f, 14.f), juce::Justification::centredRight);

    if (A.fbSrc >= 0 && A.fbDst >= 0) {
        g.setColour(kOk);
        juce::Path p;
        auto s = pos[A.fbSrc], d = pos[A.fbDst];
        if (A.fbSrc == A.fbDst) {
            p.startNewSubPath(s.x + box / 2, s.y + 4.f);
            p.lineTo(s.x + box / 2 + 11.f, s.y + 4.f);
            p.lineTo(s.x + box / 2 + 11.f, s.y - box / 2 - 9.f);
            p.lineTo(s.x, s.y - box / 2 - 9.f);
            p.lineTo(s.x, s.y - box / 2);
        } else {
            const float xr = std::max(s.x, d.x) + box / 2 + 10.f;
            p.startNewSubPath(s.x + box / 2, s.y);
            p.lineTo(xr, s.y);
            p.lineTo(xr, d.y - box / 2 - 8.f);
            p.lineTo(d.x, d.y - box / 2 - 8.f);
            p.lineTo(d.x, d.y - box / 2);
        }
        g.strokePath(p, juce::PathStrokeType(2.f));
    }
    for (int i = 0; i < 6; ++i) {
        auto r = juce::Rectangle<float>(box, box).withCentre(pos[i]);
        const bool car = A.carriers & (1 << i);
        const bool dead = v_.op[i].ol == 0;
        g.setGradientFill(juce::ColourGradient(car ? kOrange.brighter(0.2f) : juce::Colour(0xff2b2c31), r.getX(), r.getY(),
                                               car ? kOrange.darker(0.3f) : juce::Colour(0xff17181b), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour(i == hl ? juce::Colours::white : (car ? kOrange : kDim));
        g.drawRoundedRectangle(r, 4.f, i == hl ? 2.6f : 1.4f);
        g.setColour((car ? juce::Colour(0xff211105) : kText).withAlpha(dead ? 0.4f : 1.f));
        g.setFont(sans(box * 0.46f, true));
        g.drawText(juce::String(i + 1), r, juce::Justification::centred);
    }
}

void Dx7Panel::drawEnvelope(juce::Graphics& g, juce::Rectangle<float> r, const int* rate, const int* level, bool markers) {
    g.setColour(juce::Colour(0xff0d0e10));
    g.fillRoundedRectangle(r, 6.f);
    g.setColour(kEdge);
    g.drawRoundedRectangle(r, 6.f, 1.f);
    auto in = r.reduced(12.f, 14.f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    for (int i = 0; i <= 4; ++i) g.drawHorizontalLine((int)(in.getY() + in.getHeight() * (float)i / 4.f), in.getX(), in.getRight());

    float w[5];
    for (int k = 0; k < 3; ++k) w[k] = 0.10f + 0.90f * float(99 - rate[k]) / 99.f;
    w[3] = 0.6f;
    w[4] = 0.10f + 0.90f * float(99 - rate[3]) / 99.f;
    const float total = w[0] + w[1] + w[2] + w[3] + w[4];
    auto Y = [&](int l) { return in.getBottom() - in.getHeight() * (float)l / 99.f; };
    const int lv[5] = {level[0], level[1], level[2], level[2], level[3]};
    juce::Path line, fill;
    float x = in.getX();
    line.startNewSubPath(x, Y(level[3]));
    fill.startNewSubPath(x, in.getBottom());
    fill.lineTo(x, Y(level[3]));
    std::vector<juce::Point<float>> pts;
    for (int k = 0; k < 5; ++k) {
        x += in.getWidth() * w[k] / total;
        line.lineTo(x, Y(lv[k]));
        fill.lineTo(x, Y(lv[k]));
        pts.push_back({x, Y(lv[k])});
    }
    fill.lineTo(x, in.getBottom());
    fill.closeSubPath();
    g.setColour(kOrange.withAlpha(0.14f));
    g.fillPath(fill);
    g.setColour(kOrange);
    g.strokePath(line, juce::PathStrokeType(2.f));
    if (markers) {
        g.setFont(sans(11.f, true));
        const char* nm[5] = {"1", "2", "3", "", "4"};
        for (size_t k = 0; k < pts.size(); ++k) {
            if (k == 3) continue;
            g.setColour(kOrange);
            g.fillEllipse(juce::Rectangle<float>(8.f, 8.f).withCentre(pts[k]));
            g.setColour(kText);
            g.drawText(nm[k], juce::Rectangle<float>(16.f, 14.f).withCentre(pts[k].translated(0, -14.f)), juce::Justification::centred);
        }
        g.setColour(kDim);
        g.drawText("KEY ON", in.withTrimmedTop(in.getHeight() - 14.f), juce::Justification::bottomLeft);
        g.drawText("KEY OFF", in.withTrimmedTop(in.getHeight() - 14.f), juce::Justification::bottomRight);
    }
}

void Dx7Panel::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(kPlateTop, 0, 0, kPlateBot, 0, (float)kHeight, false));
    g.fillAll();
    g.setColour(juce::Colours::white.withAlpha(0.018f));
    for (int yy = 0; yy < kHeight; yy += 3) g.drawHorizontalLine(yy, 0.f, (float)kWidth);

    g.setColour(kText);
    g.setFont(sans(30.f, true));
    g.drawText("ZIRCON FM-6", 24, 10, 300, 36, juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(sans(9.5f, true));
    g.drawText("DIGITAL PROGRAMMABLE ALGORITHM SYNTHESIZER", 24, 46, 310, 14, juce::Justification::centredLeft);
    g.setFont(sans(10.f));
    g.drawText("6 OPERATORS  /  32 ALGORITHMS  /  .SYX COMPATIBLE", 24, 60, 310, 14, juce::Justification::centredLeft);
    const juce::Colour stripe[3] = {kOrange, kRed, kPlum};
    for (int i = 0; i < 3; ++i) { g.setColour(stripe[i]); g.fillRect(24, 84 + i * 5, 300, 3); }

    drawLcd(g);

    auto inset = juce::Rectangle<float>(12.f, (float)kContentY - 8.f, (float)kWidth - 24.f, (float)kContentH + 4.f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(inset.translated(0, 2.f), 10.f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff17181a), 0, inset.getY(), juce::Colour(0xff101012), 0, inset.getBottom(), false));
    g.fillRoundedRectangle(inset, 10.f);
    g.setColour(kEdge);
    g.drawRoundedRectangle(inset, 10.f, 1.f);

    const int cy = kContentY;
    const int top = curTop_ > 0 ? 1 : 0;

    if (top == 0 && curSub_ == 0) {
        groupTitle(g, "Algorithm " + juce::String(v_.algo + 1), 24, cy + 6, 540);
        drawAlgorithm(g, {24.f, (float)cy + 28.f, 540.f, 380.f}, -1);
        groupTitle(g, "Voice", 585, cy + 6, 390);
        g.setColour(kDim);
        g.setFont(sans(11.5f));
        g.drawFittedText("Orange operators are carriers (heard). Grey operators modulate the one they feed. "
                         "Green is the feedback path. Output level 0 mutes an operator.",
                         juce::Rectangle<int>(585, cy + 260, 390, 60), juce::Justification::topLeft, 3);
    } else if (top == 0 && curSub_ == 1) {
        groupTitle(g, "LFO", 24, cy + 6, 560);
        groupTitle(g, "Pitch EG", 600, cy + 6, 378);
        g.setColour(kDim);
        g.setFont(sans(10.5f, true));
        g.drawText("RATE", 612, cy + 22, 4 * 46, 12, juce::Justification::centred);
        g.drawText("LEVEL", 612 + 4 * 46 + 14, cy + 22, 4 * 46, 12, juce::Justification::centred);
        groupTitle(g, "Pitch envelope", 24, cy + 228, 560);
        drawEnvelope(g, {24.f, (float)cy + 250.f, 560.f, 180.f}, v_.prate, v_.plevel, false);
    } else if (top == 0) {
        groupTitle(g, "Macro knobs", 24, cy + 6, 470);
        groupTitle(g, "Delay", 24, cy + 190, 290);
        groupTitle(g, "Chorus", 340, cy + 190, 290);
        groupTitle(g, "Reverb", 656, cy + 190, 290);
        g.setColour(kDim);
        g.setFont(sans(11.5f));
        g.drawFittedText("The four macros are the patch's own knobs (Level, Bright = modulation depth, EG Speed, Vibrato). "
                         "Chorus, delay and reverb are global effects (in that order): they apply to every instrument and are host-automatable.",
                         juce::Rectangle<int>(540, cy + 30, 430, 70), juce::Justification::topLeft, 4);
    } else if (curSub_ == 0) {
        groupTitle(g, "Operator " + juce::String(curTop_) + " tuning", 24, cy + 6, 460);
        groupTitle(g, "Routing", 610, cy + 6, 370);
        drawAlgorithm(g, {610.f, (float)cy + 28.f, 370.f, 380.f}, curOp_);
        g.setColour(kLcdHi);
        g.setFont(mono(18.f));
        g.drawText(frequencyText(), 24, cy + 372, 460, 28, juce::Justification::centredLeft);
    } else if (curSub_ == 1) {
        groupTitle(g, "Rate", 24, cy + 6, 220);
        groupTitle(g, "Level", 24 + 4 * 56 + 24, cy + 6, 220);
        const auto& o = v_.op[curOp_];
        groupTitle(g, "Operator " + juce::String(curTop_) + " envelope", 500, cy + 6, 478);
        drawEnvelope(g, {500.f, (float)cy + 28.f, 478.f, 240.f}, o.rate, o.level, true);
    } else {
        groupTitle(g, "Operator " + juce::String(curTop_) + " keyboard level scaling", 24, cy + 6, 570);
        groupTitle(g, "Routing", 610, cy + 6, 370);
        drawAlgorithm(g, {610.f, (float)cy + 28.f, 370.f, 380.f}, curOp_);
        const auto& o = v_.op[curOp_];
        auto area = juce::Rectangle<float>(24.f, (float)cy + 180.f, 570.f, 240.f);
        g.setColour(juce::Colour(0xff0d0e10));
        g.fillRoundedRectangle(area, 6.f);
        g.setColour(kEdge);
        g.drawRoundedRectangle(area, 6.f, 1.f);
        auto in = area.reduced(14.f, 16.f);
        const float mid = in.getCentreY();
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine((int)mid, in.getX(), in.getRight());
        auto shape = [](int curveType, float n) {
            const bool neg = curveType <= 1;
            const bool ex = curveType == 1 || curveType == 2;
            const float m = ex ? n * n : n;
            return neg ? -m : m;
        };
        juce::Path p;
        for (int k = 0; k <= 99; ++k) {
            const float dpt = (float)(k - o.bp);
            float eff;
            if (dpt < 0) eff = shape(o.lc, o.bp > 0 ? -dpt / (float)o.bp : 0.f) * (float)o.ld / 99.f;
            else eff = shape(o.rc, o.bp < 99 ? dpt / (float)(99 - o.bp) : 0.f) * (float)o.rd / 99.f;
            const float xx = in.getX() + in.getWidth() * (float)k / 99.f;
            const float yy = mid - eff * in.getHeight() * 0.5f;
            if (k == 0) p.startNewSubPath(xx, yy); else p.lineTo(xx, yy);
        }
        g.setColour(kOrange);
        g.strokePath(p, juce::PathStrokeType(2.f));
        const float bx = in.getX() + in.getWidth() * (float)o.bp / 99.f;
        g.setColour(kDim);
        g.drawVerticalLine((int)bx, in.getY(), in.getBottom());
        g.setFont(sans(11.f, true));
        g.drawText("BREAKPOINT " + noteName(21 + o.bp), juce::Rectangle<float>(bx - 70.f, in.getBottom() - 14.f, 140.f, 14.f),
                   juce::Justification::centred);
        g.drawText("+ LOUDER", in.withHeight(14.f), juce::Justification::topLeft);
        g.drawText("- QUIETER", in.withTrimmedTop(in.getHeight() - 14.f), juce::Justification::bottomLeft);
    }

    g.setColour(juce::Colour(0xffcfccbf));
    g.setFont(sans(11.5f, true));
    for (auto& c : ctls_)
        if (c->isVisible()) g.drawText(c->name.toUpperCase(), c->cell.getX(), c->cell.getY(), c->cell.getWidth(), 14, juce::Justification::centred);
    for (auto& h : hosts_)
        if (h->isVisible()) g.drawText(h->name.toUpperCase(), h->cell.getX(), h->cell.getY(), h->cell.getWidth(), 14, juce::Justification::centred);
}
