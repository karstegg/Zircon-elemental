// Zircon Elemental — runtime engine.
// Runs a compiled Program sample by sample. Every register holds one
// float per voice lane, so each instruction is a tight loop across voices
// (auto-vectorised by the compiler; explicit SIMD later).
#pragma once
#include "zircon/compiler.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace zircon {

class Engine {
public:
    explicit Engine(Program p);

    void prepare(double sampleRate);
    void reset();

    void noteOn(int note, float velocity);   // velocity 0..1
    void noteOff(int note);
    void allNotesOff();
    void setModWheel(float v01) { mod_ = v01; }   // CC1, 0..1
    void setSustain(bool on);                // CC64 sustain pedal
    void setPitchBend(float semitones) { bend_ = semitones; }

    int numParams() const { return (int)prog_.params.size(); }
    const ParamInfo& paramInfo(int i) const { return prog_.params[i]; }
    void setParam(int i, float value);              // real units
    void setParamNormalized(int i, float x01);
    int findParam(const std::string& name) const;

    // Renders n stereo samples (overwrites buffers).
    void process(float* left, float* right, int n);

    int activeVoices() const;
    const Program& program() const { return prog_; }

private:
    struct Voice { int note = -1; bool gate = false; bool active = false; uint64_t age = 0; float vel = 0; bool sus = false; bool retrig = false; };
    void runSample();
    bool laneIdle(int lane) const;
    void clearLane(int lane);   // zero a lane's state/delay lines when a voice starts on a free lane

    Program prog_;
    double sr_ = 48000.0;
    int lanes_ = 8;
    std::vector<float> regs_;    // numRegs * kMaxLanes
    std::vector<float> state_;   // stateFloats * kMaxLanes
    std::vector<uint32_t> rng_;
    struct DelayBuf { std::vector<float> buf; int size = 0; int pos = 0; };
    std::vector<DelayBuf> delays_;   // delays * lanes
    std::vector<float> paramTarget_, paramValue_;
    float paramCoef_ = 0.f;
    std::vector<Voice> voices_;
    uint64_t clock_ = 0;
    float bend_ = 0.f;
    bool sustain_ = false;
    float mod_ = 0.f;
};

} // namespace zircon
