// Minimal self-contained test runner for the core engine.
#include "zircon/compiler.hpp"
#include "zircon/engine.hpp"
#include "zircon/dx7.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace zircon;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static MacroLibrary lib() { MacroLibrary l; l.loadDirectory(ZIRCON_SOURCE_DIR "/library/macros"); return l; }

static std::vector<float> render(Engine& e, int n) {
    std::vector<float> L(n), R(n); e.process(L.data(), R.data(), n); return L;
}

static std::vector<zircon::dx7::Voice> factoryCart() {
    std::ifstream f(ZIRCON_SOURCE_DIR "/patches/11-dx7.json");
    std::stringstream ss; ss << f.rdbuf();
    return zircon::dx7::cartFromPatchJson(ss.str());
}
static zircon::dx7::Voice factoryVoice(int i) { return factoryCart().at((size_t)i); }

static double zeroCrossFreq(const std::vector<float>& x, int start, double sr) {
    int first = -1, last = -1, count = 0;
    for (size_t i = start + 1; i < x.size(); ++i)
        if (x[i - 1] <= 0.f && x[i] > 0.f) { if (first < 0) first = (int)i; last = (int)i; ++count; }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}

static void test(const char* name, std::function<void()> f) {
    int before = failures;
    try { f(); } catch (const std::exception& e) { printf("    EXCEPTION %s\n", e.what()); ++failures; }
    printf("%s %s\n", failures == before ? "[ ok ]" : "[FAIL]", name);
}

int main() {
    auto L = lib();

    test("showcase: DX e-piano is bounded, decays, and Tine brightens it", [&] {
        auto run = [&](float tine) {
            Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/09-dx-epiano.json", L));
            e.prepare(48000); e.setParam(e.findParam("Tine"), tine); e.noteOn(60, 1.f);
            auto x = render(e, 48000); e.noteOff(60);
            auto y = render(e, 48000 * 6);
            double hf = 0, tail = 0; float pk = 0;
            for (size_t i = 1; i < x.size(); ++i) { double d = x[i] - x[i - 1]; hf += d * d; pk = std::max(pk, std::fabs(x[i])); }
            for (size_t i = y.size() - 4800; i < y.size(); ++i) tail += y[i] * y[i];
            CHECK(std::isfinite(hf) && pk < 1.0f && pk > 0.05f);
            CHECK(tail / 4800 < 1e-6);        // fully released by 6 s
            return hf;
        };
        CHECK(run(2.5f) > 2.0 * run(0.2f));
    });

    test("showcase: ladder filter is stable at max resonance, tracks cutoff, voices free", [&] {
        auto run = [&](float cutoff, float res) {
            Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/10-ladder-lead.json", L));
            e.prepare(48000);
            e.setParam(e.findParam("Cutoff"), cutoff); e.setParam(e.findParam("Resonance"), res);
            e.setParam(e.findParam("Env Amount"), 0.f);
            e.noteOn(48, 1.f); e.noteOn(55, 1.f);
            auto x = render(e, 48000 * 2);
            double hf = 0; float pk = 0;
            for (size_t i = 1; i < x.size(); ++i) { double d = x[i] - x[i - 1]; hf += d * d; pk = std::max(pk, std::fabs(x[i])); }
            CHECK(std::isfinite(hf) && pk <= 1.0f);
            e.noteOff(48); e.noteOff(55); render(e, 48000 * 3);
            CHECK(e.activeVoices() == 0);
            return hf;
        };
        double dark = run(150.f, 0.5f), open = run(6000.f, 0.5f);
        CHECK(open > 5.0 * dark);
        run(1500.f, 3.8f);                    // self-oscillation region must stay bounded
    });

    test("dx7: algorithm table structure (carrier counts, modulators feed lower operators)", [&] {
        const int expectCarriers[32] = {2,2,2,2,3,3,2,2,2,2,2,2,2,2,2,1,1,1,3,3,4,4,4,5,5,3,3,3,4,4,5,6};
        for (int a = 0; a < 32; ++a) {
            const auto& g = dx7::algorithm(a);
            int n = 0; for (int i = 0; i < 6; ++i) n += (g.carriers >> i) & 1;
            CHECK(n == expectCarriers[a]);
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 6; ++j)
                    if ((g.mods[i] >> j) & 1) CHECK(j > i);        // operator 6 modulates down toward 1
        }
    });

    test("dx7: instrument patch loads and carries the factory cartridge", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/11-dx7.json", L));
        CHECK(e.numDx7() == 1 && e.numParams() == 4);
        CHECK(factoryCart().size() == 6);
        e.setDx7Voice(0, factoryVoice(3));
        CHECK(e.dx7Voice(0).name.substr(0, 8) == "ZE BRASS");
    });

    test("dx7: voice param round trip + packed SysEx parse", [&] {
        dx7::Voice v;
        v.algo = 17; v.fb = 5; v.trnsp = 30; v.lfw = 4; v.name = "ROUNDTRIP";
        v.op[2].ol = 77; v.op[2].fc = 9; v.op[2].ff = 33; v.op[2].det = 11; v.op[2].rate[1] = 42; v.op[2].level[2] = 18;
        v.op[2].lc = 2; v.op[2].rc = 1; v.op[2].rs = 5; v.op[2].ams = 2; v.op[2].kvs = 6; v.op[2].mode = 1;
        dx7::Voice w;
        CHECK(dx7::fromUnpacked(dx7::toUnpacked(v), w));
        CHECK(w.algo == 17 && w.fb == 5 && w.trnsp == 30 && w.lfw == 4);
        CHECK(w.op[2].ol == 77 && w.op[2].fc == 9 && w.op[2].ff == 33 && w.op[2].det == 11 && w.op[2].rate[1] == 42);
        CHECK(w.op[2].lc == 2 && w.op[2].rc == 1 && w.op[2].rs == 5 && w.op[2].ams == 2 && w.op[2].kvs == 6 && w.op[2].mode == 1);
        // hand-packed op1 (stored last, i = 5): check the bit layout of bytes 11..16
        std::vector<uint8_t> bulk(4104, 0);
        bulk[0] = 0xF0; bulk[1] = 0x43; bulk[3] = 0x09;
        uint8_t* p = &bulk[6];                           // voice 0
        uint8_t* o1 = p + 5 * 17;
        o1[11] = 2 | (1 << 2);                           // LC=2 RC=1
        o1[12] = 5 | (11 << 3);                          // RS=5 DET=11
        o1[13] = 2 | (6 << 2);                           // AMS=2 KVS=6
        o1[14] = 77; o1[15] = 1 | (9 << 1); o1[16] = 33; // OL, mode+coarse, fine
        p[110] = 17; p[111] = 5 | (1 << 3); p[117] = 30;
        auto voices = dx7::parseSysex(bulk);
        CHECK(voices.size() == 32);
        const auto& u = voices[0].op[0];
        CHECK(u.lc == 2 && u.rc == 1 && u.rs == 5 && u.det == 11 && u.ams == 2 && u.kvs == 6);
        CHECK(u.ol == 77 && u.mode == 1 && u.fc == 9 && u.ff == 33);
        CHECK(voices[0].algo == 17 && voices[0].fb == 5 && voices[0].oks == 1 && voices[0].trnsp == 30);
    });

    test("dx7: toSysex -> parseSysex round trip", [&] {
        dx7::Voice v;
        v.algo = 21; v.fb = 3; v.trnsp = 20; v.lfw = 5; v.lpms = 6; v.lks = 0; v.name = "SYXTEST";
        v.op[4].ol = 66; v.op[4].fc = 12; v.op[4].ff = 71; v.op[4].det = 3; v.op[4].mode = 1; v.op[4].rate[2] = 55;
        v.op[4].lc = 3; v.op[4].rc = 2; v.op[4].rs = 4; v.op[4].ams = 1; v.op[4].kvs = 5; v.op[4].bp = 50;
        v.plevel[1] = 20; v.prate[3] = 33;
        auto bytes = dx7::toSysex({v});
        CHECK(bytes.size() == 4104 && bytes[0] == 0xF0 && bytes.back() == 0xF7);
        int sum = 0; for (size_t i = 6; i < 6 + 4096; ++i) sum += bytes[i];
        CHECK(((sum + bytes[4102]) & 127) == 0);
        auto vs = dx7::parseSysex(bytes);
        CHECK(vs.size() == 32);
        CHECK(dx7::toUnpacked(vs[0]) == dx7::toUnpacked(v));
    });

    test("dx7: organ plays the right pitch; e-piano decays while held, releases, frees voice", [&] {
        {
            Engine e(compilePatch(dx7::patchJson(factoryVoice(5)), L));
            e.prepare(48000); e.noteOn(60, 1.f);
            render(e, 4800);
            auto x = render(e, 24000);
            int best = 0; double bestR = -1e30;
            for (int lag = 100; lag < 400; ++lag) {          // 120..480 Hz
                double r = 0; for (size_t i = 0; i + (size_t)lag < x.size(); i += 2) r += x[i] * x[i + lag];
                if (r > bestR) { bestR = r; best = lag; }
            }
            double f = 48000.0 / best;
            CHECK(std::fabs(f - 261.63) < 4.0);
        }
        Engine e(compilePatch(dx7::patchJson(factoryVoice(0)), L));
        e.prepare(48000); e.noteOn(60, 1.f);
        auto rms = [&](int n) { auto x = render(e, n); double r = 0; for (float v : x) r += v * v; return std::sqrt(r / n); };
        double early = rms(4800);
        render(e, 48000 * 3);
        double late = rms(4800);
        CHECK(early > 0.01 && late < 0.5 * early);           // decays while the key is held
        e.noteOff(60);
        render(e, 48000 * 4);
        CHECK(e.activeVoices() == 0);
    });

    test("dx7: all factory voices finite, bounded, silent after release", [&] {
        CHECK(factoryCart().size() == 6);
        for (int n = 0; n < 6; ++n) {
            Engine e(compilePatch(dx7::patchJson(factoryVoice(n)), L));
            e.prepare(48000); e.noteOn(48, 0.9f); e.noteOn(55, 0.9f); e.noteOn(64, 0.9f);
            auto x = render(e, 48000);
            float pk = 0; for (float v : x) { CHECK(std::isfinite(v)); pk = std::max(pk, std::fabs(v)); }
            CHECK(pk > 0.02f && pk < 1.5f);
            e.noteOff(48); e.noteOff(55); e.noteOff(64);
            render(e, 48000 * 8);
            CHECK(e.activeVoices() == 0);
        }
    });

    test("dx7: patchJson(voice) compiles and plays", [&] {
        dx7::Voice v; v.algo = 4; v.name = "CART TEST";
        v.op[0].ol = 99; v.op[1].ol = 70; v.op[1].fc = 2;
        Engine e(compilePatch(dx7::patchJson(v), L));
        e.prepare(48000); e.noteOn(60, 1.f);
        auto x = render(e, 4800); float pk = 0; for (float f : x) pk = std::max(pk, std::fabs(f));
        CHECK(pk > 0.02f);
        CHECK(e.numParams() == 4);
    });

    test("mod wheel brightens the FM bell", [&] {
        auto energy = [&](float mw) {
            Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/03-fm-bell.json", L));
            e.prepare(48000); e.setModWheel(mw); e.noteOn(60, 1.f);
            auto x = render(e, 24000); double hf = 0;
            for (size_t i = 1; i < x.size(); ++i) { double d = x[i] - x[i - 1]; hf += d * d; }
            return hf;
        };
        CHECK(energy(1.f) > 1.5 * energy(0.f));
    });

    test("sustain pedal lengthens karplus ring", [&] {
        auto tail = [&](bool pedal) {
            Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/04-karplus-pluck.json", L));
            e.prepare(48000); e.setSustain(pedal); e.noteOn(57, 1.f); render(e, 2400); e.noteOff(57);
            auto x = render(e, 48000 * 3); double r = 0;
            for (size_t i = x.size() - 4800; i < x.size(); ++i) r += x[i] * x[i];
            return r;
        };
        CHECK(tail(true) > 4.0 * tail(false));
    });

    test("re-pressing a pedal-held note re-excites the pluck", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/04-karplus-pluck.json", L));
        e.prepare(48000); e.setSustain(true);
        e.noteOn(57, 1.f); render(e, 2400); e.noteOff(57);
        render(e, 48000 * 2);
        auto peak = [](const std::vector<float>& x) { float m = 0; for (float v : x) m = std::max(m, std::fabs(v)); return m; };
        float before = peak(render(e, 2400));
        e.noteOn(57, 1.f);
        float after = peak(render(e, 2400));
        CHECK(after > 2.f * before);
        // pool full of pedal-held notes: stolen voice must also re-excite
        for (int n = 40; n < 40 + 12; ++n) { e.noteOn(n, 1.f); render(e, 4800); e.noteOff(n); }
        e.noteOn(80, 1.f);
        CHECK(peak(render(e, 2400)) > 0.05f);
    });

    test("pedal down: newest chord notes always get voices (stealing)", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/02-zdf-saw-bass.json", L));
        e.prepare(48000); e.setSustain(true);
        int chords[4][4] = {{48, 52, 55, 60}, {50, 53, 57, 62}, {52, 55, 59, 64}, {53, 57, 60, 65}};
        for (auto& c : chords) {
            for (int n : c) { e.noteOn(n, 0.8f); render(e, 64); }
            for (int n : c) e.noteOff(n);
            render(e, 4800);
        }
        CHECK(e.activeVoices() == e.program().voices);   // pool full of held notes, newest still fit
    });

    test("sustain pedal holds released notes until pedal up", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/02-zdf-saw-bass.json", L));
        e.prepare(48000); e.setSustain(true); e.noteOn(48, 1.f);
        render(e, 4800); e.noteOff(48);
        auto held = render(e, 48000 * 2);                 // pedal still down -> must keep sounding
        double rms = 0; for (size_t i = held.size() - 4800; i < held.size(); ++i) rms += held[i] * held[i];
        CHECK(rms / 4800 > 1e-6);
        e.setSustain(false);                              // pedal up -> release, voice frees
        render(e, 48000 * 6);
        CHECK(e.activeVoices() == 0);
    });

    test("primitive sine oscillates at 440 Hz for note 69", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/01-primitive-sine.json", L));
        e.prepare(48000); e.noteOn(69, 1.f);
        auto x = render(e, 48000);
        double f = zeroCrossFreq(x, 4800, 48000);
        CHECK(std::fabs(f - 440.0) < 0.5);
    });

    test("algebraic loop is rejected with a helpful error", [&] {
        const char* p = R"({"modules":[{"id":"a","type":"add"},{"id":"b","type":"mul"}],
                            "wires":[["a","b.a"],["b","a.a"]],"out":"a.out"})";
        bool threw = false;
        try { compilePatch(p, L); } catch (const CompileError& e) { threw = std::string(e.what()).find("z1") != std::string::npos; }
        CHECK(threw);
    });

    test("loop closed by z1 compiles (counter ramps by 1/sample)", [&] {
        const char* p = R"({"voices":1,"modules":[{"id":"acc","type":"add","in":{"b":1}},{"id":"m","type":"z1"}],
                            "wires":[["m","acc.a"],["acc","m.in"]],"out":"acc.out"})";
        Engine e(compilePatch(p, L)); e.prepare(48000); e.noteOn(60, 1.f);
        auto x = render(e, 10);
        CHECK(x[0] == 1.f); CHECK(x[3] == 4.f);
    });

    test("primitive ZDF one-pole has unity DC gain and correct -3dB point", [&] {
        const char* p = R"({"voices":1,"modules":[{"id":"lp","type":"OnePoleLP","in":{"in":1,"cutoff":1000}}],"out":"lp.out"})";
        Engine e(compilePatch(p, L)); e.prepare(48000); e.noteOn(60, 1.f);
        auto x = render(e, 48000);
        CHECK(std::fabs(x.back() - 1.f) < 1e-4);
        // step response of 1-pole at fc: reaches 1-1/e after 1/(2*pi*fc) seconds (~7.64 samples)
        int i63 = 0; while (x[i63] < 0.6321f) ++i63;
        CHECK(std::abs(i63 - 8) <= 1);
    });

    test("multiple wires into one input are summed", [&] {
        const char* p = R"({"voices":1,"modules":[{"id":"a","type":"const","params":{"value":0.25}},
                            {"id":"b","type":"const","params":{"value":0.5}},{"id":"s","type":"mul","in":{"b":1}}],
                            "wires":[["a","s.a"],["b","s.a"]],"out":"s.out"})";
        Engine e(compilePatch(p, L)); e.prepare(48000); e.noteOn(60, 1.f);
        CHECK(std::fabs(render(e, 4)[3] - 0.75f) < 1e-6);
    });

    test("nested + local macros flatten (pad patch compiles, stereo)", [&] {
        auto prog = compilePatchFile(ZIRCON_SOURCE_DIR "/patches/05-svf-pad.json", L);
        CHECK(prog.outL != prog.outR);
        CHECK(prog.params.size() == 3);
    });

    test("polyphony: 3 notes sum, voices free after release", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/01-primitive-sine.json", L));
        e.prepare(48000);
        for (int n : {60, 64, 67}) e.noteOn(n, 1.f);
        render(e, 4800);
        CHECK(e.activeVoices() == 3);
        for (int n : {60, 64, 67}) e.noteOff(n);
        render(e, 48000 * 4);
        CHECK(e.activeVoices() == 0);
    });

    test("voice stealing beyond polyphony", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/01-primitive-sine.json", L));
        e.prepare(48000);
        for (int n = 40; n < 60; ++n) e.noteOn(n, 1.f);
        render(e, 64);
        CHECK(e.activeVoices() == e.program().voices);
    });

    test("exp param curve maps normalized <-> value", [&] {
        ParamInfo p; p.min = 20; p.max = 20000; p.expCurve = true;
        CHECK(std::fabs(p.fromNormalized(0.5f) - 632.45f) < 0.1f);
        CHECK(std::fabs(p.toNormalized(632.45f) - 0.5f) < 1e-4f);
    });

    test("karplus pluck pitch follows note (A3 = 220 Hz)", [&] {
        Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/04-karplus-pluck.json", L));
        e.prepare(48000); e.noteOn(57, 1.f);
        auto x = render(e, 24000);
        // autocorrelation peak near 48000/220 = 218 samples
        int best = 0; double bestv = -1e9;
        for (int lag = 150; lag < 300; ++lag) {
            double s = 0; for (int i = 4800; i < 14400; ++i) s += x[i] * x[i + lag];
            if (s > bestv) { bestv = s; best = lag; }
        }
        CHECK(std::abs(best - 218) <= 3);
    });

    test("zircon lattice: Raman partials at 220*nu/202 Hz, metamict downshifts", [&] {
        auto energyAt = [](const std::vector<float>& x, double f) {   // Goertzel
            double w = 2 * 3.14159265358979 * f / 48000, c = 2 * std::cos(w), s1 = 0, s2 = 0;
            for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - c * s1 * s2;
        };
        auto renderNote = [&](float metamict) {
            Engine e(compilePatchFile(ZIRCON_SOURCE_DIR "/patches/06-zircon-gem.json", L));
            e.prepare(48000); e.setParam(e.findParam("Metamict"), metamict);
            e.setParam(e.findParam("Activity"), 1.f);
            render(e, 4800); e.noteOn(57, 1.f);
            return render(e, 48000);
        };
        auto x0 = renderNote(0.f), x1 = renderNote(1.f);
        // nu3 (1008 cm^-1) -> 1139 Hz incl. dispersion; nu_rot (356) -> 391 Hz
        CHECK(energyAt(x0, 1139) > 50 * energyAt(x0, 800));
        CHECK(energyAt(x0, 391) > 50 * energyAt(x0, 600));
        // damage shifts the 202 mode from 220 Hz down toward 212 Hz
        CHECK(energyAt(x1, 212) > energyAt(x1, 220));
        CHECK(energyAt(x0, 220) > energyAt(x0, 212));
    });

    test("unknown module and bad port give errors", [&] {
        int errs = 0;
        try { compilePatch(R"({"modules":[{"id":"x","type":"nope"}],"out":"x.out"})", L); } catch (const CompileError&) { ++errs; }
        try { compilePatch(R"({"modules":[{"id":"x","type":"add"}],"wires":[["x","x.zz"]],"out":"x.out"})", L); } catch (const CompileError&) { ++errs; }
        CHECK(errs == 2);
    });

    test("malformed sysex is rejected without crashing", [&] {
        CHECK(dx7::parseSysex({}).empty());
        auto good = dx7::toSysex(factoryCart());
        CHECK(good.size() == 4104 && dx7::parseSysex(good).size() == 32);
        auto trunc = good; trunc.resize(4000);
        CHECK(dx7::parseSysex(trunc).empty());                 // truncated 32-voice dump
        trunc.resize(100);
        CHECK(dx7::parseSysex(trunc).empty());
        auto badMaker = good; badMaker[1] = 0x42;               // not Yamaha
        CHECK(dx7::parseSysex(badMaker).empty());
        auto badSub = good; badSub[3] = 0x7E;                   // unknown format byte
        CHECK(dx7::parseSysex(badSub).empty());
        uint32_t x = 12345;
        auto rnd = [&](size_t n) { std::vector<uint8_t> b(n); for (auto& v : b) { x = x * 1664525u + 1013904223u; v = (uint8_t)(x >> 24); } return b; };
        CHECK(dx7::parseSysex(rnd(5000)).empty());
        CHECK(dx7::parseSysex(rnd(7)).empty());
        CHECK(dx7::parseSysex(rnd(4096)).size() <= 32);         // raw-payload path: must not crash
        auto single = rnd(163); single[0] = 0xF0; single[1] = 0x43; single[2] = 0x00; single[3] = 0x00;
        CHECK(dx7::parseSysex(single).size() <= 1);             // single-voice header with garbage body: must not crash
    });

    test("delay with NaN / Inf time stays in bounds and finite", [&] {
        // big = 1e30*1e30 = +Inf; nan = Inf-Inf = NaN; both drive a delay time
        const char* p = R"({"voices":1,"modules":[
            {"id":"big","type":"mul","in":{"a":1e30,"b":1e30}},
            {"id":"nan","type":"sub"},
            {"id":"c","type":"const","params":{"value":0.5}},
            {"id":"d1","type":"delay","params":{"max":0.01}},
            {"id":"d2","type":"delay","params":{"max":0.01}},
            {"id":"s","type":"add"}],
            "wires":[["big","nan.a"],["big","nan.b"],["c","d1.in"],["c","d2.in"],["big","d1.time"],["nan","d2.time"],["d1","s.a"],["d2","s.b"]],
            "out":"s.out"})";
        Engine e(compilePatch(p, L)); e.prepare(48000); e.noteOn(60, 1.f);
        auto x = render(e, 4800);
        for (float v : x) CHECK(std::isfinite(v));
        CHECK(e.activeVoices() == 1);
    });

    test("a NaN in one voice does not silence the others", [&] {
        // notes above 60.5 produce Inf * 0 = NaN, lower notes output a steady 0.25
        const char* p = R"({"voices":4,"modules":[
            {"id":"p","type":"pitch"},
            {"id":"g","type":"gt","in":{"b":60.5}},
            {"id":"m1","type":"mul","in":{"b":1e30}},
            {"id":"m2","type":"mul","in":{"b":1e30}},
            {"id":"m3","type":"mul","in":{"b":0}},
            {"id":"s","type":"add","in":{"b":0.25}}],
            "wires":[["p","g.a"],["g","m1.a"],["m1","m2.a"],["m2","m3.a"],["m3","s.a"]],"out":"s.out"})";
        Engine e(compilePatch(p, L)); e.prepare(48000);
        e.noteOn(48, 1.f); e.noteOn(72, 1.f);
        auto x = render(e, 256);
        for (float v : x) CHECK(v == 0.25f);                    // good voice keeps sounding from sample 0
        CHECK(e.activeVoices() == 1);                           // the poisoned voice was dropped
        e.noteOn(72, 1.f);                                      // and the lane is reusable without lingering NaN
        x = render(e, 64);
        for (float v : x) CHECK(v == 0.25f);
    });

    test("delay max is clamped to a sane range", [&] {
        const char* big = R"({"voices":1,"modules":[{"id":"c","type":"const","params":{"value":1}},
            {"id":"d","type":"delay","params":{"max":1e9}}],"wires":[["c","d.in"]],"out":"d.out"})";
        auto prog = compilePatch(big, L);
        CHECK(prog.delays.size() == 1 && prog.delays[0].maxSeconds == 10.f);
        Engine e(std::move(prog)); e.prepare(48000); e.noteOn(60, 1.f);
        for (float v : render(e, 480)) CHECK(std::isfinite(v));
        const char* tiny = R"({"voices":1,"modules":[{"id":"c","type":"const","params":{"value":1}},
            {"id":"d","type":"delay","params":{"max":-5}}],"wires":[["c","d.in"]],"out":"d.out"})";
        CHECK(compilePatch(tiny, L).delays[0].maxSeconds == 0.001f);
    });

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
