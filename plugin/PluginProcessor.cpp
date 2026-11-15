#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

ZirconProcessor::ZirconProcessor()
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

bool ZirconProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ZirconProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    const juce::SpinLock::ScopedLockType sl(lock_);
    if (engine_) engine_->prepare(sr);
}

juce::String ZirconProcessor::loadPatchText(const juce::String& json) {
    std::shared_ptr<zircon::Engine> fresh;
    try {
        auto prog = zircon::compilePatch(json.toStdString(), lib_);
        fresh = std::make_shared<zircon::Engine>(std::move(prog));
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

juce::String ZirconProcessor::loadSysex(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return lastError_ = "could not read file";
    std::vector<uint8_t> bytes((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize());
    auto voices = zircon::dx7::parseSysex(bytes);
    if (voices.empty()) return lastError_ = "not a DX7 SysEx dump (expected a 32-voice bulk or single-voice .syx)";
    for (int i = factory_.size(); --i >= 0;) if (factory_.getReference(i).cart) factory_.remove(i);
    const int first = factory_.size();
    for (auto& v : voices) {
        juce::String json(zircon::dx7::patchJson(v));
        factory_.add({juce::JSON::parse(json).getProperty("name", "DX7").toString(), json, true});
    }
    setCurrentProgram(first);
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    if (onProgramListChanged) juce::MessageManager::callAsync([this] { if (onProgramListChanged) onProgramListChanged(); });
    return lastError_;
}

juce::String ZirconProcessor::loadPatchFile(const juce::File& f) {
    if (!f.existsAsFile()) return lastError_ = "file not found";
    if (f.hasFileExtension("syx")) return loadSysex(f);
    return loadPatchText(f.loadFileAsString());
}

void ZirconProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= factory_.size()) return;
    currentProgram_ = index;
    loadPatchText(factory_[index].json);
}
const juce::String ZirconProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, factory_.size()) ? factory_[index].name : juce::String();
}

juce::String ZirconProcessor::patchName() const {
    return engine_ ? juce::String(engine_->program().name) : "(none)";
}
int ZirconProcessor::mappedParamCount() const {
    return engine_ ? std::min(kHostParams, engine_->numParams()) : 0;
}
juce::String ZirconProcessor::mappedParamName(int slot) const {
    return engine_ && slot < engine_->numParams() ? juce::String(engine_->paramInfo(slot).name) : juce::String();
}

void ZirconProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
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
        else if (m.isSustainPedalOn()) eng.setSustain(true);
        else if (m.isSustainPedalOff()) eng.setSustain(false);
        else if (m.isControllerOfType(1)) eng.setModWheel(m.getControllerValue() / 127.f);
        else if (m.isPitchWheel()) eng.setPitchBend((m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
    }
    if (pos < n) eng.process(L + pos, R + pos, n - pos);
    voicesActive_ = eng.activeVoices();
    peak_ = std::max(buffer.getMagnitude(0, 0, n), peak_.load() * 0.9f);
}

void ZirconProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree st("ZirconState");
    st.setProperty("patch", patchText_, nullptr);
    st.setProperty("program", currentProgram_, nullptr);
    for (int i = 0; i < kHostParams; ++i) st.setProperty("p" + juce::String(i + 1), hostParams_[i]->getValue(), nullptr);
    juce::MemoryOutputStream os(dest, false);
    st.writeToStream(os);
}

void ZirconProcessor::setStateInformation(const void* data, int size) {
    auto st = juce::ValueTree::readFromData(data, (size_t)size);
    if (!st.isValid()) return;
    currentProgram_ = juce::jlimit(0, std::max(0, factory_.size() - 1), (int)st.getProperty("program", 0));
    loadPatchText(st.getProperty("patch").toString());
    for (int i = 0; i < kHostParams; ++i)
        if (st.hasProperty("p" + juce::String(i + 1)))
            hostParams_[i]->setValueNotifyingHost(st.getProperty("p" + juce::String(i + 1)));
}

juce::AudioProcessorEditor* ZirconProcessor::createEditor() { return new ZirconEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZirconProcessor(); }
