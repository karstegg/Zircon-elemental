#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "StructureView.h"

// v0 "panel view": header (preset picker, load, status) + auto-generated
// knob panel from the patch's params. The node-graph structure view is P2.
class ZirconLook : public juce::LookAndFeel_V4 {
public:
    ZirconLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
};

class ZirconEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ZirconEditor(ZirconProcessor&);
    ~ZirconEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void rebuildKnobs();
    void refreshPresets();
    void showStructure();

    ZirconProcessor& proc_;
    ZirconLook look_;
    juce::ComboBox presets_;
    juce::TextButton load_{"Load patch..."};
    juce::Label status_, error_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<StructureWindow> structure_;

    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
    };
    std::vector<std::unique_ptr<Knob>> knobs_;
};
