#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "zircon/engine.hpp"
#include <memory>

// Host-facing wrapper. The patch's own params are mapped, in order, onto a
// fixed bank of kHostParams automatable slots (VST3 needs a static list).
class ZirconProcessor : public juce::AudioProcessor {
public:
    static constexpr int kHostParams = 16;
    static constexpr int kFxParams = 6;   // delay time/feedback/mix, reverb size/damp/mix (global, all instruments)

    ZirconProcessor();
    ~ZirconProcessor() override = default;

    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Zircon Elemental"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return factory_.size(); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Compile + swap. Returns empty string on success, else the compile error.
    juce::String loadPatchText(const juce::String& json);
    juce::String loadPatchFile(const juce::File& f);      // .json patch or .syx DX7 cartridge
    juce::String loadSysex(const juce::File& f);          // loads a cartridge into the DX7 instrument
    juce::String saveSysex(const juce::File& f);          // writes the cartridge as a 32-voice bulk dump

    // DX7 instrument: live voice + cartridge (32 slots)
    bool dx7Active() const { return dx7Active_.load(); }
    zircon::dx7::Voice dx7Voice();                        // copy of the live voice
    void setDx7Voice(const zircon::dx7::Voice&);          // live edit (audio thread safe)
    int cartSize() const { return (int)cart_.size(); }
    juce::String cartName(int i) const;
    int cartIndex() const { return cartIndex_; }
    void selectCartVoice(int i);                          // store pending edit, then switch
    void storeToCart();                                   // write the live voice into its slot
    void initDx7Voice();
    std::function<void()> onCartChanged;

    juce::String patchName() const;
    juce::StringArray macroNames() const {
        juce::StringArray a; for (auto& n : lib_.names()) a.add(juce::String(n)); return a;
    }
    juce::String macroText(const juce::String& n) const {
        return lib_.has(n.toStdString()) ? juce::String(lib_.get(n.toStdString())) : juce::String();
    }
    juce::String patchText() const { return patchText_; }
    juce::String lastError() const { return lastError_; }
    int mappedParamCount() const;
    juce::String mappedParamName(int slot) const;
    juce::RangedAudioParameter* hostParam(int slot) { return hostParams_[slot]; }
    juce::RangedAudioParameter* fxParam(int i) { return fxParams_[(size_t)i]; }
    int activeVoices() const { return voicesActive_.load(); }
    float peakLevel() const { return peak_.load(); }

    std::function<void()> onPatchChanged;       // editor hook (message thread)
    std::function<void()> onProgramListChanged; // program list replaced (e.g. .syx cartridge loaded)

private:
    struct Factory { juce::String name, json; };
    std::vector<zircon::dx7::Voice> cart_;
    int cartIndex_ = 0;
    std::atomic<bool> dx7Active_{false};
    void applyDx7ToEngine(const zircon::dx7::Voice&);
    juce::Array<Factory> factory_;
    int currentProgram_ = 0;

    zircon::MacroLibrary lib_;
    std::shared_ptr<zircon::Engine> engine_;      // swapped under lock_
    juce::SpinLock lock_;
    juce::String patchText_, lastError_;
    juce::String pendingDx7_;   // restored voice text, applied after the patch compiles
    double sampleRate_ = 48000.0;

    std::array<juce::RangedAudioParameter*, kHostParams> hostParams_{};
    std::array<juce::RangedAudioParameter*, kFxParams> fxParams_{};
    juce::Reverb reverb_;
    std::vector<float> dlBuf_[2];
    int dlPos_ = 0;
    float dlTime_ = 0.3f;     // smoothed delay time, seconds
    void applyFx(float* L, float* R, int n);
    std::array<float, kHostParams> lastSent_{};
    std::atomic<int> voicesActive_{0};
    std::atomic<float> peak_{0.f};
};
