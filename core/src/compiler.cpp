#include "zircon/compiler.hpp"
#include "json.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>

using nlohmann::json;
namespace fs = std::filesystem;

namespace zircon {

// ---------------------------------------------------------------- params
float ParamInfo::fromNormalized(float x) const {
    x = std::fmin(1.f, std::fmax(0.f, x));
    if (expCurve && min > 0.f && max > 0.f) return min * std::pow(max / min, x);
    return min + (max - min) * x;
}
float ParamInfo::toNormalized(float v) const {
    if (expCurve && min > 0.f && max > 0.f) return std::log(std::fmax(v, min) / min) / std::log(max / min);
    return max == min ? 0.f : (v - min) / (max - min);
}

// ---------------------------------------------------------------- library
void MacroLibrary::addMacro(const std::string& name, const std::string& text) { macros_[name] = text; }

void MacroLibrary::loadDirectory(const std::string& dir) {
    if (!fs::exists(dir)) return;
    for (auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.path().extension() != ".json") continue;
        std::ifstream f(e.path());
        std::stringstream ss; ss << f.rdbuf();
        auto j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded()) throw CompileError("macro file is not valid JSON: " + e.path().string());
        std::string name = j.value("name", e.path().stem().string());
        macros_[name] = ss.str();
    }
}

std::vector<std::string> MacroLibrary::names() const {
    std::vector<std::string> v;
    for (auto& [k, _] : macros_) v.push_back(k);
    return v;
}

// ---------------------------------------------------------------- flatten
namespace {

struct FlatNode {
    std::string id, type;
    json params = json::object();
    std::map<std::string, float> inConst;   // unwired input constants
    float thruDefault = 0.f;                // for "thru" (macro ports)
};
struct FlatWire { std::string srcNode, srcPort, dstNode, dstPort; };

struct Flattener {
    const MacroLibrary& lib;
    std::map<std::string, json> localMacros;
    std::vector<FlatNode> nodes;
    std::vector<FlatWire> wires;
    std::set<std::string> ids;
    int depth = 0;

    const json* macroDef(const std::string& type, json& storage) {
        auto it = localMacros.find(type);
        if (it != localMacros.end()) return &it->second;
        if (lib.has(type)) { storage = json::parse(lib.get(type)); return &storage; }
        return nullptr;
    }

    static std::pair<std::string, std::string> split(const std::string& ref) {
        auto p = ref.find('.');
        if (p == std::string::npos) return {ref, ""};
        return {ref.substr(0, p), ref.substr(p + 1)};
    }

    void addNode(FlatNode n) {
        if (!ids.insert(n.id).second) throw CompileError("duplicate module id: " + n.id);
        nodes.push_back(std::move(n));
    }

    // Expand one graph level. prefix is "" at top or "inst/" inside a macro.
    void expand(const json& g, const std::string& prefix, bool isMacro) {
        if (++depth > 32) throw CompileError("macro nesting too deep (recursive macro?) at " + prefix);
        std::map<std::string, json> instMacro;    // local id -> macro def
        if (g.contains("macros"))
            for (auto& [k, v] : g["macros"].items()) localMacros[k] = v;

        for (auto& m : g.value("modules", json::array())) {
            std::string id = m.at("id").get<std::string>();
            std::string type = m.at("type").get<std::string>();
            if (id.find('.') != std::string::npos || id.find('/') != std::string::npos)
                throw CompileError("module id may not contain '.' or '/': " + id);
            json storage;
            if (findOp(type)) {
                FlatNode n{prefix + id, type, m.value("params", json::object()), {}};
                if (m.contains("in"))
                    for (auto& [k, v] : m["in"].items()) n.inConst[k] = v.get<float>();
                addNode(std::move(n));
            } else if (const json* md = macroDef(type, storage)) {
                json def = *md;
                instMacro[id] = def;
                std::string ip = prefix + id + "/";
                // macro input ports -> thru nodes
                for (auto& in : def.value("inputs", json::array())) {
                    std::string nm = in.is_string() ? in.get<std::string>() : in.at("name").get<std::string>();
                    float dv = in.is_object() ? in.value("default", 0.f) : 0.f;
                    if (m.contains("in") && m["in"].contains(nm)) dv = m["in"][nm].get<float>();
                    FlatNode t{ip + "$" + nm, "thru", json::object(), {}, dv};
                    addNode(std::move(t));
                }
                // instance params override internal module params: "params": {"inner.param": v}
                if (m.contains("params"))
                    for (auto& [k, v] : m["params"].items()) {
                        auto [inner, pk] = split(k);
                        for (auto& im : def["modules"])
                            if (im.at("id") == inner) im["params"][pk] = v;
                    }
                expand(def, ip, true);
                json outsDef = def.value("outputs", json::object());
                for (auto& [oname, oref] : outsDef.items()) {
                    FlatNode t{ip + "@" + oname, "thru", json::object(), {}, 0.f};
                    addNode(std::move(t));
                    auto [sn, sp] = resolveSrc(oref.get<std::string>(), ip, def);
                    wires.push_back({sn, sp, ip + "@" + oname, "in"});
                }
            } else {
                throw CompileError("unknown module type '" + type + "' (module " + prefix + id + ")");
            }
        }

        for (auto& w : g.value("wires", json::array())) {
            std::string s, d;
            if (w.is_array()) { s = w.at(0); d = w.at(1); }
            else { s = w.at("from"); d = w.at("to"); }
            auto [sn, sp] = resolveSrcLocal(s, prefix, instMacro, isMacro);
            auto [dn, dp] = resolveDst(d, prefix, instMacro);
            wires.push_back({sn, sp, dn, dp});
        }
        --depth;
    }

    // source reference as seen from inside macro def `def` with prefix ip (used for macro outputs)
    std::pair<std::string, std::string> resolveSrc(const std::string& ref, const std::string& ip, const json& def) {
        std::map<std::string, json> im;
        json storage;
        for (auto& m : def.value("modules", json::array())) {
            std::string type = m.at("type");
            if (!findOp(type)) if (const json* md = macroDef(type, storage)) im[m.at("id")] = *md;
        }
        return resolveSrcLocal(ref, ip, im, true);
    }

    std::pair<std::string, std::string> resolveSrcLocal(const std::string& ref, const std::string& prefix,
                                                        const std::map<std::string, json>& instMacro, bool isMacro) {
        if (!ref.empty() && ref[0] == '$') {
            if (!isMacro) throw CompileError("'$' input reference used outside a macro: " + ref);
            return {prefix + ref, "out"};
        }
        auto [n, p] = split(ref);
        if (instMacro.count(n)) {
            if (p.empty()) {
                json o = instMacro.at(n).value("outputs", json::object());
                if (o.empty()) throw CompileError("macro instance has no outputs: " + ref);
                p = o.begin().key();
            }
            return {prefix + n + "/@" + p, "out"};
        }
        return {prefix + n, p.empty() ? "out" : p};
    }

    std::pair<std::string, std::string> resolveDst(const std::string& ref, const std::string& prefix,
                                                   const std::map<std::string, json>& instMacro) {
        auto [n, p] = split(ref);
        if (instMacro.count(n)) {
            if (p.empty()) throw CompileError("wire into macro instance needs a port: " + ref);
            return {prefix + n + "/$" + p, "in"};
        }
        return {prefix + n, p};
    }
};

struct Src { int node = -1; int port = 0; float k = 0.f; };   // node<0 => constant k

} // namespace

// ---------------------------------------------------------------- compile
Program compilePatch(const std::string& text, const MacroLibrary& lib) {
    json patch = json::parse(text, nullptr, false);
    if (patch.is_discarded()) throw CompileError("patch is not valid JSON");

    Flattener fl{lib, {}, {}, {}, {}, 0};
    fl.expand(patch, "", false);

    // output sinks as thru nodes
    json outs = patch.value("out", json());
    std::map<std::string, std::string> outRefs;
    if (outs.is_string()) outRefs = {{"L", outs}, {"R", outs}};
    else if (outs.is_object()) for (auto& [k, v] : outs.items()) outRefs[k] = v;
    else throw CompileError("patch needs an \"out\" (string or {L,R})");
    if (!outRefs.count("R") && outRefs.count("L")) outRefs["R"] = outRefs["L"];
    for (auto& [ch, ref] : outRefs) {
        fl.addNode({"#out" + ch, "thru", json::object(), {}, 0.f});
        std::map<std::string, json> im;
        json storage;
        for (auto& m : patch.value("modules", json::array())) {
            std::string type = m.at("type");
            if (!findOp(type)) if (const json* md = fl.macroDef(type, storage)) im[m.at("id")] = *md;
        }
        auto [sn, sp] = fl.resolveSrcLocal(ref, "", im, false);
        fl.wires.push_back({sn, sp, "#out" + ch, "in"});
    }

    // index nodes
    std::unordered_map<std::string, int> idx;
    for (int i = 0; i < (int)fl.nodes.size(); ++i) idx[fl.nodes[i].id] = i;
    auto specOf = [&](int i) -> const OpSpec* { return findOp(fl.nodes[i].type); };

    // incoming wires: node -> input port index -> list of raw sources (node, outPortIdx)
    std::vector<std::vector<std::vector<std::pair<int, int>>>> in(fl.nodes.size());
    for (int i = 0; i < (int)fl.nodes.size(); ++i) {
        const OpSpec* s = specOf(i);
        in[i].resize(s ? s->inputs.size() : 1);
    }
    for (auto& w : fl.wires) {
        if (!idx.count(w.srcNode)) throw CompileError("wire from unknown module: " + w.srcNode);
        if (!idx.count(w.dstNode)) throw CompileError("wire to unknown module: " + w.dstNode);
        int sn = idx[w.srcNode], dn = idx[w.dstNode];
        int sp = 0;
        if (const OpSpec* s = specOf(sn)) {
            auto it = std::find(s->outputs.begin(), s->outputs.end(), w.srcPort);
            if (it == s->outputs.end()) throw CompileError("module " + w.srcNode + " has no output '" + w.srcPort + "'");
            sp = int(it - s->outputs.begin());
        }
        int dp = 0;
        if (const OpSpec* s = specOf(dn)) {
            if (w.dstPort.empty()) {
                if (s->inputs.empty()) throw CompileError("module " + w.dstNode + " has no inputs");
            } else {
                auto it = std::find_if(s->inputs.begin(), s->inputs.end(), [&](auto& p) { return p.name == w.dstPort; });
                if (it == s->inputs.end()) throw CompileError("module " + w.dstNode + " has no input '" + w.dstPort + "'");
                dp = int(it - s->inputs.begin());
            }
        }
        in[dn][dp].push_back({sn, sp});
    }

    // resolve thru chains -> real sources (constants for unwired macro inputs)
    std::function<void(int, std::vector<Src>&, int)> expandThru = [&](int t, std::vector<Src>& outv, int guard) {
        if (guard > 256) throw CompileError("macro port loop at " + fl.nodes[t].id);
        if (in[t][0].empty()) { outv.push_back({-1, 0, fl.nodes[t].thruDefault}); return; }
        for (auto [sn, sp] : in[t][0]) {
            if (!specOf(sn)) expandThru(sn, outv, guard + 1);
            else outv.push_back({sn, sp, 0.f});
        }
    };
    auto sourcesOf = [&](int node, int port) {
        std::vector<Src> v;
        for (auto [sn, sp] : in[node][port]) {
            if (!specOf(sn)) expandThru(sn, v, 0);
            else v.push_back({sn, sp, 0.f});
        }
        return v;
    };

    // topological order of real nodes; breakers' outputs have no deps
    std::vector<int> order, mark(fl.nodes.size(), 0);
    std::vector<std::string> stack;
    std::function<void(int)> visit = [&](int n) {
        if (mark[n] == 2) return;
        if (mark[n] == 1) {
            std::string path;
            for (auto& s : stack) path += s + " -> ";
            throw CompileError("feedback loop without delay: " + path + fl.nodes[n].id +
                               "  (insert a z1 or delay to close the loop)");
        }
        mark[n] = 1; stack.push_back(fl.nodes[n].id);
        const OpSpec* s = specOf(n);
        if (!s->breaker)
            for (int p = 0; p < (int)s->inputs.size(); ++p)
                for (auto& src : sourcesOf(n, p)) if (src.node >= 0) visit(src.node);
        stack.pop_back(); mark[n] = 2;
        order.push_back(n);
    };
    for (int i = 0; i < (int)fl.nodes.size(); ++i) if (specOf(i)) visit(i);
    // breakers' inputs must also be computed: visit their sources
    for (int i = 0; i < (int)fl.nodes.size(); ++i)
        if (specOf(i) && specOf(i)->breaker)
            for (int p = 0; p < (int)specOf(i)->inputs.size(); ++p)
                for (auto& src : sourcesOf(i, p)) if (src.node >= 0) visit(src.node);

    // ---- emit
    Program prog;
    prog.name = patch.value("name", "untitled");
    prog.voices = std::clamp(patch.value("voices", 8), 1, kMaxLanes);
    std::vector<std::array<int, 3>> outReg(fl.nodes.size(), {-1, -1, -1});
    std::map<float, int> constReg;
    auto newReg = [&] { return prog.numRegs++; };
    auto kReg = [&](float v) {
        auto it = constReg.find(v);
        if (it != constReg.end()) return it->second;
        int r = newReg(); constReg[v] = r; prog.constRegs.push_back({r, v}); return r;
    };
    // pre-allocate output registers
    for (int n : order) {
        const OpSpec* s = specOf(n);
        for (int o = 0; o < (int)s->outputs.size(); ++o) outReg[n][o] = newReg();
    }
    auto inputReg = [&](int n, int p, float def) {
        auto v = sourcesOf(n, p);
        if (v.empty()) {
            auto it = fl.nodes[n].inConst.find(specOf(n)->inputs[p].name);
            return kReg(it != fl.nodes[n].inConst.end() ? it->second : def);
        }
        auto regOf = [&](const Src& s) { return s.node < 0 ? kReg(s.k) : outReg[s.node][s.port]; };
        int acc = regOf(v[0]);
        for (size_t i = 1; i < v.size(); ++i) {   // multiple wires into one input are summed
            Instr a; a.op = Op::Add; a.in[0] = acc; a.in[1] = regOf(v[i]); a.out[0] = newReg();
            a.node = fl.nodes[n].id + ":sum";
            prog.code.push_back(a); acc = a.out[0];
        }
        return acc;
    };

    std::map<std::string, int> paramIdx;
    std::vector<Instr> writes;
    for (int n : order) {
        const OpSpec* s = specOf(n);
        const FlatNode& fn = fl.nodes[n];
        prog.order.push_back(fn.id);
        Instr I; I.op = s->op; I.node = fn.id;
        for (int o = 0; o < (int)s->outputs.size(); ++o) I.out[o] = outReg[n][o];

        switch (s->op) {
        case Op::SampleRate: if (prog.regSampleRate < 0) prog.regSampleRate = I.out[0]; else I.in[0] = prog.regSampleRate; break;
        case Op::Pitch: if (prog.regPitch < 0) prog.regPitch = I.out[0]; else I.in[0] = prog.regPitch; break;
        case Op::Gate: if (prog.regGate < 0) prog.regGate = I.out[0]; else I.in[0] = prog.regGate; break;
        case Op::Velocity: if (prog.regVel < 0) prog.regVel = I.out[0]; else I.in[0] = prog.regVel; break;
        case Op::Sustain: if (prog.regSustain < 0) prog.regSustain = I.out[0]; else I.in[0] = prog.regSustain; break;
        case Op::ModWheel: if (prog.regModWheel < 0) prog.regModWheel = I.out[0]; else I.in[0] = prog.regModWheel; break;
        default: break;
        }
        // context duplicates become copies of the canonical register
        bool ctx = s->op == Op::SampleRate || s->op == Op::Pitch || s->op == Op::Gate || s->op == Op::Velocity || s->op == Op::Sustain || s->op == Op::ModWheel;
        if (ctx) {
            if (I.in[0] >= 0) { I.op = Op::Add; I.in[1] = kReg(0.f); prog.code.push_back(I); }
            continue;
        }
        if (s->op == Op::Const) { I.p0 = fn.params.value("value", 0.f); prog.code.push_back(I); continue; }
        if (s->op == Op::Param) {
            std::string pname = fn.params.value("name", fn.id);
            if (!paramIdx.count(pname)) {
                ParamInfo pi;
                pi.name = pname;
                pi.min = fn.params.value("min", 0.f);
                pi.max = fn.params.value("max", 1.f);
                pi.def = fn.params.value("default", pi.min);
                pi.expCurve = fn.params.value("curve", "lin") == "exp";
                paramIdx[pname] = (int)prog.params.size();
                prog.params.push_back(pi);
                prog.paramRegs.push_back(I.out[0]);
                continue;   // engine writes this register directly
            }
            // duplicate param node: copy
            I.op = Op::Add; I.in[0] = prog.paramRegs[paramIdx[pname]]; I.in[1] = kReg(0.f);
            prog.code.push_back(I); continue;
        }

        if (s->op == Op::Z1) {
            I.state = prog.stateFloats; prog.stateFloats += 1;
            prog.code.push_back(I);
            Instr W; W.op = Op::Z1Write; W.state = I.state; W.node = fn.id + ":write"; W.in[0] = -2; // fill later
            writes.push_back(W);
            writes.back().slot = n;
            continue;
        }
        if (s->op == Op::Delay) {
            I.slot = (int)prog.delays.size();
            I.state = prog.stateFloats; prog.stateFloats += 1;   // last delay time (read uses previous sample's time)
            prog.delays.push_back({std::fmax(0.001f, fn.params.value("max", 1.f))});
            prog.code.push_back(I);
            Instr W; W.op = Op::DelayWrite; W.slot = I.slot; W.state = I.state; W.node = fn.id + ":write";
            W.p0 = (float)n;  // node index stash
            writes.push_back(W);
            continue;
        }
        if (s->op == Op::Noise) { I.state = prog.stateFloats; prog.stateFloats += 1; }
        if (s->op == Op::Adsr) { I.state = prog.stateFloats; prog.stateFloats += 3; prog.adsrStates.push_back(I.state); }
        if (s->op == Op::Svf) { I.state = prog.stateFloats; prog.stateFloats += 2; }

        for (int p = 0; p < (int)s->inputs.size(); ++p) I.in[p] = inputReg(n, p, s->inputs[p].def);
        prog.code.push_back(I);
    }
    // breaker writes go last, after every input has been computed this sample
    for (auto& W : writes) {
        int n = W.op == Op::Z1Write ? W.slot : (int)W.p0;
        if (W.op == Op::Z1Write) W.slot = -1;
        const OpSpec* s = specOf(n);
        for (int p = 0; p < (int)s->inputs.size(); ++p) W.in[p] = inputReg(n, p, s->inputs[p].def);
        prog.code.push_back(W);
    }
    // outputs
    for (auto& [ch, _] : outRefs) {
        int t = idx["#out" + ch];
        std::vector<Src> v; expandThru(t, v, 0);
        int acc = v[0].node < 0 ? kReg(v[0].k) : outReg[v[0].node][v[0].port];
        for (size_t i = 1; i < v.size(); ++i) {
            Instr a; a.op = Op::Add; a.in[0] = acc;
            a.in[1] = v[i].node < 0 ? kReg(v[i].k) : outReg[v[i].node][v[i].port];
            a.out[0] = newReg(); prog.code.push_back(a); acc = a.out[0];
        }
        if (ch == "L") prog.outL = acc; else if (ch == "R") prog.outR = acc;
    }
    if (prog.regSampleRate < 0) prog.regSampleRate = newReg();
    if (prog.regPitch < 0) prog.regPitch = newReg();
    if (prog.regGate < 0) prog.regGate = newReg();
    if (prog.regVel < 0) prog.regVel = newReg();
    if (prog.regSustain < 0) prog.regSustain = newReg();
    if (prog.regModWheel < 0) prog.regModWheel = newReg();
    return prog;
}

Program compilePatchFile(const std::string& path, const MacroLibrary& lib) {
    std::ifstream f(path);
    if (!f) throw CompileError("cannot open patch: " + path);
    std::stringstream ss; ss << f.rdbuf();
    return compilePatch(ss.str(), lib);
}

} // namespace zircon
