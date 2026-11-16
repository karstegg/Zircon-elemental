#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

// Full DX7 editor: cartridge (load/save .syx, voice list, name), algorithm + diagram,
// feedback/transpose/LFO/pitch-EG, and all 21 parameters of each of the 6 operators.
class Dx7Panel : public juce::Component {
public:
    explicit Dx7Panel(ZirconProcessor&);
    ~Dx7Panel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();                  // re-read the live voice + cartridge from the processor

    static constexpr int kWidth = 1020, kHeight = 820;

private:
    struct Cell : juce::Slider {
        std::function<int&(zircon::dx7::Voice&)> ref;
    };
    Cell* addCell(const juce::String& name, int lo, int hi, std::function<int&(zircon::dx7::Voice&)> ref,
                  std::function<juce::String(int)> fmt, bool showName);
    void edit(Cell&);
    void rebuildVoiceList();
    void drawAlgorithm(juce::Graphics&, juce::Rectangle<float>);
    void drawEnvelope(juce::Graphics&, juce::Rectangle<float>, const int* rate, const int* level);

    ZirconProcessor& proc_;
    bool built_ = false;
    zircon::dx7::Voice v_;
    std::vector<std::unique_ptr<Cell>> cells_;
    Cell* algo_ = nullptr;
    Cell* opCell_[6][21] = {};
    Cell* globals_[8] = {};          // algo, fb, transpose, oks column (for layout)
    Cell* lfo_[7] = {};
    Cell* peg_[8] = {};

    struct HostBar {
        juce::Slider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
    };
    std::vector<std::unique_ptr<HostBar>> hostBars_;

    juce::ComboBox voices_;
    juce::TextButton load_{"Load .syx"}, save_{"Save .syx"}, init_{"Init voice"};
    juce::TextEditor name_;
    juce::Label cartLabel_, status_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
