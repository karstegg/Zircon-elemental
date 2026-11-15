// zircon — command-line front end for the engine.
//   zircon modules                         list native primitives + library macros
//   zircon info   <patch.json>             compile and show params / program stats
//   zircon render <patch.json> <out.wav> [--score s.json] [--set Name=value] [--sr 48000] [--seconds N]
#include "zircon/compiler.hpp"
#include "zircon/engine.hpp"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace zircon;
using nlohmann::json;

static std::string defaultLibDir(const char* argv0) {
    namespace fs = std::filesystem;
    for (fs::path p : {fs::path("library"), fs::weakly_canonical(fs::path(argv0)).parent_path() / "../library",
                       fs::path(ZIRCON_SOURCE_DIR) / "library"})
        if (fs::exists(p)) return p.string();
    return "library";
}

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    uint32_t n = (uint32_t)L.size(), bytes = n * 2 * 2;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : {L[i], R[i]}) {
            int v = (int)std::lround(std::clamp(s, -1.f, 1.f) * 32767.f);
            u16((uint16_t)(int16_t)v);
        }
}

struct Note { double t, dur; int note; float vel; };

static std::vector<Note> defaultScore(double& length) {
    // arpeggio C minor, then a held chord
    std::vector<Note> s;
    int arp[] = {48, 51, 55, 60, 63, 67, 72, 67};
    for (int i = 0; i < 8; ++i) s.push_back({i * 0.25, 0.2, arp[i], 0.8f});
    for (int n : {48, 55, 60, 63}) s.push_back({2.2, 1.6, n, 0.7f});
    length = 5.5;
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: zircon modules | info <patch> | render <patch> <out.wav> [--score f] [--set k=v] [--sr n] [--seconds n] [--lib dir]\n";
        return 1;
    }
    std::string cmd = argv[1];
    std::string libDir = defaultLibDir(argv[0]);
    std::string scorePath; int sr = 48000; double seconds = -1;
    std::vector<std::pair<std::string, float>> sets;
    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&] { if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; exit(1); } return std::string(argv[++i]); };
        if (a == "--lib") libDir = next();
        else if (a == "--score") scorePath = next();
        else if (a == "--sr") sr = std::stoi(next());
        else if (a == "--seconds") seconds = std::stod(next());
        else if (a == "--set") { auto kv = next(); auto e = kv.find('='); sets.push_back({kv.substr(0, e), std::stof(kv.substr(e + 1))}); }
        else pos.push_back(a);
    }

    try {
        MacroLibrary lib; lib.loadDirectory(libDir + "/macros");
        if (cmd == "modules") {
            std::string cat;
            for (auto& s : opCatalogue()) {
                std::string ins, outs;
                for (auto& p : s.inputs) ins += (ins.empty() ? "" : ",") + p.name;
                for (auto& o : s.outputs) outs += (outs.empty() ? "" : ",") + o;
                printf("  %-9s %-9s (%s) -> (%s)  %s\n", s.category.c_str(), s.type.c_str(), ins.c_str(), outs.c_str(), s.doc.c_str());
            }
            printf("\nmacros (library/macros):\n");
            for (auto& n : lib.names()) {
                auto j = json::parse(lib.get(n));
                printf("  %-12s %s\n", n.c_str(), j.value("doc", "").c_str());
            }
            return 0;
        }
        if (pos.empty()) { std::cerr << "missing patch path\n"; return 1; }
        Program prog = compilePatchFile(pos[0], lib);
        if (cmd == "info") {
            printf("%s: %zu instructions, %d registers, %d state floats/voice, %d voices\n",
                   prog.name.c_str(), prog.code.size(), prog.numRegs, prog.stateFloats, prog.voices);
            for (auto& p : prog.params)
                printf("  param %-12s %g..%g default %g%s\n", p.name.c_str(), p.min, p.max, p.def, p.expCurve ? " (exp)" : "");
            return 0;
        }
        if (cmd != "render" || pos.size() < 2) { std::cerr << "unknown command\n"; return 1; }

        Engine eng(prog);
        eng.prepare(sr);
        for (auto& [k, v] : sets) {
            int i = eng.findParam(k);
            if (i < 0) { std::cerr << "no param named '" << k << "'\n"; return 1; }
            eng.setParam(i, v);
        }
        double length = 0;
        std::vector<Note> score;
        if (!scorePath.empty()) {
            std::ifstream f(scorePath); json j = json::parse(f);
            length = j.value("length", 4.0);
            for (auto& n : j["notes"]) score.push_back({n[0].get<double>(), n[1].get<double>(), n[2].get<int>(), n.size() > 3 ? n[3].get<float>() : 0.8f});
        } else score = defaultScore(length);
        if (seconds > 0) length = seconds;

        // event list (sample-accurate at block boundaries of 32)
        struct Ev { long s; int note; float vel; };
        std::vector<Ev> ev;
        for (auto& n : score) { ev.push_back({(long)(n.t * sr), n.note, n.vel}); ev.push_back({(long)((n.t + n.dur) * sr), n.note, 0.f}); }
        std::sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.s < b.s || (a.s == b.s && a.vel < b.vel); });

        long total = (long)(length * sr);
        std::vector<float> L(total), R(total);
        size_t e = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (long s = 0; s < total;) {
            while (e < ev.size() && ev[e].s <= s) {
                if (ev[e].vel > 0) eng.noteOn(ev[e].note, ev[e].vel); else eng.noteOff(ev[e].note);
                ++e;
            }
            long until = e < ev.size() ? std::min<long>(ev[e].s, total) : total;
            int n = (int)std::max<long>(1, std::min<long>(until - s, 256));
            n = (int)std::min<long>(n, total - s);
            eng.process(&L[s], &R[s], n);
            s += n;
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        float peak = 0; double rms = 0;
        for (long i = 0; i < total; ++i) { peak = std::max(peak, std::fabs(L[i])); rms += L[i] * L[i]; }
        rms = std::sqrt(rms / std::max<long>(1, total));
        writeWav(pos[1], L, R, sr);
        printf("%s -> %s  %.2fs audio in %.3fs (%.0fx realtime)  peak %.3f  rms %.3f\n",
               prog.name.c_str(), pos[1].c_str(), length, secs, length / secs, peak, rms);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 2;
    }
}
