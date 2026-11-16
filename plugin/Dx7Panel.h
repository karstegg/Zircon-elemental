#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

// Look of a late-80s digital FM synth front panel: dark faceplate, olive LCD, membrane buttons,
// black metal knobs and slider caps.
class Dx7Look : public juce::LookAndFeel_V4 {
public:
    Dx7Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
};

// Full DX7 editor, organised in pages: MASTER (voice / lfo+pitch eg / fx) and OP 1..6 (tune / envelope / scaling).
class Dx7Panel : public juce::Component {
public:
    explicit Dx7Panel(ZirconProcessor&);
    ~Dx7Panel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();                  // re-read the live voice + cartridge from the processor

    static constexpr int kWidth = 1000, kHeight = 650;

private:
    // top page: 0 = master, 1..6 = operator n; sub page 0..2
    struct Ctl : juce::Slider {
        std::function<int&(zircon::dx7::Voice&)> ref;
        juce::String name;
        int top = 0, sub = 0;        // top 0 = master pages, 1 = operator pages (bound to curOp_)
        juce::Rectangle<int> cell;
    };
    struct Sw : juce::TextButton {
        std::function<int&(zircon::dx7::Voice&)> ref;
        juce::String onText, offText;
        int top = 0, sub = 0;
    };
    struct HostCtl : juce::Slider {
        std::unique_ptr<juce::SliderParameterAttachment> attach;
        juce::String name;
        int sub = 2;
        juce::Rectangle<int> cell;
    };

    Ctl* knob(const juce::String& name, int lo, int hi, std::function<int&(zircon::dx7::Voice&)> ref,
              std::function<juce::String(int)> fmt, int top, int sub, int x, int y);
    Ctl* fader(const juce::String& name, int lo, int hi, std::function<int&(zircon::dx7::Voice&)> ref,
               std::function<juce::String(int)> fmt, int top, int sub, int x, int y);
    Sw* toggle(const juce::String& offText, const juce::String& onText, std::function<int&(zircon::dx7::Voice&)> ref,
               int top, int sub, int x, int y);
    HostCtl* hostKnob(juce::RangedAudioParameter* p, const juce::String& name, int x, int y);
    void edit(Ctl&);
    void showPage();
    void rebuildVoiceList();
    void drawAlgorithm(juce::Graphics&, juce::Rectangle<float>, int highlightOp);
    void drawEnvelope(juce::Graphics&, juce::Rectangle<float>, const int* rate, const int* level, bool markers);
    void drawLcd(juce::Graphics&);
    juce::String frequencyText() const;

    ZirconProcessor& proc_;
    Dx7Look look_;
    bool built_ = false;
    zircon::dx7::Voice v_;
    int curTop_ = 0, curSub_ = 0, curOp_ = 0;
    std::vector<std::unique_ptr<Ctl>> ctls_;
    std::vector<std::unique_ptr<Sw>> sws_;
    std::vector<std::unique_ptr<HostCtl>> hosts_;

    juce::TextButton topBtn_[7], subBtn_[3];
    juce::TextButton prev_{"<"}, next_{">"}, load_{"LOAD SYX"}, save_{"SAVE SYX"}, init_{"INIT"}, factory_{"FACTORY"};
    juce::ComboBox voices_;
    juce::TextEditor name_;
    juce::Label status_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
