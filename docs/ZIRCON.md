# Zircon Lattice: a synth voice built from the physics of ZrSiO4

The signature instrument of Zircon Elemental. Every part is assembled from Elemental
math primitives (`z1`, `cos`, `exp`, `mul`, ...). There is no native DSP code. The
generator is `tools/gen_zircon.py`, and its data tables are the place to change the physics.

| Crystal property | Real-world fact | Sound mapping | Knob |
|---|---|---|---|
| **Raman phonon modes** | Lattice vibrations at ≈202, 214, 225, 356, 439, 974, 1008 cm⁻¹ | 8 two-pole resonators at `f0 · ν/202`. The 202/214/225 cluster beats; the ν1/ν3 pair (974/1008) shimmers | pitch |
| **Raman intensity** | ν3 (1008) strongest, then the rotation (356) and ν2 (439) bands | per-mode excitation gain | — |
| **Linewidth (FWHM)** | Pristine bands ≈2–4 cm⁻¹ wide | mode bandwidth = FWHM × Hz-per-cm⁻¹, so Q is pitch-invariant and ring time ∝ 1/FWHM | — |
| **Metamictisation** | α-decay of U/Th damages the lattice: bands broaden and shift to lower wavenumbers | morphs gem crystal → amorphous glass: modes detune down, ring shortens, timbre darkens | **Metamict** 0–1 |
| **Temperature** | Phonon linewidths grow with temperature (anharmonic decay) | linewidth × (0.15 + 0.85·T/300); cryogenic crystal rings longest | **Temperature** 10–1200 K |
| **Birefringence** | Uniaxial; o- and e-rays travel at different speeds (Δn up to ≈0.059) | every mode gets an e-ray twin at `f · n_o/(n_o+Δn)`. o-rays → L, e-rays → R | **Birefringence** 0–0.06 |
| **Polarizer** | Rotating a polarizer selects o vs e ray | L/R crossfade by cos²θ / sin²θ | **Polarizer** 0–90° |
| **Dispersion ("fire")** | 0.038, close to diamond | partial stretching ∝ (ν/202 − 1) | **Fire** 0–3 |
| **Hardness** | Mohs 7.5 | strike length and brightness (harder = shorter, brighter hammer) | **Mohs** 1–10 |
| **Radioactivity** | Trace U/Th decay continuously inside the crystal | Poisson "alpha decay" clicks keep exciting the lattice while a note is held | **Activity** 1–400 Hz |

## Presets

- **06 Zircon: Gem**: pristine high zircon. Glassy bell with beating low cluster and stereo o/e shimmer.
- **07 Zircon: Metamict Glass**: ancient, damaged crystal. Woody, damped, an alien marimba.
- **08 Zircon: Geiger Shimmer**: cryogenic, highly radioactive crystal. Alpha clicks make a crackling, glittering sustained pad.

## Verified

`zircon_tests` checks that the rendered partials land at `220·ν/202` Hz for A3 (e.g. ν3 → 1139 Hz
including dispersion) and that Metamict = 1 shifts the 202 cm⁻¹ mode from 220 Hz down toward 212 Hz.

## Ideas for the next zircon modules

- **Oscillatory zoning**: zircon grows in concentric chemical bands. These could become a morphing exciter or a rhythmic LFO.
- **Annealing**: heating repairs metamict damage (closure temperatures ≈260–370 °C per band). This could be a slow Temperature → Metamict feedback, a voice that "heals" while held.
- **U–Pb clock**: decay-chain timing for a probabilistic sequencer.

Band positions follow published micro-Raman tables. Widths and shifts are representative of pristine
vs. strongly damaged zircon, not a fit to a specific sample.
