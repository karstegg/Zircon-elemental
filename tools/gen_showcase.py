#!/usr/bin/env python3
"""Generate the showcase synths from Elemental primitives.

  library/macros/LadderLP.json      4-pole ZDF ladder lowpass with tanh-saturated resonance feedback
  patches/09-dx-epiano.json         DX-style 4-operator FM electric piano (two op pairs, op feedback)
  patches/10-ladder-lead.json       Moog-style mono/poly lead: 2 saws + sub -> ladder, key tracking, drive

Edit this script, not the JSON.  Run:  python tools/gen_showcase.py
"""
import json, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


class P:
    """Tiny patch builder: modules + wires, with auto-numbered ids."""
    def __init__(self, name, doc, voices=8):
        self.name, self.doc, self.voices = name, doc, voices
        self.modules, self.wires = [], []

    def m(self, id, type, **kw):
        mod = {"id": id, "type": type}
        if "inn" in kw: mod["in"] = kw.pop("inn")
        if "params" in kw: mod["params"] = kw.pop("params")
        self.modules.append(mod)
        return id

    def param(self, id, name, lo, hi, default, curve=None):
        p = {"name": name, "min": lo, "max": hi, "default": default}
        if curve: p["curve"] = curve
        return self.m(id, "param", params=p)

    def w(self, src, dst):
        self.wires.append([src, dst])

    def chain(self, *ids_ports):
        for a, b in zip(ids_ports, ids_ports[1:]):
            self.w(a, b)

    def dump(self, out):
        return {"name": self.name, "doc": self.doc, "voices": self.voices,
                "modules": self.modules, "wires": self.wires, "out": out}


def write(path, obj):
    path = ROOT / path
    path.write_text(json.dumps(obj, indent=1) + "\n", encoding="utf-8")
    print("wrote", path.relative_to(ROOT))


# ---------------------------------------------------------------------------
# Ladder filter macro
# ---------------------------------------------------------------------------
ladder = {
    "name": "LadderLP",
    "doc": "Moog-style 4-pole lowpass: four cascaded ZDF one-poles, resonance feedback from the last pole "
           "through a unit delay (z1), tanh saturation at the input. res 0..~4 (self-oscillates near 4). "
           "The unit delay in the feedback path slightly detunes the resonance at very high cutoffs.",
    "inputs": [{"name": "in", "default": 0}, {"name": "cutoff", "default": 1000}, {"name": "res", "default": 0}],
    "outputs": {"out": "lp4.out"},
    "modules": [
        {"id": "fbz", "type": "z1"},
        {"id": "fbm", "type": "mul"},
        {"id": "err", "type": "sub"},
        {"id": "sat", "type": "tanh"},
        {"id": "lp1", "type": "OnePoleLP"}, {"id": "lp2", "type": "OnePoleLP"},
        {"id": "lp3", "type": "OnePoleLP"}, {"id": "lp4", "type": "OnePoleLP"},
    ],
    "wires": [
        ["$in", "err.a"], ["fbm", "err.b"], ["err", "sat.in"],
        ["sat", "lp1.in"], ["$cutoff", "lp1.cutoff"],
        ["lp1", "lp2.in"], ["$cutoff", "lp2.cutoff"],
        ["lp2", "lp3.in"], ["$cutoff", "lp3.cutoff"],
        ["lp3", "lp4.in"], ["$cutoff", "lp4.cutoff"],
        ["lp4", "fbz.in"], ["fbz", "fbm.a"], ["$res", "fbm.b"],
    ],
}
write("library/macros/LadderLP.json", ladder)


# ---------------------------------------------------------------------------
# 09  DX-style 4-op FM electric piano
#     pair A: op2 (ratio 14, fast decay "tine") -> op1 (carrier)
#     pair B: op4 (ratio 1, self-feedback "body") -> op3 (carrier)
#     velocity scales both indices, mod wheel opens the tine, outputs summed
# ---------------------------------------------------------------------------
e = P("DX Classic E-Piano",
      "DX-style 4-operator FM electric piano from primitives. Two operator pairs (tine 14:1 with a fast-decaying "
      "index, body 1:1 with self-feedback), velocity-scaled indices, mod wheel (CC1) opens the tine. "
      "Every operator is a SineOsc macro with phase-modulation input.")
e.m("f", "NoteFreq")
e.m("g", "gate")
e.m("vel", "velocity")
# velocity curve 0.35..1.0
e.m("v1", "mul", inn={"b": 0.65}); e.m("v2", "add", inn={"b": 0.35})
e.chain("vel", "v1.a"); e.w("v1", "v2.a")
# mod wheel factor 1..3
e.m("mw", "modwheel"); e.m("mwk", "mul", inn={"b": 2}); e.m("mw1", "add", inn={"b": 1})
e.w("mw", "mwk.a"); e.w("mwk", "mw1.a")
e.param("tine", "Tine", 0, 3, 0.45)
e.param("body", "Body", 0, 3, 0.5)
e.param("fbk", "Feedback", 0, 0.6, 0.08)
e.param("rat", "Tine Ratio", 1, 20, 14)
e.param("rel", "Release", 0.1, 3, 0.7, "exp")

# pair A: tine
e.m("m1f", "mul"); e.w("f", "m1f.a"); e.w("rat", "m1f.b")
e.m("m1", "SineOsc"); e.w("m1f", "m1.freq")
e.m("m1e", "adsr", inn={"a": 0.001, "d": 0.18, "s": 0.0, "r": 0.1}); e.w("g", "m1e.gate")
e.m("m1o", "mul"); e.w("m1", "m1o.a"); e.w("m1e", "m1o.b")
e.m("t1", "mul"); e.w("tine", "t1.a"); e.w("v2", "t1.b")
e.m("t2", "mul"); e.w("t1", "t2.a"); e.w("mw1", "t2.b")
e.m("m1i", "mul"); e.w("m1o", "m1i.a"); e.w("t2", "m1i.b")
e.m("c1", "SineOsc"); e.w("f", "c1.freq"); e.w("m1i", "c1.pm")
e.m("a1e", "adsr", inn={"a": 0.001, "d": 2.2, "s": 0.0}); e.w("g", "a1e.gate"); e.w("rel", "a1e.r")
e.m("c1o", "mul"); e.w("c1", "c1o.a"); e.w("a1e", "c1o.b")

# pair B: body with feedback on the modulator
e.m("m2", "SineOsc"); e.w("f", "m2.freq")
e.m("fbz", "z1"); e.w("m2", "fbz.in")
e.m("fbm", "mul"); e.w("fbz", "fbm.a"); e.w("fbk", "fbm.b"); e.w("fbm", "m2.pm")
e.m("m2e", "adsr", inn={"a": 0.002, "d": 1.4, "s": 0.25, "r": 0.5}); e.w("g", "m2e.gate")
e.m("m2o", "mul"); e.w("m2", "m2o.a"); e.w("m2e", "m2o.b")
e.m("b1", "mul"); e.w("body", "b1.a"); e.w("v2", "b1.b")
e.m("m2i", "mul"); e.w("m2o", "m2i.a"); e.w("b1", "m2i.b")
e.m("c2", "SineOsc"); e.w("f", "c2.freq"); e.w("m2i", "c2.pm")
e.m("a2e", "adsr", inn={"a": 0.002, "d": 3.5, "s": 0.12}); e.w("g", "a2e.gate"); e.w("rel", "a2e.r")
e.m("c2o", "mul"); e.w("c2", "c2o.a"); e.w("a2e", "c2o.b")

# mix
e.m("mix", "mul", inn={"b": 0.22}); e.w("c1o", "mix.a"); e.w("c2o", "mix.a")
e.m("vk", "mul"); e.w("mix", "vk.a"); e.w("v2", "vk.b")      # velocity also scales loudness
e.m("sat", "tanh"); e.w("vk", "sat.in")
write("patches/09-dx-epiano.json", e.dump("sat.out"))


# ---------------------------------------------------------------------------
# 10  Ladder lead: 2 detuned saws + sub sine -> drive -> LadderLP
#     key tracking, filter envelope (velocity-scaled), mod wheel opens the filter
# ---------------------------------------------------------------------------
l = P("Ladder Lead",
      "Moog-style voice from primitives: two detuned saws and a sub sine into the LadderLP macro (4 cascaded ZDF "
      "one-poles + resonance feedback through z1). Cutoff, envelope amount, key tracking, resonance, drive; "
      "velocity scales the filter envelope and the mod wheel (CC1) opens the cutoff.")
l.param("det", "Detune", 0, 0.5, 0.1)
l.param("cut", "Cutoff", 40, 12000, 700, "exp")
l.param("env", "Env Amount", 0, 6, 3)
l.param("res", "Resonance", 0, 3.8, 1.6)
l.param("kt", "Key Track", 0, 1, 0.5)
l.param("drive", "Drive", 0, 4, 1)
l.param("fdec", "Filter Decay", 0.05, 2, 0.35, "exp")
l.param("sub", "Sub Level", 0, 1, 0.5)

l.m("g", "gate"); l.m("vel", "velocity")
l.m("v1", "mul", inn={"b": 0.5}); l.m("v2", "add", inn={"b": 0.5}); l.w("vel", "v1.a"); l.w("v1", "v2.a")
l.m("mw", "modwheel"); l.m("mwk", "mul", inn={"b": 2}); l.m("mw1", "add", inn={"b": 1})
l.w("mw", "mwk.a"); l.w("mwk", "mw1.a")

# oscillators
l.m("t1", "add", inn={"b": 0.0}); l.m("f1", "NoteFreq"); l.w("det", "t1.a"); l.w("t1", "f1.transpose")
l.m("dn", "neg"); l.m("f2", "NoteFreq"); l.w("det", "dn.in"); l.w("dn", "f2.transpose")
l.m("s1", "Saw"); l.w("f1", "s1.freq")
l.m("s2", "Saw"); l.w("f2", "s2.freq")
l.m("fs", "NoteFreq"); l.m("fsub", "mul", inn={"b": 0.5}); l.w("fs", "fsub.a")
l.m("so", "SineOsc"); l.w("fsub", "so.freq")
l.m("sg", "mul"); l.w("so", "sg.a"); l.w("sub", "sg.b")
l.m("mix", "mul", inn={"b": 0.35}); l.w("s1", "mix.a"); l.w("s2", "mix.a"); l.w("sg", "mix.a")
l.m("dr1", "add", inn={"b": 1}); l.w("drive", "dr1.a")
l.m("pre", "mul"); l.w("mix", "pre.a"); l.w("dr1", "pre.b")

# cutoff: base * (1 + env*fenv*vel) * keytrack * modwheel, clipped to a safe range
l.m("fenv", "adsr", inn={"a": 0.002, "s": 0.2, "r": 0.2}); l.w("g", "fenv.gate"); l.w("fdec", "fenv.d")
l.m("em", "mul"); l.w("fenv", "em.a"); l.w("env", "em.b")
l.m("emv", "mul"); l.w("em", "emv.a"); l.w("v2", "emv.b")
l.m("em1", "add", inn={"b": 1}); l.w("emv", "em1.a")
l.m("fr", "div", inn={"b": 261.63}); l.w("fs", "fr.a")
l.m("ktr", "pow"); l.w("fr", "ktr.a"); l.w("kt", "ktr.b")
l.m("c1", "mul"); l.w("cut", "c1.a"); l.w("em1", "c1.b")
l.m("c2", "mul"); l.w("c1", "c2.a"); l.w("ktr", "c2.b")
l.m("c3", "mul"); l.w("c2", "c3.a"); l.w("mw1", "c3.b")
l.m("fc", "clip", inn={"lo": 20, "hi": 16000}); l.w("c3", "fc.in")

l.m("lad", "LadderLP"); l.w("pre", "lad.in"); l.w("fc", "lad.cutoff"); l.w("res", "lad.res")
l.m("aenv", "adsr", inn={"a": 0.003, "d": 0.3, "s": 0.8, "r": 0.2}); l.w("g", "aenv.gate")
l.m("vca", "mul"); l.w("lad", "vca.a"); l.w("aenv", "vca.b")
l.m("lvl", "mul", inn={"b": 0.38}); l.w("vca", "lvl.a")
l.m("sat", "tanh"); l.w("lvl", "sat.in")
write("patches/10-ladder-lead.json", l.dump("sat.out"))
