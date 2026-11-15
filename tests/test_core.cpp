// Minimal self-contained test runner for the core engine.
#include "zircon/compiler.hpp"
#include "zircon/engine.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace zircon;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static MacroLibrary lib() { MacroLibrary l; l.loadDirectory(ZIRCON_SOURCE_DIR "/library/macros"); return l; }

static std::vector<float> render(Engine& e, int n) {
    std::vector<float> L(n), R(n); e.process(L.data(), R.data(), n); return L;
}

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

    test("unknown module and bad port give errors", [&] {
        int errs = 0;
        try { compilePatch(R"({"modules":[{"id":"x","type":"nope"}],"out":"x.out"})", L); } catch (const CompileError&) { ++errs; }
        try { compilePatch(R"({"modules":[{"id":"x","type":"add"}],"wires":[["x","x.zz"]],"out":"x.out"})", L); } catch (const CompileError&) { ++errs; }
        CHECK(errs == 2);
    });

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
