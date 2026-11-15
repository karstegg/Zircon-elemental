#include "PluginEditor.h"

namespace col {
const juce::Colour bg{0xff111318}, panel{0xff1a1d24}, edge{0xff2a2f3a}, text{0xffe6e8ee},
    dim{0xff8a90a0}, accent{0xffff8a3d}, ok{0xff5ad19a}, err{0xffff5a6a};
}

ZirconLook::ZirconLook() {
    setColour(juce::ComboBox::backgroundColourId, col::panel);
    setColour(juce::ComboBox::outlineColourId, col::edge);
    setColour(juce::ComboBox::textColourId, col::text);
    setColour(juce::TextButton::buttonColourId, col::panel);
    setColour(juce::TextButton::textColourOffId, col::text);
    setColour(juce::PopupMenu::backgroundColourId, col::panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha(0.3f));
    setColour(juce::Label::textColourId, col::text);
    setColour(juce::Slider::textBoxTextColourId, col::dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void ZirconLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                                  juce::Slider&) {
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.f);
    float d = std::min(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(d, d);
    float ang = a0 + pos * (a1 - a0);
    juce::Path track, val;
    track.addCentredArc(c.getCentreX(), c.getCentreY(), d / 2 - 3, d / 2 - 3, 0, a0, a1, true);
    val.addCentredArc(c.getCentreX(), c.getCentreY(), d / 2 - 3, d / 2 - 3, 0, a0, ang, true);
    g.setColour(col::edge);
    g.strokePath(track, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(col::accent);
    g.strokePath(val, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto inner = c.reduced(d * 0.22f);
    g.setColour(col::panel.brighter(0.08f));
    g.fillEllipse(inner);
    juce::Point<float> tip = inner.getCentre().getPointOnCircumference(inner.getWidth() * 0.42f, ang);
    g.setColour(col::text);
    g.drawLine({inner.getCentre().getPointOnCircumference(inner.getWidth() * 0.15f, ang), tip}, 2.5f);
}

ZirconEditor::ZirconEditor(ZirconProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    setLookAndFeel(&look_);
    refreshPresets();
    presets_.onChange = [this] { proc_.setCurrentProgram(presets_.getSelectedId() - 1); };
    addAndMakeVisible(presets_);

    load_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load Zircon patch (.json) or DX7 cartridge (.syx)", juce::File(), "*.json;*.syx");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile()) { auto e = proc_.loadPatchFile(f); error_.setText(e, juce::dontSendNotification); }
                              });
    };
    addAndMakeVisible(load_);
    status_.setColour(juce::Label::textColourId, col::dim);
    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);
    error_.setColour(juce::Label::textColourId, col::err);
    addAndMakeVisible(error_);

    addMouseListener(this, true);   // right-click anywhere (incl. over knobs) opens the context menu
    proc_.onProgramListChanged = [this] { refreshPresets(); };
    proc_.onPatchChanged = [this] {
        rebuildKnobs();
        if (structure_ != nullptr && structure_->isVisible()) structure_->setPatch(proc_.patchName(), proc_.patchText());
        presets_.setSelectedId(proc_.getCurrentProgram() + 1, juce::dontSendNotification);
        error_.setText(proc_.lastError(), juce::dontSendNotification);
    };
    rebuildKnobs();
    setResizable(true, true);
    setResizeLimits(520, 300, 1600, 1000);
    setSize(760, 420);
    startTimerHz(15);
}

ZirconEditor::~ZirconEditor() {
    proc_.onProgramListChanged = nullptr;
    removeMouseListener(this);
    structure_.reset();
    proc_.onPatchChanged = nullptr;
    setLookAndFeel(nullptr);
}

void ZirconEditor::refreshPresets() {
    presets_.clear(juce::dontSendNotification);
    for (int i = 0; i < proc_.getNumPrograms(); ++i) presets_.addItem(proc_.getProgramName(i), i + 1);
    presets_.setSelectedId(proc_.getCurrentProgram() + 1, juce::dontSendNotification);
}

void ZirconEditor::rebuildKnobs() {
    knobs_.clear();
    for (int i = 0; i < proc_.mappedParamCount(); ++i) {
        auto k = std::make_unique<Knob>();
        k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        k->label.setText(proc_.mappedParamName(i), juce::dontSendNotification);
        k->label.setJustificationType(juce::Justification::centred);
        k->label.setFont(juce::FontOptions(13.f));
        k->attach = std::make_unique<juce::SliderParameterAttachment>(*proc_.hostParam(i), k->slider);
        addAndMakeVisible(k->slider);
        addAndMakeVisible(k->label);
        knobs_.push_back(std::move(k));
    }
    resized();
    repaint();
}

void ZirconEditor::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    auto b = getLocalBounds().reduced(14);
    auto head = b.removeFromTop(48);
    g.setColour(col::text);
    g.setFont(juce::FontOptions(22.f, juce::Font::bold));
    g.drawText("ZIRCON", head.removeFromLeft(110), juce::Justification::centredLeft);
    g.setColour(col::accent);
    g.setFont(juce::FontOptions(13.f));
    g.drawText("ELEMENTAL v0", head.removeFromLeft(90), juce::Justification::centredLeft);

    b.removeFromTop(8);
    auto panel = b.withTrimmedBottom(26);
    g.setColour(col::panel);
    g.fillRoundedRectangle(panel.toFloat(), 10.f);
    g.setColour(col::edge);
    g.drawRoundedRectangle(panel.toFloat(), 10.f, 1.f);
    g.setColour(col::text);
    g.setFont(juce::FontOptions(18.f, juce::Font::bold));
    g.drawText(proc_.patchName(), panel.reduced(16, 10).removeFromTop(24), juce::Justification::centredLeft);
    if (knobs_.empty()) {
        g.setColour(col::dim);
        g.setFont(juce::FontOptions(14.f));
        g.drawText("This patch exposes no params.", panel, juce::Justification::centred);
    }
    // level meter
    auto meter = panel.reduced(16).removeFromRight(6).toFloat();
    g.setColour(col::edge); g.fillRoundedRectangle(meter, 3.f);
    float lv = juce::jlimit(0.f, 1.f, proc_.peakLevel());
    g.setColour(lv > 0.95f ? col::err : col::ok);
    g.fillRoundedRectangle(meter.withTrimmedTop(meter.getHeight() * (1.f - lv)), 3.f);
}

void ZirconEditor::resized() {
    auto b = getLocalBounds().reduced(14);
    auto head = b.removeFromTop(48);
    head.removeFromLeft(210);
    load_.setBounds(head.removeFromRight(120).reduced(0, 8));
    head.removeFromRight(8);
    presets_.setBounds(head.removeFromRight(220).reduced(0, 8));
    status_.setBounds(head.reduced(0, 8));
    b.removeFromTop(8);
    error_.setBounds(b.removeFromBottom(22));
    b.removeFromBottom(4);
    auto panel = b.reduced(16, 10);
    panel.removeFromTop(34);
    panel.removeFromRight(20);
    if (knobs_.empty()) return;
    const int cell = 104;
    int cols = std::max(1, panel.getWidth() / cell);
    for (size_t i = 0; i < knobs_.size(); ++i) {
        int cx = (int)i % cols, cy = (int)i / cols;
        juce::Rectangle<int> r(panel.getX() + cx * cell, panel.getY() + cy * (cell + 14), cell, cell + 14);
        knobs_[i]->label.setBounds(r.removeFromBottom(20));
        knobs_[i]->slider.setBounds(r.reduced(10, 4));
    }
}

void ZirconEditor::timerCallback() {
    status_.setText(juce::String(proc_.activeVoices()) + " voices", juce::dontSendNotification);
    repaint();
}

void ZirconEditor::showStructure() {
    if (structure_ == nullptr)
    {
        StructureHooks h;
        h.onClose = [this] { if (structure_) structure_->setVisible(false); };
        h.onEdit = [this](const juce::String& json) { return proc_.loadPatchText(json); };
        h.macroNames = [this] { return proc_.macroNames(); };
        h.macroText = [this](const juce::String& n) { return proc_.macroText(n); };
        structure_ = std::make_unique<StructureWindow>(std::move(h));
    }
    structure_->setPatch(proc_.patchName(), proc_.patchText());
    structure_->setVisible(true);
    structure_->toFront(true);
}

void ZirconEditor::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) return;
    juce::PopupMenu m;
    m.addItem(1, "Show structure / wiring...");
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r == 1) showStructure(); });
}
