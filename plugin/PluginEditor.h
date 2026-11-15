#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// v0 "panel view": header (preset picker, load, status) + auto-generated
// knob panel from the patch's params. The node-graph structure view is P2.
class PrimalLook : public juce::LookAndFeel_V4 {
public:
    PrimalLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
};

class PrimalEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PrimalEditor(PrimalProcessor&);
    ~PrimalEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildKnobs();

    PrimalProcessor& proc_;
    PrimalLook look_;
    juce::ComboBox presets_;
    juce::TextButton load_{"Load patch..."};
    juce::Label status_, error_;
    std::unique_ptr<juce::FileChooser> chooser_;

    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
    };
    std::vector<std::unique_ptr<Knob>> knobs_;
};
