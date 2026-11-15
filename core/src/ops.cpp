#include "zircon/ops.hpp"

namespace zircon {

const std::vector<OpSpec>& opCatalogue() {
    static const std::vector<OpSpec> cat = {
        {Op::Const, "const", {}, {"out"}, false, "Constant (param: value)", "math"},
        {Op::Add, "add", {{"a"}, {"b"}}, {"out"}, false, "a + b", "math"},
        {Op::Sub, "sub", {{"a"}, {"b"}}, {"out"}, false, "a - b", "math"},
        {Op::Mul, "mul", {{"a", 1.f}, {"b", 1.f}}, {"out"}, false, "a * b", "math"},
        {Op::Div, "div", {{"a"}, {"b", 1.f}}, {"out"}, false, "a / b (0 if b == 0)", "math"},
        {Op::Min, "min", {{"a"}, {"b"}}, {"out"}, false, "min(a, b)", "math"},
        {Op::Max, "max", {{"a"}, {"b"}}, {"out"}, false, "max(a, b)", "math"},
        {Op::Neg, "neg", {{"in"}}, {"out"}, false, "-in", "math"},
        {Op::Abs, "abs", {{"in"}}, {"out"}, false, "|in|", "math"},
        {Op::Sin, "sin", {{"in"}}, {"out"}, false, "sin(in), radians", "math"},
        {Op::Cos, "cos", {{"in"}}, {"out"}, false, "cos(in), radians", "math"},
        {Op::Tan, "tan", {{"in"}}, {"out"}, false, "tan(in), radians", "math"},
        {Op::Tanh, "tanh", {{"in"}}, {"out"}, false, "tanh(in) soft clip", "math"},
        {Op::Exp, "exp", {{"in"}}, {"out"}, false, "e^in", "math"},
        {Op::Log, "log", {{"in", 1.f}}, {"out"}, false, "ln(in), in clamped > 0", "math"},
        {Op::Pow, "pow", {{"a"}, {"b", 1.f}}, {"out"}, false, "a^b", "math"},
        {Op::Sqrt, "sqrt", {{"in"}}, {"out"}, false, "sqrt(max(in,0))", "math"},
        {Op::Floor, "floor", {{"in"}}, {"out"}, false, "floor(in)", "math"},
        {Op::Frac, "frac", {{"in"}}, {"out"}, false, "in - floor(in) (phase wrap)", "math"},
        {Op::Gt, "gt", {{"a"}, {"b"}}, {"out"}, false, "1 if a > b else 0", "logic"},
        {Op::Select, "select", {{"cond"}, {"a"}, {"b"}}, {"out"}, false, "cond > 0.5 ? a : b", "logic"},
        {Op::Clip, "clip", {{"in"}, {"lo", -1.f}, {"hi", 1.f}}, {"out"}, false, "clamp(in, lo, hi)", "math"},
        {Op::Z1, "z1", {{"in"}}, {"out"}, true, "Unit delay: out = previous in. Use to close feedback loops", "state"},
        {Op::Delay, "delay", {{"in"}, {"time", 0.01f}}, {"out"}, true,
            "Delay line, time in seconds (param: max seconds). Breaks feedback", "state"},
        {Op::SampleRate, "sr", {}, {"out"}, false, "Sample rate in Hz", "context"},
        {Op::Pitch, "pitch", {}, {"out"}, false, "Voice MIDI note number (pitch-bend applied)", "context"},
        {Op::Gate, "gate", {}, {"out"}, false, "Voice gate 1/0", "context"},
        {Op::Velocity, "velocity", {}, {"out"}, false, "Voice velocity 0..1", "context"},
        {Op::Param, "param", {}, {"out"}, false, "Host-automatable knob (params: name,min,max,default)", "context"},
        {Op::Noise, "noise", {}, {"out"}, false, "White noise -1..1", "source"},
        {Op::Mtof, "mtof", {{"in", 69.f}}, {"out"}, false, "MIDI note -> Hz", "math"},
        {Op::Adsr, "adsr", {{"gate"}, {"a", 0.01f}, {"d", 0.2f}, {"s", 0.7f}, {"r", 0.3f}}, {"out"}, false,
            "Native ADSR envelope (times in seconds)", "premade"},
        {Op::Svf, "svf", {{"in"}, {"cutoff", 1000.f}, {"res", 0.2f}}, {"lp", "bp", "hp"}, false,
            "Native zero-delay-feedback state variable filter (TPT)", "premade"},
    };
    return cat;
}

const OpSpec* findOp(const std::string& type) {
    static std::unordered_map<std::string, const OpSpec*> idx = [] {
        std::unordered_map<std::string, const OpSpec*> m;
        for (auto& s : opCatalogue()) m[s.type] = &s;
        return m;
    }();
    auto it = idx.find(type);
    return it == idx.end() ? nullptr : it->second;
}

} // namespace zircon
