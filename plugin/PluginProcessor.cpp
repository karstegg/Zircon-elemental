#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

PrimalProcessor::PrimalProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    for (int i = 0; i < kHostParams; ++i) {
        auto* p = new juce::AudioParameterFloat(juce::ParameterID{"p" + juce::String(i + 1), 1},
                                                "Knob " + juce::String(i + 1),
                                                juce::NormalisableRange<float>(0.f, 1.f), 0.5f);
        hostParams_[i] = p;
        addParameter(p);
        lastSent_[i] = -1.f;
    }
    // embedded macro library + factory patches
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        juce::String text(juce::CharPointer_UTF8(data), (size_t)size);
        juce::String file = BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[i]);
        auto parsed = juce::JSON::parse(text);
        bool isPatch = parsed.hasProperty("out");
        if (isPatch) factory_.add({parsed.getProperty("name", file).toString(), text});
        else lib_.addMacro(parsed.getProperty("name", file.upToFirstOccurrenceOf(".", false, false)).toString().toStdString(),
                           text.toStdString());
    }
    std::sort(factory_.begin(), factory_.end(), [](auto& a, auto& b) { return a.name < b.name; });
    if (!factory_.isEmpty()) loadPatchText(factory_[0].json);
}

bool PrimalProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PrimalProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    const juce::SpinLock::ScopedLockType sl(lock_);
    if (engine_) engine_->prepare(sr);
}

juce::String PrimalProcessor::loadPatchText(const juce::String& json) {
    std::shared_ptr<primal::Engine> fresh;
    try {
        auto prog = primal::compilePatch(json.toStdString(), lib_);
        fresh = std::make_shared<primal::Engine>(std::move(prog));
        fresh->prepare(sampleRate_);
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return lastError_;
    }
    // reflect patch defaults onto host knobs
    for (int i = 0; i < kHostParams; ++i) {
        if (i < fresh->numParams()) {
            auto& pi = fresh->paramInfo(i);
            hostParams_[i]->setValueNotifyingHost(pi.toNormalized(pi.def));
        }
        lastSent_[i] = -1.f;
    }
    {
        const juce::SpinLock::ScopedLockType sl(lock_);
        std::swap(engine_, fresh);   // old engine destroyed outside the lock, below
    }
    patchText_ = json;
    lastError_ = {};
    updateHostDisplay(ChangeDetails().withProgramChanged(true).withParameterInfoChanged(true));
    if (onPatchChanged) juce::MessageManager::callAsync([this] { if (onPatchChanged) onPatchChanged(); });
    return {};
}

juce::String PrimalProcessor::loadPatchFile(const juce::File& f) {
    if (!f.existsAsFile()) return lastError_ = "file not found";
    return loadPatchText(f.loadFileAsString());
}

void PrimalProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= factory_.size()) return;
    currentProgram_ = index;
    loadPatchText(factory_[index].json);
}
const juce::String PrimalProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, factory_.size()) ? factory_[index].name : juce::String();
}

juce::String PrimalProcessor::patchName() const {
    return engine_ ? juce::String(engine_->program().name) : "(none)";
}
int PrimalProcessor::mappedParamCount() const {
    return engine_ ? std::min(kHostParams, engine_->numParams()) : 0;
}
juce::String PrimalProcessor::mappedParamName(int slot) const {
    return engine_ && slot < engine_->numParams() ? juce::String(engine_->paramInfo(slot).name) : juce::String();
}

void PrimalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const juce::SpinLock::ScopedTryLockType sl(lock_);
    if (!sl.isLocked() || !engine_) return;
    auto& eng = *engine_;

    for (int i = 0; i < std::min(kHostParams, eng.numParams()); ++i) {
        float v = hostParams_[i]->getValue();
        if (v != lastSent_[i]) { eng.setParamNormalized(i, v); lastSent_[i] = v; }
    }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    int pos = 0, n = buffer.getNumSamples();
    for (const auto meta : midi) {
        int t = juce::jlimit(0, n, meta.samplePosition);
        if (t > pos) { eng.process(L + pos, R + pos, t - pos); pos = t; }
        auto m = meta.getMessage();
        if (m.isNoteOn()) eng.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) eng.noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) eng.allNotesOff();
        else if (m.isPitchWheel()) eng.setPitchBend((m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
    }
    if (pos < n) eng.process(L + pos, R + pos, n - pos);
    voicesActive_ = eng.activeVoices();
    peak_ = std::max(buffer.getMagnitude(0, 0, n), peak_.load() * 0.9f);
}

void PrimalProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree st("PrimalState");
    st.setProperty("patch", patchText_, nullptr);
    st.setProperty("program", currentProgram_, nullptr);
    for (int i = 0; i < kHostParams; ++i) st.setProperty("p" + juce::String(i + 1), hostParams_[i]->getValue(), nullptr);
    juce::MemoryOutputStream os(dest, false);
    st.writeToStream(os);
}

void PrimalProcessor::setStateInformation(const void* data, int size) {
    auto st = juce::ValueTree::readFromData(data, (size_t)size);
    if (!st.isValid()) return;
    currentProgram_ = st.getProperty("program", 0);
    loadPatchText(st.getProperty("patch").toString());
    for (int i = 0; i < kHostParams; ++i)
        if (st.hasProperty("p" + juce::String(i + 1)))
            hostParams_[i]->setValueNotifyingHost(st.getProperty("p" + juce::String(i + 1)));
}

juce::AudioProcessorEditor* PrimalProcessor::createEditor() { return new PrimalEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrimalProcessor(); }
