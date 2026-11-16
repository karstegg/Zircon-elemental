// Zircon Elemental — DX7-style 6-operator FM voice ("dx7" module).
//
// A clean-room implementation of the classic 6-operator, 32-algorithm FM architecture:
// per-operator 4-stage envelope (R1-4/L1-4), output level, velocity sensitivity,
// keyboard level scaling (breakpoint, left/right depth and curve), rate scaling,
// amp-mod sensitivity, ratio/fixed frequency with fine + detune, operator feedback,
// pitch envelope, LFO (6 waves, delay, key sync, pitch + amp depth) and transpose.
//
// It reads the standard DX7 voice parameter set (155 params, or the 128-byte packed
// SysEx bulk format), so cartridges (.syx) load directly. The envelope/level curves
// are *approximations* of the hardware's (calibrated by ear/measurement), not a
// bit-exact emulation; voices will sound very close but not identical.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace zircon::dx7 {

struct OpParams {
    int rate[4] = {99, 99, 99, 99};
    int level[4] = {99, 99, 99, 0};
    int bp = 39;            // breakpoint 0..99 (0 = A-1)
    int ld = 0, rd = 0;     // left/right depth 0..99
    int lc = 0, rc = 0;     // curves: 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN
    int rs = 0;             // rate scaling 0..7
    int ams = 0;            // amp mod sens 0..3
    int kvs = 0;            // key velocity sens 0..7
    int ol = 99;            // output level 0..99
    int mode = 0;           // 0 ratio, 1 fixed
    int fc = 1, ff = 0;     // coarse 0..31, fine 0..99
    int det = 7;            // detune 0..14 (7 = centre)
};

struct Voice {
    OpParams op[6];         // op[0] = operator 1 ... op[5] = operator 6
    int prate[4] = {99, 99, 99, 99};
    int plevel[4] = {50, 50, 50, 50};
    int algo = 0;           // 0..31
    int fb = 0;             // 0..7
    int oks = 1;            // oscillator key sync
    int lfs = 35, lfd = 0, lpmd = 0, lamd = 0;
    int lks = 1;            // LFO key sync
    int lfw = 0;            // 0 tri, 1 saw down, 2 saw up, 3 square, 4 sine, 5 S&H
    int lpms = 3;           // pitch mod sens 0..7
    int trnsp = 24;         // 0..48, 24 = C3 (no shift)
    std::string name = "INIT VOICE";
};

constexpr int kUnpackedSize = 155;
constexpr int kPackedSize = 128;

bool fromUnpacked(const std::vector<int>& v, Voice& out);     // 145 values + optional 10 name chars
bool fromPacked(const uint8_t* p128, Voice& out);
std::vector<int> toUnpacked(const Voice&);                    // 155 values
// Parses a DX7 SysEx dump: 32-voice bulk (4104 bytes) or a single-voice dump. Empty on failure.
std::vector<Voice> parseSysex(const std::vector<uint8_t>& bytes);
// 32-voice bulk dump (4104 bytes) of up to 32 voices (padded with INIT VOICE).
std::vector<uint8_t> toSysex(const std::vector<Voice>& voices);
void toPacked(const Voice&, uint8_t* p128);
// Complete Zircon patch (JSON text) that plays this voice, with Level/Bright/EG Speed/Vibrato knobs.
std::string patchJson(const Voice&, int polyphony = 8);

// Algorithm routing (operators 0-based).
struct Algo {
    uint8_t carriers;       // bitmask
    uint8_t mods[6];        // mods[i] = bitmask of operators that modulate operator i
    int8_t fbSrc, fbDst;    // feedback: output of fbSrc (delayed, averaged) modulates fbDst
};
const Algo& algorithm(int index);   // 0..31

// One voice lane's runtime state.
class Lane {
public:
    Lane() { reset(); }
    void reset();
    // Called every sample. pitch = MIDI note (pitch bend applied), gate edges start/release notes.
    float tick(const Voice& v, float pitch, bool gate, float vel, float modWheel,
               float level, float bright, float egSpeed, float vibrato, float sr);
    bool busy() const { return held_ || audible_; }

private:
    struct Eg { float cur = 0.f; int stage = 5; };
    void noteOn(const Voice&, float pitch, float vel);
    void noteOff(const Voice&);
    void control(const Voice&, float pitch, float modWheel, float egSpeed, float vibrato, float sr);

    static constexpr int kBlock = 16;
    Eg eg_[6];
    float ph_[6], y_[6], amp_[6], ampTarget_[6], ampSlope_[6], inc_[6];
    float fbHist_[2];
    float noteAtt_[6];          // keyboard level scaling + velocity (DX units), fixed per note
    int rateScale_[6];
    float lfoPhase_, lfoTime_, lfoSh_;
    float pegCur_; int pegStage_;
    int note_;
    int counter_;
    bool held_, audible_, gatePrev_;
};

} // namespace zircon::dx7
