// Zircon Elemental — patch compiler.
// Turns a JSON patch (modules + wires + macros) into a flat, ordered
// program of register instructions that the Engine runs per sample,
// vectorised across voices.
#pragma once
#include "zircon/ops.hpp"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace zircon {

constexpr int kMaxLanes = 16;   // max polyphony (voice lanes)
constexpr int kMaxIn = 5;

struct CompileError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Instr {
    Op op;
    int in[kMaxIn] = {-1, -1, -1, -1, -1};
    int out[3] = {-1, -1, -1};
    int state = -1;     // offset into per-lane float state
    int slot = -1;      // param index / delay index
    float p0 = 0.f;     // const value / misc
    std::string node;   // originating node id (debug)
};

struct ParamInfo {
    std::string name;
    float min = 0.f, max = 1.f, def = 0.5f;
    bool expCurve = false;
    float fromNormalized(float x) const;
    float toNormalized(float v) const;
};

struct DelayInfo { float maxSeconds = 1.f; };

struct Program {
    std::string name;
    int voices = 8;
    int numRegs = 0;
    int stateFloats = 0;                 // per lane
    std::vector<Instr> code;
    std::vector<std::pair<int, float>> constRegs;   // register, value
    std::vector<ParamInfo> params;
    std::vector<DelayInfo> delays;
    int regSampleRate = -1, regPitch = -1, regGate = -1, regVel = -1;
    std::vector<int> paramRegs;          // per param
    std::vector<int> adsrStates;         // state offsets of ADSRs (voice-free detection)
    int outL = -1, outR = -1;
    std::vector<std::string> order;      // node evaluation order (debug)
};

class MacroLibrary {
public:
    void loadDirectory(const std::string& dir);   // *.json, macro name = "name" field or file stem
    void addMacro(const std::string& name, const std::string& json);
    bool has(const std::string& name) const { return macros_.count(name) != 0; }
    const std::string& get(const std::string& name) const { return macros_.at(name); }
    std::vector<std::string> names() const;
private:
    std::map<std::string, std::string> macros_;   // name -> json text
};

Program compilePatch(const std::string& patchJson, const MacroLibrary& lib);
Program compilePatchFile(const std::string& path, const MacroLibrary& lib);

} // namespace zircon
