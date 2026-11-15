#!/usr/bin/env python3
"""Generate the original factory DX7-style voices (patches 11-16).

These are our own voices written for the `dx7` module (6-operator FM, 32 algorithms,
standard DX7 parameter set).  They are NOT Yamaha factory voices.  To use real
cartridges, load a .syx file in the plugin ("Load patch..." accepts .syx).

Run:  python tools/gen_dx7.py
"""
import json, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=99, coarse=1, fine=0, mode=0, det=7,
       bp=39, ld=0, rd=0, lc=0, rc=0, rs=0, ams=0, kvs=0):
    """One operator, in DX7 parameter order (21 values)."""
    return [*r, *l, bp, ld, rd, lc, rc, rs, ams, kvs, ol, mode, coarse, fine, det]


OFF = op(ol=0)


def voice(name, algo, fb, ops, peg=((99, 99, 99, 99), (50, 50, 50, 50)), lfo=(35, 0, 0, 0, 1, 0, 3), trnsp=24):
    """ops: dict operator number (1..6) -> op(); missing operators are silent.
    lfo = (speed, delay, pmd, amd, key sync, wave, pms)."""
    vals = []
    for n in range(6, 0, -1):                       # data order is operator 6 first
        vals += ops.get(n, OFF)
    vals += [*peg[0], *peg[1]]
    vals += [algo - 1, fb, 1, lfo[0], lfo[1], lfo[2], lfo[3], lfo[4], lfo[5], lfo[6], trnsp]
    vals += [ord(c) for c in name.ljust(10)[:10]]
    assert len(vals) == 155, len(vals)
    return name, vals


VOICES = [
    voice("ZE E.PIANO", 5, 5, {
        1: op(r=(96, 38, 32, 60), l=(99, 78, 0, 0), kvs=2),
        2: op(r=(99, 55, 55, 50), l=(99, 55, 0, 0), ol=62, coarse=14, kvs=3),
        3: op(r=(96, 35, 30, 60), l=(99, 80, 0, 0), ol=90, det=8, kvs=2),
        4: op(r=(99, 40, 40, 50), l=(99, 45, 0, 0), ol=75, kvs=3),
        5: op(r=(95, 30, 25, 60), l=(99, 70, 0, 0), ol=60, det=5),
        6: op(r=(99, 35, 35, 50), l=(99, 40, 0, 0), ol=55),
    }),
    voice("ZE BELLS", 5, 3, {
        1: op(r=(99, 35, 35, 40), l=(99, 70, 0, 0)),
        2: op(r=(99, 38, 38, 40), l=(99, 50, 0, 0), ol=75, coarse=3, fine=50),
        3: op(r=(99, 32, 32, 40), l=(99, 70, 0, 0), ol=80, coarse=2),
        4: op(r=(99, 40, 40, 40), l=(99, 45, 0, 0), ol=70, coarse=5, fine=19),
        5: op(r=(99, 30, 30, 40), l=(99, 65, 0, 0), ol=60, coarse=3),
        6: op(r=(99, 42, 42, 40), l=(99, 40, 0, 0), ol=55, coarse=7),
    }),
    voice("ZE BASS", 1, 4, {
        1: op(r=(99, 50, 50, 55), l=(99, 90, 70, 0), kvs=2),
        2: op(r=(99, 60, 60, 55), l=(99, 55, 35, 0), ol=80),
        3: op(r=(99, 50, 50, 55), l=(99, 95, 90, 0), ol=85, coarse=0),
        4: op(r=(99, 55, 55, 55), l=(99, 60, 50, 0), ol=65),
        5: op(r=(99, 55, 55, 55), l=(99, 60, 50, 0), ol=60),
        6: op(r=(99, 55, 55, 55), l=(99, 70, 60, 0), ol=55),
    }),
    voice("ZE BRASS", 22, 6, {
        1: op(r=(75, 50, 40, 60), l=(99, 95, 90, 0), kvs=2),
        2: op(r=(70, 55, 40, 60), l=(99, 85, 75, 0), ol=70),
        3: op(r=(75, 50, 40, 60), l=(99, 95, 90, 0), ol=92, det=9),
        4: op(r=(75, 50, 40, 60), l=(99, 95, 90, 0), ol=92, det=5),
        5: op(r=(75, 50, 40, 60), l=(99, 95, 90, 0), ol=80),
        6: op(r=(70, 60, 40, 50), l=(99, 92, 85, 0), ol=78),
    }, lfo=(40, 25, 6, 0, 1, 4, 3)),
    voice("ZE MARIMBA", 5, 0, {
        1: op(r=(99, 45, 45, 65), l=(99, 60, 0, 0), kvs=2),
        2: op(r=(99, 70, 70, 60), l=(99, 10, 0, 0), ol=70, coarse=4, kvs=3),
    }),
    voice("ZE ORGAN", 32, 0, {
        1: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), coarse=1),
        2: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), ol=85, coarse=2),
        3: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), ol=70, coarse=3),
        4: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), ol=70, coarse=4),
        5: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), ol=55, coarse=6),
        6: op(r=(99, 99, 99, 70), l=(99, 99, 99, 0), ol=50, coarse=8),
    }, lfo=(32, 0, 3, 0, 1, 4, 2)),
]


def param(id, name, lo, hi, default, curve=None):
    p = {"name": name, "min": lo, "max": hi, "default": default}
    if curve:
        p["curve"] = curve
    return {"id": id, "type": "param", "params": p}


for i, (name, vals) in enumerate(VOICES, start=11):
    patch = {
        "name": "DX7: " + name.strip(),
        "doc": "Original DX7-style voice for the 'dx7' module (6-op FM, 32 algorithms). Knobs: Level, Bright "
               "(modulation depth), EG Speed, Vibrato. Mod wheel adds vibrato.",
        "voices": 8,
        "modules": [
            {"id": "dx", "type": "dx7", "params": {"voice": vals}},
            param("lvl", "Level", 0, 1, 0.9), param("brt", "Bright", 0, 2, 1),
            param("egs", "EG Speed", 0.25, 4, 1, "exp"), param("vib", "Vibrato", 0, 1, 0),
        ],
        "wires": [["lvl", "dx.level"], ["brt", "dx.bright"], ["egs", "dx.rate"], ["vib", "dx.pmd"]],
        "out": "dx.out",
    }
    slug = name.strip().lower().replace(" ", "-").replace(".", "")
    path = ROOT / "patches" / f"{i}-dx7-{slug}.json"
    path.write_text(json.dumps(patch, indent=None, separators=(", ", ": ")) + "\n", encoding="utf-8")
    print("wrote", path.relative_to(ROOT))
