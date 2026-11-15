// Zircon Elemental — operator catalogue.
// Every module type the compiler knows natively is described here: its
// input ports (with default values used when nothing is wired), output
// ports, and whether it breaks feedback loops (z1, delay).
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace zircon {

enum class Op {
    // math primitives
    Const, Add, Sub, Mul, Div, Min, Max, Neg, Abs, Sin, Cos, Tan, Tanh,
    Exp, Log, Pow, Sqrt, Floor, Frac, Gt, Select, Clip,
    // state primitives (feedback breakers)
    Z1, Delay,
    // context sources
    SampleRate, Pitch, Gate, Velocity, Sustain, ModWheel, Param, Noise,
    // conversions
    Mtof,
    // native premade modules
    Adsr, Svf,
    // internal
    Z1Write, DelayWrite
};

struct PortSpec { std::string name; float def = 0.f; };

struct OpSpec {
    Op op;
    std::string type;          // JSON type name
    std::vector<PortSpec> inputs;
    std::vector<std::string> outputs;
    bool breaker = false;      // output does not depend on current-sample inputs
    std::string doc;
    std::string category;
};

const std::vector<OpSpec>& opCatalogue();
const OpSpec* findOp(const std::string& type);

} // namespace zircon
