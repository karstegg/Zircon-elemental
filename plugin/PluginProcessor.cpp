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
    static const char* fxNames[kFxParams] = {"Delay Time", "Delay Feedback", "Delay Mix", "Reverb Size", "Reverb Damping", "Reverb Mix"};
    static const float fxDefaults[kFxParams] = {0.45f, 0.35f, 0.0f, 0.5f, 0.5f, 0.12f};
    for (int i = 0; i < kFxParams; ++i) {
        auto* p = new juce::AudioParameterFloat(juce::ParameterID{"fx" + juce::String(i + 1), 1}, fxNames[i],
                                                juce::NormalisableRange<float>(0.f, 1.f), fxDefaults[i]);
        fxParams_[(size_t)i] = p;
        addParameter(p);
    }
    // embedded macro library + factory patches
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        juce::String text(juce::CharPointer_UTF8(data), (size_t)size);
        juce::String file = BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[i]);
        auto parsed = juce::JSON::parse(text);
        bool isPatch = parsed.hasProperty("out");
        if (isPatch && factoryCart_.empty()) factoryCart_ = zircon::dx7::cartFromPatchJson(text.toStdString());
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
    reverb_.setSampleRate(sr);
    reverb_.reset();
    for (auto& b : dlBuf_) b.assign((size_t)(sr * 1.2) + 8, 0.f);
    dlPos_ = 0;
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
    {
        const juce::SpinLock::ScopedLockType sl(lock_);
        dx7Active_ = engine_ && engine_->numDx7() > 0;
        if (dx7Active_ && cart_.empty()) { cart_ = factoryCart_; cartIndex_ = 0; }
        if (dx7Active_) {
            padCart();
            if (pendingDx7_.isEmpty()) engine_->setDx7Voice(0, cart_[(size_t)cartIndex_]);   // instrument keeps its current cartridge voice
        }
        if (dx7Active_ && pendingDx7_.isNotEmpty()) {
            std::vector<int> vals;
            for (auto& t : juce::StringArray::fromTokens(pendingDx7_, " ", "")) vals.push_back(t.getIntValue());
            zircon::dx7::Voice v;
            if (zircon::dx7::fromUnpacked(vals, v)) engine_->setDx7Voice(0, v);
        }
        pendingDx7_ = {};
    }
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
    cart_ = voices;
    padCart();
    cartIndex_ = 0;
    if (!dx7Active_) {
        // switch to the DX7 instrument first
        for (int i = 0; i < factory_.size(); ++i)
            if (factory_[i].name.startsWith("DX7")) { currentProgram_ = i; loadPatchText(factory_[i].json); break; }
    }
    lastError_ = {};
    setDx7Voice(cart_[0]);
    if (onCartChanged) juce::MessageManager::callAsync([this] { if (onCartChanged) onCartChanged(); });
    return {};
}

juce::String ZirconProcessor::saveSysex(const juce::File& f) {
    if (cart_.empty()) return "no cartridge";
    auto bytes = zircon::dx7::toSysex(cart_);
    return f.replaceWithData(bytes.data(), bytes.size()) ? juce::String() : juce::String("could not write file");
}

void ZirconProcessor::padCart() {
    if (cart_.size() > 32) cart_.resize(32);
    while (cart_.size() < 32) { zircon::dx7::Voice v; v.op[0].ol = 99; for (int i = 1; i < 6; ++i) v.op[i].ol = 0; cart_.push_back(v); }
    cartIndex_ = juce::jlimit(0, (int)cart_.size() - 1, cartIndex_);
}

zircon::dx7::Voice ZirconProcessor::dx7Voice() {
    const juce::SpinLock::ScopedLockType sl(lock_);
    return engine_ && engine_->numDx7() > 0 ? engine_->dx7Voice(0) : zircon::dx7::Voice();
}

void ZirconProcessor::setDx7Voice(const zircon::dx7::Voice& v) {
    const juce::SpinLock::ScopedLockType sl(lock_);
    if (engine_) engine_->setDx7Voice(0, v);
}

juce::String ZirconProcessor::cartName(int i) const {
    if (i < 0 || i >= (int)cart_.size()) return {};
    return juce::String(i + 1).paddedLeft('0', 2) + "  " + juce::String(cart_[(size_t)i].name).trimEnd();
}

void ZirconProcessor::storeToCart() {
    cart_[(size_t)juce::jlimit(0, (int)cart_.size() - 1, cartIndex_)] = dx7Voice();
}

void ZirconProcessor::selectCartVoice(int i) {
    if (i < 0 || i >= (int)cart_.size()) return;
    cartIndex_ = i;
    setDx7Voice(cart_[(size_t)i]);
}

void ZirconProcessor::initDx7Voice() {
    zircon::dx7::Voice v;
    v.op[0].ol = 99;
    for (int i = 1; i < 6; ++i) v.op[i].ol = 0;
    setDx7Voice(v);
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
    applyFx(L, R, n);
    voicesActive_ = eng.activeVoices();
    peak_ = std::max(buffer.getMagnitude(0, 0, n), peak_.load() * 0.9f);
}

void ZirconProcessor::applyFx(float* L, float* R, int n) {
    const float dTime = 0.02f * std::pow(50.f, fxParams_[0]->getValue());   // 20 ms .. 1 s
    const float dFb = fxParams_[1]->getValue() * 0.9f;
    const float dMix = fxParams_[2]->getValue();
    if (!dlBuf_[0].empty() && (dMix > 0.001f || dlTime_ > 0.f)) {
        const int size = (int)dlBuf_[0].size();
        const float sr = (float)sampleRate_;
        for (int i = 0; i < n; ++i) {
            dlTime_ += (dTime - dlTime_) * 0.0005f;
            float rp = (float)dlPos_ - dlTime_ * sr;
            while (rp < 0) rp += (float)size;
            const int i0 = (int)rp % size, i1 = (i0 + 1) % size;
            const float fr = rp - std::floor(rp);
            const float dl = dlBuf_[0][(size_t)i0] * (1 - fr) + dlBuf_[0][(size_t)i1] * fr;
            const float dr = dlBuf_[1][(size_t)i0] * (1 - fr) + dlBuf_[1][(size_t)i1] * fr;
            // ping-pong: left feeds right and vice versa
            dlBuf_[0][(size_t)dlPos_] = L[i] + dr * dFb;
            dlBuf_[1][(size_t)dlPos_] = R[i] + dl * dFb;
            L[i] += dl * dMix;
            R[i] += dr * dMix;
            if (++dlPos_ >= size) dlPos_ = 0;
        }
    }
    const float rMix = fxParams_[5]->getValue();
    juce::Reverb::Parameters rp;
    rp.roomSize = fxParams_[3]->getValue();
    rp.damping = fxParams_[4]->getValue();
    rp.wetLevel = rMix * 0.5f;
    rp.dryLevel = 1.f - rMix * 0.3f;
    rp.width = 1.f;
    reverb_.setParameters(rp);
    reverb_.processStereo(L, R, n);
}

void ZirconProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree st("ZirconState");
    st.setProperty("patch", patchText_, nullptr);
    st.setProperty("program", currentProgram_, nullptr);
    if (dx7Active_) {
        juce::String t;
        for (int x : zircon::dx7::toUnpacked(dx7Voice())) t << x << " ";
        st.setProperty("dx7voice", t, nullptr);
        st.setProperty("cartidx", cartIndex_, nullptr);
        st.setProperty("cart", juce::Base64::toBase64(zircon::dx7::toSysex(cart_).data(), 4104), nullptr);
    }
    for (int i = 0; i < kHostParams; ++i) st.setProperty("p" + juce::String(i + 1), hostParams_[i]->getValue(), nullptr);
    for (int i = 0; i < kFxParams; ++i) st.setProperty("fx" + juce::String(i + 1), fxParams_[(size_t)i]->getValue(), nullptr);
    juce::MemoryOutputStream os(dest, false);
    st.writeToStream(os);
}

void ZirconProcessor::setStateInformation(const void* data, int size) {
    auto st = juce::ValueTree::readFromData(data, (size_t)size);
    if (!st.isValid()) return;
    currentProgram_ = juce::jlimit(0, std::max(0, factory_.size() - 1), (int)st.getProperty("program", 0));
    if (st.hasProperty("cart")) {
        juce::MemoryOutputStream mo;
        if (juce::Base64::convertFromBase64(mo, st.getProperty("cart").toString())) {
            auto* d = (const uint8_t*)mo.getData();
            auto vs = zircon::dx7::parseSysex(std::vector<uint8_t>(d, d + mo.getDataSize()));
            if (!vs.empty()) cart_ = vs;
        }
        cartIndex_ = juce::jlimit(0, std::max(0, (int)cart_.size() - 1), (int)st.getProperty("cartidx", 0));
    }
    pendingDx7_ = st.getProperty("dx7voice").toString();
    loadPatchText(st.getProperty("patch").toString());
    for (int i = 0; i < kFxParams; ++i)
        if (st.hasProperty("fx" + juce::String(i + 1)))
            fxParams_[(size_t)i]->setValueNotifyingHost(st.getProperty("fx" + juce::String(i + 1)));
    for (int i = 0; i < kHostParams; ++i)
        if (st.hasProperty("p" + juce::String(i + 1)))
            hostParams_[i]->setValueNotifyingHost(st.getProperty("p" + juce::String(i + 1)));
}

juce::AudioProcessorEditor* ZirconProcessor::createEditor() { return new ZirconEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZirconProcessor(); }
