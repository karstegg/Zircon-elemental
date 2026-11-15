#!/usr/bin/env python3
"""Generate the Zircon Lattice macros and patches from zircon's measured physics.

Everything below is built from Elemental primitives (no native DSP modules):

  * Modes      - the Raman-active lattice vibrations of ZrSiO4 become a bank of
                 two-pole resonators. Partial ratios = band positions / 202 cm^-1.
  * Linewidth  - each band's FWHM (cm^-1) sets that mode's bandwidth, so ring
                 time follows the real phonon lifetimes. Q is pitch-invariant.
  * Metamict   - alpha-decay damage from U/Th downshifts and broadens the bands
                 (Nasdala et al.). One knob morphs gem-crystal -> amorphous glass.
  * Birefringence - zircon is uniaxial; light splits into ordinary/extraordinary
                 rays with different speeds. Every mode gets an e-ray twin detuned
                 by n_o/(n_o+dn); o-rays -> left, e-rays -> right, mixed by a
                 polarizer angle.
  * Fire       - optical dispersion (0.038) becomes partial-stretch dispersion.
  * Temperature - phonon linewidths grow with temperature (anharmonic decay):
                 cold crystal rings longer.
  * Exciter    - a hammer strike whose hardness is Mohs-scaled, plus Poisson
                 "alpha decay" clicks (radioactive activity) that keep the
                 lattice shimmering while a note is held.

Values are representative of published spectra (band positions from micro-Raman
tables; widths/shifts approximate pristine vs. strongly damaged zircon).
"""
import json, math, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

#            band cm^-1, rel. intensity, FWHM pristine, FWHM metamict, downshift metamict, assignment
BANDS = [
    (202.0, 0.30, 3.0, 14.0, 3.0, "lattice (Eg)"),
    (214.0, 0.22, 3.0, 14.0, 3.0, "lattice (Eg)"),
    (225.0, 0.30, 3.5, 16.0, 3.0, "lattice (B1g)"),
    (356.0, 0.75, 3.5, 22.0, 6.0, "external SiO4 rotation"),
    (439.0, 0.55, 3.0, 20.0, 6.0, "nu2 SiO4 bend"),
    (974.0, 0.35, 2.5, 26.0, 9.0, "nu1 SiO4 stretch"),
    (1008.0, 1.00, 2.5, 30.0, 10.0, "nu3 SiO4 antisymmetric stretch"),
    (1055.0, 0.08, 4.0, 30.0, 10.0, "weak"),
]
REF = BANDS[0][0]
N_O = 1.93          # ordinary refractive index
DISPERSION = 0.038  # zircon B-G dispersion


def raman_mode():
    """Two-pole resonator from primitives: y = g*x + 2r cos(w) y1 - r^2 y2."""
    return {
        "name": "RamanMode",
        "doc": "Two-pole resonator from primitives. freq Hz, width = bandwidth Hz (FWHM). "
               "Input gain sin(w) keeps impulse response level equal across modes.",
        "inputs": [{"name": "in"}, {"name": "freq", "default": 440}, {"name": "width", "default": 5}],
        "outputs": {"out": "y.out"},
        "modules": [
            {"id": "sr", "type": "sr"},
            {"id": "wn", "type": "div"}, {"id": "w", "type": "mul", "in": {"b": 6.283185307}},
            {"id": "cw", "type": "cos"}, {"id": "sw", "type": "sin"},
            {"id": "bn", "type": "div"}, {"id": "bk", "type": "mul", "in": {"b": -3.14159265}},
            {"id": "r", "type": "exp"},
            {"id": "r2", "type": "mul"}, {"id": "a2", "type": "neg"},
            {"id": "rc", "type": "mul"}, {"id": "a1", "type": "mul", "in": {"b": 2}},
            {"id": "gx", "type": "mul"},
            {"id": "y1", "type": "z1"}, {"id": "y2", "type": "z1"},
            {"id": "t1", "type": "mul"}, {"id": "t2", "type": "mul"},
            {"id": "y", "type": "add"},
        ],
        "wires": [
            ["$freq", "wn.a"], ["sr", "wn.b"], ["wn", "w.a"], ["w", "cw.in"], ["w", "sw.in"],
            ["$width", "bn.a"], ["sr", "bn.b"], ["bn", "bk.a"], ["bk", "r.in"],
            ["r", "r2.a"], ["r", "r2.b"], ["r2", "a2.in"],
            ["r", "rc.a"], ["cw", "rc.b"], ["rc", "a1.a"],
            ["$in", "gx.a"], ["sw", "gx.b"],
            ["a1", "t1.a"], ["y1", "t1.b"], ["a2", "t2.a"], ["y2", "t2.b"],
            ["gx", "y.a"], ["t1", "y.a"], ["t2", "y.a"],
            ["y", "y1.in"], ["y1", "y2.in"],
        ],
    }


def lattice():
    """ZirconLattice: the crystal. Inputs in physical units, outputs stereo."""
    mods, wires = [], []
    M = lambda **k: mods.append(k)
    W = lambda a, b: wires.append([a, b])

    # shared: Hz per cm^-1 for this note, linewidth scale, e-ray ratio, dispersion amount
    M(id="hzpc", type="div", **{"in": {"b": REF}}); W("$f0", "hzpc.a")
    # temperature multiplier on linewidth: 0.15 + 0.85*T/300
    M(id="tk", type="mul", **{"in": {"b": 0.85 / 300.0}}); W("$temp", "tk.a")
    M(id="tm", type="add", **{"in": {"b": 0.15}}); W("tk", "tm.a")
    M(id="wscale", type="mul"); W("hzpc", "wscale.a"); W("tm", "wscale.b")
    # e-ray frequency ratio n_o / (n_o + dn)
    M(id="ne", type="add", **{"in": {"b": N_O}}); W("$biref", "ne.a")
    M(id="eratio", type="div", **{"in": {"a": N_O}}); W("ne", "eratio.b")
    # dispersion: D = fire * 0.038 * 0.25 per unit of (ratio-1)
    M(id="disp", type="mul", **{"in": {"b": DISPERSION * 0.25}}); W("$fire", "disp.a")
    # polarizer: c = cos^2(theta), s = 1 - c ; theta in degrees
    M(id="rad", type="mul", **{"in": {"b": math.pi / 180}}); W("$polar", "rad.a")
    M(id="cth", type="cos"); W("rad", "cth.in")
    M(id="c2", type="mul"); W("cth", "c2.a"); W("cth", "c2.b")
    M(id="s2", type="sub", **{"in": {"a": 1}}); W("c2", "s2.b")
    M(id="sumO", type="add"); M(id="sumE", type="add")

    for i, (nu, amp, g0, g1, shift, _) in enumerate(BANDS):
        p = f"m{i}_"
        # band position after damage: nu - m*shift  (cm^-1)
        M(id=p + "sh", type="mul", **{"in": {"b": -shift}}); W("$metamict", p + "sh.a")
        M(id=p + "nu", type="add", **{"in": {"b": nu}}); W(p + "sh", p + "nu.a")
        # dispersion stretch: ratio*(1 + D*(ratio-1))
        M(id=p + "rt", type="div", **{"in": {"b": REF}}); W(p + "nu", p + "rt.a")
        M(id=p + "rm1", type="sub", **{"in": {"b": 1}}); W(p + "rt", p + "rm1.a")
        M(id=p + "dd", type="mul"); W(p + "rm1", p + "dd.a"); W("disp", p + "dd.b")
        M(id=p + "dd1", type="add", **{"in": {"b": 1}}); W(p + "dd", p + "dd1.a")
        M(id=p + "fo", type="mul"); W(p + "nu", p + "fo.a"); W("hzpc", p + "fo.b")
        M(id=p + "fs", type="mul"); W(p + "fo", p + "fs.a"); W(p + "dd1", p + "fs.b")
        M(id=p + "fe", type="mul"); W(p + "fs", p + "fe.a"); W("eratio", p + "fe.b")
        # linewidth: g0 + m*(g1-g0)  (cm^-1) -> Hz
        M(id=p + "gm", type="mul", **{"in": {"b": g1 - g0}}); W("$metamict", p + "gm.a")
        M(id=p + "g", type="add", **{"in": {"b": g0}}); W(p + "gm", p + "g.a")
        M(id=p + "bw", type="mul"); W(p + "g", p + "bw.a"); W("wscale", p + "bw.b")
        # excitation per mode = Raman intensity
        M(id=p + "ex", type="mul", **{"in": {"b": amp}}); W("$exc", p + "ex.a")
        # o-ray and e-ray resonators
        for ray, f in (("o", "fs"), ("e", "fe")):
            M(id=p + ray, type="RamanMode")
            W(p + "ex", p + ray + ".in"); W(p + f, p + ray + ".freq"); W(p + "bw", p + ray + ".width")
            W(p + ray, "sum" + ray.upper() + ".a")

    # polarizer mix
    for out, a, b in (("L", "c2", "s2"), ("R", "s2", "c2")):
        M(id=out + "o", type="mul"); W("sumO", out + "o.a"); W(a, out + "o.b")
        M(id=out + "e", type="mul"); W("sumE", out + "e.a"); W(b, out + "e.b")
        M(id=out, type="add"); W(out + "o", out + ".a"); W(out + "e", out + ".b")

    return {
        "name": "ZirconLattice",
        "doc": "Resonant zircon crystal: 8 Raman phonon modes x (ordinary + extraordinary ray). "
               "f0 = pitch of the 202 cm^-1 mode; metamict 0..1; biref 0..0.06; fire 0..3; "
               "temp Kelvin; polar = polarizer angle in degrees. Outputs L (o-ray-weighted), R.",
        "inputs": [{"name": "exc"}, {"name": "f0", "default": 220}, {"name": "metamict", "default": 0},
                   {"name": "biref", "default": 0.042}, {"name": "fire", "default": 1},
                   {"name": "temp", "default": 300}, {"name": "polar", "default": 20}],
        "outputs": {"L": "L.out", "R": "R.out"},
        "modules": mods, "wires": wires,
    }


def alpha_exciter():
    """Hammer strike (Mohs hardness) + Poisson alpha-decay clicks."""
    return {
        "name": "AlphaStrike",
        "doc": "Exciter: noise hammer whose length/brightness follow Mohs hardness (1..10), "
               "plus Poisson impulse clicks at `activity` Hz while `hold` > 0 (radioactive decay).",
        "inputs": [{"name": "gate"}, {"name": "vel", "default": 1}, {"name": "mohs", "default": 7.5},
                   {"name": "activity", "default": 0}, {"name": "hold", "default": 0}],
        "outputs": {"out": "mix.out"},
        "modules": [
            {"id": "n", "type": "noise"},
            # strike decay = 0.0015 * (11 - mohs) s ; brightness = 400 * 2^(mohs*0.75) Hz
            {"id": "h1", "type": "sub", "in": {"a": 11}}, {"id": "dec", "type": "mul", "in": {"b": 0.0015}},
            {"id": "env", "type": "adsr", "in": {"a": 0.0003, "s": 0, "r": 0.002}},
            {"id": "hb", "type": "mul", "in": {"b": 0.519860385}}, {"id": "hx", "type": "exp"},
            {"id": "cut", "type": "mul", "in": {"b": 400}}, {"id": "cmin", "type": "min", "in": {"b": 18000}},
            {"id": "lp", "type": "OnePoleLP"},
            {"id": "hit", "type": "mul"}, {"id": "hv", "type": "mul", "in": {"b": 0.12}},
            # Poisson clicks: noise > 1 - 2*rate/sr
            {"id": "sr", "type": "sr"}, {"id": "rr", "type": "div"}, {"id": "r2", "type": "mul", "in": {"b": -2}},
            {"id": "th", "type": "add", "in": {"b": 1}}, {"id": "n2", "type": "noise"},
            {"id": "click", "type": "gt"}, {"id": "n3", "type": "noise"},
            {"id": "camp", "type": "mul"}, {"id": "ch", "type": "mul"}, {"id": "cg", "type": "mul", "in": {"b": 1.5}},
            {"id": "vel", "type": "mul"},
            {"id": "mix", "type": "add"},
        ],
        "wires": [
            ["$mohs", "h1.b"], ["h1", "dec.a"], ["dec", "env.d"], ["$gate", "env.gate"],
            ["$mohs", "hb.a"], ["hb", "hx.in"], ["hx", "cut.a"], ["cut", "cmin.a"],
            ["n", "lp.in"], ["cmin", "lp.cutoff"], ["lp", "hit.a"], ["env", "hit.b"], ["hit", "hv.a"],
            ["$activity", "rr.a"], ["sr", "rr.b"], ["rr", "r2.a"], ["r2", "th.a"],
            ["n2", "click.a"], ["th", "click.b"], ["click", "camp.a"], ["n3", "camp.b"],
            ["camp", "ch.a"], ["$hold", "ch.b"], ["ch", "cg.a"],
            ["hv", "vel.a"], ["cg", "vel.a"], ["$vel", "vel.b"], ["vel", "mix.a"],
        ],
    }


def patch(name, doc, d):
    """A full voice: strike -> ZirconLattice -> hold envelope -> soft clip."""
    P = lambda id, n, lo, hi, df, curve="lin": {"id": id, "type": "param",
        "params": {"name": n, "min": lo, "max": hi, "default": df, "curve": curve}}
    return {
        "name": name, "doc": doc, "voices": 8,
        "modules": [
            P("pm", "Metamict", 0, 1, d["metamict"]),
            P("pb", "Birefringence", 0, 0.06, d["biref"]),
            P("pf", "Fire", 0, 3, d["fire"]),
            P("pt", "Temperature", 10, 1200, d["temp"], "exp"),
            P("pp", "Polarizer", 0, 90, d["polar"]),
            P("ph", "Mohs", 1, 10, d["mohs"]),
            P("pa", "Activity", 1, 400, d["activity"], "exp"),
            P("pr", "Release", 0.05, 8, d["release"], "exp"),
            P("pv", "Volume", 0, 1, 0.6),
            {"id": "f", "type": "NoteFreq", "in": {"transpose": d.get("transpose", 0)}},
            {"id": "g", "type": "gate"}, {"id": "v", "type": "velocity"},
            {"id": "hold", "type": "adsr", "in": {"a": 0.002, "d": 0.01, "s": 1}},
            {"id": "act", "type": "mul"},
            {"id": "x", "type": "AlphaStrike"},
            {"id": "z", "type": "ZirconLattice"},
            {"id": "aL", "type": "mul"}, {"id": "aR", "type": "mul"},
            {"id": "gL", "type": "mul", "in": {"b": d["gain"]}}, {"id": "gR", "type": "mul", "in": {"b": d["gain"]}},
            {"id": "vL", "type": "mul"}, {"id": "vR", "type": "mul"},
            {"id": "oL", "type": "tanh"}, {"id": "oR", "type": "tanh"},
        ],
        "wires": [
            ["g", "hold.gate"], ["pr", "hold.r"],
            ["g", "x.gate"], ["v", "x.vel"], ["ph", "x.mohs"], ["pa", "act.a"], ["hold", "act.b"],
            ["act", "x.activity"], ["g", "x.hold"],
            ["x", "z.exc"], ["f", "z.f0"], ["pm", "z.metamict"], ["pb", "z.biref"], ["pf", "z.fire"],
            ["pt", "z.temp"], ["pp", "z.polar"],
            ["z.L", "aL.a"], ["hold", "aL.b"], ["z.R", "aR.a"], ["hold", "aR.b"],
            ["aL", "gL.a"], ["aR", "gR.a"], ["gL", "vL.a"], ["pv", "vL.b"], ["gR", "vR.a"], ["pv", "vR.b"],
            ["vL", "oL.in"], ["vR", "oR.in"],
        ],
        "out": {"L": "oL.out", "R": "oR.out"},
    }


PRESETS = [
    ("06-zircon-gem", "Zircon: Gem",
     "Pristine high zircon. Narrow phonon lines ring like struck crystal; 202/214/225 cluster beats; "
     "nu1/nu3 pair shimmers. Birefringence spreads o/e rays across stereo.",
     dict(metamict=0.0, biref=0.045, fire=1.0, temp=120, polar=25, mohs=7.5, activity=6, release=3.0, gain=0.32)),
    ("07-zircon-metamict-glass", "Zircon: Metamict Glass",
     "Ancient, radiation-damaged zircon: bands downshifted and broadened toward amorphous glass. "
     "Warm, woody, short ring - a damped marimba from another planet.",
     dict(metamict=0.85, biref=0.012, fire=0.3, temp=300, polar=10, mohs=6.0, activity=3, release=0.6, gain=0.52)),
    ("08-zircon-geiger-pad", "Zircon: Geiger Shimmer",
     "Cryogenic crystal with high U/Th activity: Poisson alpha-decay clicks continuously excite the "
     "lattice while held - a crackling, glittering sustained pad. Wide birefringent stereo.",
     dict(metamict=0.15, biref=0.059, fire=2.0, temp=30, polar=45, mohs=9.0, activity=120, release=4.0,
          gain=0.11, transpose=-12)),
]

if __name__ == "__main__":
    lib = ROOT / "library" / "macros"
    for m in (raman_mode(), lattice(), alpha_exciter()):
        (lib / f"{m['name']}.json").write_text(json.dumps(m, indent=1))
    for fn, name, doc, d in PRESETS:
        (ROOT / "patches" / f"{fn}.json").write_text(json.dumps(patch(name, doc, d), indent=1))
    print("generated", len(PRESETS), "patches;", len(BANDS), "modes x 2 rays")
