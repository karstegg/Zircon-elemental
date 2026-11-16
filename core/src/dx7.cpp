#include "zircon/dx7.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace zircon::dx7 {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kModCycles = 1.0f;       // phase-mod depth of a full-scale (OL 99, EG 99) operator, in cycles
constexpr float kDbPerUnit = 0.75f;      // DX7 level steps are 0.75 dB

int clampi(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

// DX7 level (0..99) -> level units (0..127, 127 = 0 dB), with the hardware's compressed low end.
float scaleLevel(int l) {
    static const int lut[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 44, 45};
    l = clampi(l, 0, 99);
    return l >= 20 ? float(28 + l) : float(lut[l]);
}

// Algorithms. Spec: carriers (operator digits), modulation pairs "ab" (a modulates b), feedback "sd".
struct AlgoSpec { const char* carriers; const char* mods; const char* fb; };
const AlgoSpec kSpecs[32] = {
    {"13", "21 65 54 43", "66"}, {"13", "21 65 54 43", "22"},
    {"14", "32 21 65 54", "66"}, {"14", "32 21 65 54", "46"},   // 4: feedback loop 4 -> 6 (cross-feedback)
    {"135", "21 43 65", "66"},   {"135", "21 43 65", "56"},     // 6: feedback loop 5 -> 6 (cross-feedback)
    {"13", "21 65 53 43", "66"}, {"13", "21 65 53 43", "44"}, {"13", "21 65 53 43", "22"},
    {"14", "32 21 64 54", "33"}, {"14", "32 21 64 54", "66"},
    {"13", "21 63 53 43", "22"}, {"13", "21 63 53 43", "66"},
    {"13", "21 65 64 53 43", "66"}, {"13", "21 65 64 53 43", "22"},
    {"1", "21 31 43 51 65", "66"}, {"1", "21 31 43 51 65", "22"},
    {"1", "21 31 41 54 65", "33"},
    {"145", "32 21 65 64", "66"},
    {"124", "31 32 64 54", "33"},
    {"1245", "31 32 64 65", "33"},
    {"1345", "21 63 64 65", "66"},
    {"1245", "32 64 65", "66"},
    {"12345", "63 64 65", "66"},
    {"12345", "64 65", "66"},
    {"124", "32 64 54", "66"}, {"124", "32 64 54", "33"},
    {"136", "21 54 43", "55"},
    {"1235", "43 65", "66"},
    {"1236", "54 43", "55"},
    {"12345", "65", "66"},
    {"123456", "", "66"},
};

std::array<Algo, 32> buildAlgos() {
    std::array<Algo, 32> t{};
    for (int a = 0; a < 32; ++a) {
        Algo g{};
        for (const char* c = kSpecs[a].carriers; *c; ++c) g.carriers |= uint8_t(1u << (*c - '1'));
        const char* m = kSpecs[a].mods;
        while (*m) {
            if (*m == ' ') { ++m; continue; }
            int src = m[0] - '1', dst = m[1] - '1';
            g.mods[dst] |= uint8_t(1u << src);
            m += 2;
        }
        g.fbSrc = int8_t(kSpecs[a].fb[0] - '1');
        g.fbDst = int8_t(kSpecs[a].fb[1] - '1');
        t[(size_t)a] = g;
    }
    return t;
}

float levelScaleDelta(const OpParams& op, int note) {   // keyboard level scaling; + = louder (DX units)
    float d = float(note - (op.bp + 21));
    const bool left = d < 0.f;
    float grp = std::min(33.f, std::fabs(d) / 3.f);
    const int depth = left ? op.ld : op.rd;
    const int curve = left ? op.lc : op.rc;
    float x = grp / 33.f;
    float shape = (curve == 0 || curve == 3) ? x : (std::exp(x * 5.f) - 1.f) / (std::exp(5.f) - 1.f);
    float s = depth / 99.f * shape * 80.f;
    return curve < 2 ? -s : s;
}

// Envelope step in level units. Stage 0-2 move to L1-L3, 3 = hold (key down), 4 = release to L4, 5 = done.
void egStep(float& cur, int& stage, const int rate[4], const int level[4], float dt, int rateScaleAdd, float speedMul) {
    if (stage == 3 || stage == 5) return;
    const int s = stage == 4 ? 3 : stage;
    const float target = scaleLevel(level[s]);
    const int q = clampi(rate[s] * 41 / 64 + rateScaleAdd, 0, 63);
    const float spd = 0.38f * std::exp2(q * 0.25f) * speedMul;
    bool arrived;
    if (cur < target) {
        cur += spd * (0.5f + 3.5f * (1.f - cur / 127.f)) * dt;      // rising: fast from silence, eases near the top
        arrived = cur >= target;
    } else {
        cur -= spd * dt;
        arrived = cur <= target;
    }
    if (arrived) {
        cur = target;
        stage = stage < 2 ? stage + 1 : (stage == 2 ? 3 : 5);
    }
}

} // namespace

const Algo& algorithm(int index) {
    static const std::array<Algo, 32> table = buildAlgos();
    return table[(size_t)clampi(index, 0, 31)];
}

// ------------------------------------------------------------------ voice I/O
bool fromUnpacked(const std::vector<int>& v, Voice& o) {
    if (v.size() < 145) return false;
    for (int i = 0; i < 6; ++i) {
        OpParams& p = o.op[5 - i];                 // data is operator 6 first
        const int* d = &v[(size_t)i * 21];
        for (int k = 0; k < 4; ++k) { p.rate[k] = clampi(d[k], 0, 99); p.level[k] = clampi(d[4 + k], 0, 99); }
        p.bp = clampi(d[8], 0, 99); p.ld = clampi(d[9], 0, 99); p.rd = clampi(d[10], 0, 99);
        p.lc = clampi(d[11], 0, 3); p.rc = clampi(d[12], 0, 3); p.rs = clampi(d[13], 0, 7);
        p.ams = clampi(d[14], 0, 3); p.kvs = clampi(d[15], 0, 7); p.ol = clampi(d[16], 0, 99);
        p.mode = clampi(d[17], 0, 1); p.fc = clampi(d[18], 0, 31); p.ff = clampi(d[19], 0, 99);
        p.det = clampi(d[20], 0, 14);
    }
    for (int k = 0; k < 4; ++k) { o.prate[k] = clampi(v[126 + (size_t)k], 0, 99); o.plevel[k] = clampi(v[130 + (size_t)k], 0, 99); }
    o.algo = clampi(v[134], 0, 31); o.fb = clampi(v[135], 0, 7); o.oks = clampi(v[136], 0, 1);
    o.lfs = clampi(v[137], 0, 99); o.lfd = clampi(v[138], 0, 99); o.lpmd = clampi(v[139], 0, 99);
    o.lamd = clampi(v[140], 0, 99); o.lks = clampi(v[141], 0, 1); o.lfw = clampi(v[142], 0, 5);
    o.lpms = clampi(v[143], 0, 7); o.trnsp = clampi(v[144], 0, 48);
    if (v.size() >= 155) {
        std::string n;
        for (int k = 0; k < 10; ++k) { int c = v[145 + (size_t)k]; n += (c >= 32 && c < 127) ? char(c) : ' '; }
        o.name = n;
    }
    return true;
}

std::vector<int> toUnpacked(const Voice& o) {
    std::vector<int> v(kUnpackedSize, 0);
    for (int i = 0; i < 6; ++i) {
        const OpParams& p = o.op[5 - i];
        int* d = &v[(size_t)i * 21];
        for (int k = 0; k < 4; ++k) { d[k] = p.rate[k]; d[4 + k] = p.level[k]; }
        d[8] = p.bp; d[9] = p.ld; d[10] = p.rd; d[11] = p.lc; d[12] = p.rc; d[13] = p.rs; d[14] = p.ams;
        d[15] = p.kvs; d[16] = p.ol; d[17] = p.mode; d[18] = p.fc; d[19] = p.ff; d[20] = p.det;
    }
    for (int k = 0; k < 4; ++k) { v[126 + (size_t)k] = o.prate[k]; v[130 + (size_t)k] = o.plevel[k]; }
    v[134] = o.algo; v[135] = o.fb; v[136] = o.oks; v[137] = o.lfs; v[138] = o.lfd; v[139] = o.lpmd;
    v[140] = o.lamd; v[141] = o.lks; v[142] = o.lfw; v[143] = o.lpms; v[144] = o.trnsp;
    std::string n = o.name; n.resize(10, ' ');
    for (int k = 0; k < 10; ++k) v[145 + (size_t)k] = (unsigned char)n[(size_t)k];
    return v;
}

bool fromPacked(const uint8_t* p, Voice& o) {
    std::vector<int> u(kUnpackedSize, 0);
    for (int i = 0; i < 6; ++i) {
        const uint8_t* b = p + i * 17;
        int* d = &u[(size_t)i * 21];
        for (int k = 0; k < 4; ++k) { d[k] = b[k] & 127; d[4 + k] = b[4 + k] & 127; }
        d[8] = b[8] & 127; d[9] = b[9] & 127; d[10] = b[10] & 127;
        d[11] = b[11] & 3; d[12] = (b[11] >> 2) & 3;
        d[13] = b[12] & 7; d[20] = (b[12] >> 3) & 15;
        d[14] = b[13] & 3; d[15] = (b[13] >> 2) & 7;
        d[16] = b[14] & 127;
        d[17] = b[15] & 1; d[18] = (b[15] >> 1) & 31;
        d[19] = b[16] & 127;
    }
    for (int k = 0; k < 8; ++k) u[126 + (size_t)k] = p[102 + k] & 127;
    u[134] = p[110] & 31;
    u[135] = p[111] & 7; u[136] = (p[111] >> 3) & 1;
    u[137] = p[112] & 127; u[138] = p[113] & 127; u[139] = p[114] & 127; u[140] = p[115] & 127;
    u[141] = p[116] & 1; u[142] = (p[116] >> 1) & 7; u[143] = (p[116] >> 4) & 7;
    u[144] = p[117] & 127;
    for (int k = 0; k < 10; ++k) u[145 + (size_t)k] = p[118 + k] & 127;
    return fromUnpacked(u, o);
}

std::vector<Voice> parseSysex(const std::vector<uint8_t>& b) {
    std::vector<Voice> out;
    auto isHeader = [&](uint8_t sub, size_t len) {
        return b.size() >= len && b[0] == 0xF0 && b[1] == 0x43 && (b[2] & 0xF0) == 0x00 && b[3] == sub;
    };
    if (isHeader(0x09, 4104)) {                       // 32-voice bulk dump
        for (int i = 0; i < 32; ++i) {
            Voice v;
            if (fromPacked(&b[6 + (size_t)i * 128], v)) out.push_back(v);
        }
    } else if (isHeader(0x00, 163)) {                 // single voice, unpacked
        std::vector<int> u(b.begin() + 6, b.begin() + 6 + 155);
        Voice v;
        if (fromUnpacked(u, v)) out.push_back(v);
    } else if (b.size() == 4096) {                    // raw bulk payload
        for (int i = 0; i < 32; ++i) {
            Voice v;
            if (fromPacked(&b[(size_t)i * 128], v)) out.push_back(v);
        }
    }
    return out;
}

void toPacked(const Voice& o, uint8_t* p) {
    const std::vector<int> u = toUnpacked(o);
    for (int i = 0; i < 128; ++i) p[i] = 0;
    for (int i = 0; i < 6; ++i) {
        uint8_t* b = p + i * 17;
        const int* d = &u[(size_t)i * 21];
        for (int k = 0; k < 4; ++k) { b[k] = uint8_t(d[k]); b[4 + k] = uint8_t(d[4 + k]); }
        b[8] = uint8_t(d[8]); b[9] = uint8_t(d[9]); b[10] = uint8_t(d[10]);
        b[11] = uint8_t(d[11] | (d[12] << 2));
        b[12] = uint8_t(d[13] | (d[20] << 3));
        b[13] = uint8_t(d[14] | (d[15] << 2));
        b[14] = uint8_t(d[16]);
        b[15] = uint8_t(d[17] | (d[18] << 1));
        b[16] = uint8_t(d[19]);
    }
    for (int k = 0; k < 8; ++k) p[102 + k] = uint8_t(u[126 + (size_t)k]);
    p[110] = uint8_t(u[134]);
    p[111] = uint8_t(u[135] | (u[136] << 3));
    for (int k = 0; k < 4; ++k) p[112 + k] = uint8_t(u[137 + (size_t)k]);
    p[116] = uint8_t(u[141] | (u[142] << 1) | (u[143] << 4));
    p[117] = uint8_t(u[144]);
    for (int k = 0; k < 10; ++k) p[118 + k] = uint8_t(u[145 + (size_t)k] & 127);
}

std::vector<uint8_t> toSysex(const std::vector<Voice>& voices) {
    std::vector<uint8_t> out = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    int sum = 0;
    for (int i = 0; i < 32; ++i) {
        uint8_t blk[128];
        toPacked((size_t)i < voices.size() ? voices[(size_t)i] : Voice(), blk);
        for (int k = 0; k < 128; ++k) { out.push_back(blk[k]); sum += blk[k]; }
    }
    out.push_back(uint8_t((128 - (sum & 127)) & 127));
    out.push_back(0xF7);
    return out;
}

std::vector<Voice> cartFromPatchJson(const std::string& text) {
    std::vector<Voice> out;
    try {
        auto j = nlohmann::json::parse(text);
        if (!j.contains("cart") || !j["cart"].is_array()) return out;
        for (auto& c : j["cart"]) {
            std::vector<int> vals = c.get<std::vector<int>>();
            Voice v;
            if (fromUnpacked(vals, v)) out.push_back(v);
        }
    } catch (...) {}
    return out;
}

std::string patchJson(const Voice& v, int polyphony) {
    using nlohmann::json;
    std::string name = v.name;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) name = "DX7 voice";
    json j;
    j["name"] = "DX7: " + name;
    j["voices"] = polyphony;
    auto param = [](const char* id, const char* n, double lo, double hi, double def, const char* curve = nullptr) {
        json p = {{"name", n}, {"min", lo}, {"max", hi}, {"default", def}};
        if (curve) p["curve"] = curve;
        return json{{"id", id}, {"type", "param"}, {"params", p}};
    };
    j["modules"] = json::array({
        json{{"id", "dx"}, {"type", "dx7"}, {"params", {{"voice", toUnpacked(v)}}}},
        param("lvl", "Level", 0, 1, 0.9),
        param("brt", "Bright", 0, 2, 1),
        param("egs", "EG Speed", 0.25, 4, 1, "exp"),
        param("vib", "Vibrato", 0, 1, 0),
    });
    j["wires"] = json::array({json::array({"lvl", "dx.level"}), json::array({"brt", "dx.bright"}),
                              json::array({"egs", "dx.rate"}), json::array({"vib", "dx.pmd"})});
    j["out"] = "dx.out";
    return j.dump(1);
}

// ------------------------------------------------------------------ lane
void Lane::reset() {
    for (int i = 0; i < 6; ++i) {
        eg_[i] = Eg{}; ph_[i] = y_[i] = amp_[i] = ampTarget_[i] = ampSlope_[i] = inc_[i] = noteAtt_[i] = 0.f;
        rateScale_[i] = 0;
    }
    fbHist_[0] = fbHist_[1] = 0.f;
    lfoPhase_ = lfoTime_ = 0.f; lfoSh_ = 0.5f; pegCur_ = 0.f; pegStage_ = 5;
    note_ = 60; counter_ = 0; held_ = audible_ = gatePrev_ = false;
}

void Lane::noteOn(const Voice& v, float pitch, float vel) {
    note_ = (int)std::lround(pitch);
    const bool fresh = !audible_;
    const int velInt = clampi((int)std::lround(vel * 127.f), 0, 127);
    for (int i = 0; i < 6; ++i) {
        const OpParams& p = v.op[i];
        rateScale_[i] = (p.rs * clampi(note_ / 3 - 7, 0, 31)) >> 3;
        const float velAtt = (p.kvs / 7.f) * 0.5f * float(127 - velInt);
        noteAtt_[i] = velAtt - levelScaleDelta(p, note_);
        if (fresh) eg_[i].cur = scaleLevel(p.level[3]);
        eg_[i].stage = 0;
        if (v.oks) ph_[i] = 0.f;
    }
    if (fresh) pegCur_ = (v.plevel[3] - 50) * 48.f / 50.f;
    pegStage_ = 0;
    lfoTime_ = 0.f;
    if (v.lks) lfoPhase_ = 0.f;
    held_ = true;
    counter_ = 0;
}

void Lane::noteOff(const Voice&) {
    held_ = false;
    for (auto& e : eg_) if (e.stage <= 3) e.stage = 4;
    if (pegStage_ <= 3) pegStage_ = 4;
}

void Lane::control(const Voice& v, float pitch, float modWheel, float egSpeed, float vibrato, float sr) {
    const float dt = kBlock / sr;
    static const float kPms[8] = {0.f, 0.2f, 0.4f, 0.8f, 1.5f, 2.5f, 4.5f, 7.f};
    static const float kAms[4] = {0.f, 25.f, 55.f, 100.f};

    // LFO
    const float lfHz = 0.062f * std::exp(6.665f * float(v.lfs) / 99.f);
    lfoTime_ += dt;
    lfoPhase_ += lfHz * dt;
    if (lfoPhase_ >= 1.f) {
        lfoPhase_ -= std::floor(lfoPhase_);
        lfoSh_ = float((int)(std::fmod(lfoSh_ * 9301.f + 49297.f, 233280.f))) / 233280.f;   // cheap S&H source
    }
    float raw = 0.f;
    switch (v.lfw) {
    case 0: raw = lfoPhase_ < 0.5f ? 2.f * lfoPhase_ : 2.f - 2.f * lfoPhase_; break;
    case 1: raw = 1.f - lfoPhase_; break;
    case 2: raw = lfoPhase_; break;
    case 3: raw = lfoPhase_ < 0.5f ? 1.f : 0.f; break;
    case 4: raw = 0.5f + 0.5f * std::sin(kTwoPi * lfoPhase_); break;
    default: raw = lfoSh_; break;
    }
    float fade = 1.f;
    if (v.lfd > 0) {
        const float delay = float(v.lfd) / 99.f * 4.f;
        fade = std::min(1.f, std::max(0.f, (lfoTime_ - delay) / std::max(0.3f, delay * 0.5f)));
    }
    const float pmd = std::min(1.f, float(v.lpmd) / 99.f + modWheel + vibrato);
    const float lfoSemis = kPms[v.lpms] * pmd * (2.f * raw - 1.f) * fade;
    const float amEff = float(v.lamd) / 99.f * raw * fade;

    // pitch envelope
    if (pegStage_ != 3 && pegStage_ != 5) {
        const int s = pegStage_ == 4 ? 3 : pegStage_;
        const float target = (v.plevel[s] - 50) * 48.f / 50.f;
        const int q = clampi(v.prate[s] * 41 / 64, 0, 63);
        const float spd = 0.8f * std::exp2(q * 0.25f) * egSpeed;
        bool arrived;
        if (pegCur_ < target) { pegCur_ += spd * dt; arrived = pegCur_ >= target; }
        else { pegCur_ -= spd * dt; arrived = pegCur_ <= target; }
        if (arrived) { pegCur_ = target; pegStage_ = pegStage_ < 2 ? pegStage_ + 1 : (pegStage_ == 2 ? 3 : 5); }
    }

    const float noteSemis = pitch + float(v.trnsp - 24) + pegCur_ + lfoSemis;
    const float baseHz = 440.f * std::exp2((noteSemis - 69.f) / 12.f);
    const Algo& A = algorithm(v.algo);
    bool audible = false;
    for (int i = 0; i < 6; ++i) {
        const OpParams& p = v.op[i];
        egStep(eg_[i].cur, eg_[i].stage, p.rate, p.level, dt, rateScale_[i], egSpeed);
        float att = (127.f - eg_[i].cur) + (127.f - scaleLevel(p.ol)) + noteAtt_[i] + kAms[p.ams] * amEff;
        att = std::max(att, -10.f);
        ampTarget_[i] = att >= 125.f ? 0.f : std::exp2(-att * kDbPerUnit / 6.0206f);
        ampSlope_[i] = (ampTarget_[i] - amp_[i]) / float(kBlock);
        float hz;
        if (p.mode == 1) hz = std::pow(10.f, float(p.fc & 3) + float(p.ff) * 0.01f);
        else {
            const float ratio = (p.fc == 0 ? 0.5f : float(p.fc)) * (1.f + float(p.ff) * 0.01f);
            hz = baseHz * ratio * std::exp2(float(p.det - 7) * 1.4f / 1200.f);
        }
        inc_[i] = hz / sr;
        if ((A.carriers >> i) & 1 && ampTarget_[i] > 1e-4f) audible = true;
    }
    audible_ = audible;
}

float Lane::tick(const Voice& v, float pitch, bool gate, float vel, float modWheel,
                 float level, float bright, float egSpeed, float vibrato, float sr) {
    if (gate && !gatePrev_) noteOn(v, pitch, vel);
    else if (!gate && gatePrev_) noteOff(v);
    gatePrev_ = gate;
    if (!held_ && !audible_) return 0.f;
    if (counter_ == 0) control(v, pitch, modWheel, egSpeed, vibrato, sr);
    counter_ = (counter_ + 1) % kBlock;

    const Algo& A = algorithm(v.algo);
    const float fbAmt = v.fb > 0 ? std::exp2(float(v.fb - 7)) * 0.5f : 0.f;     // cycles at full scale
    const float fbMod = (fbHist_[0] + fbHist_[1]) * 0.5f * fbAmt;
    const float modScale = kModCycles * bright;
    float out = 0.f;
    int nCar = 0;
    for (int i = 5; i >= 0; --i) {
        amp_[i] += ampSlope_[i];
        float m = 0.f;
        for (int j = 0; j < 6; ++j) if ((A.mods[i] >> j) & 1) m += y_[j];
        m *= modScale;
        if (A.fbDst == i) m += fbMod;
        const float p = ph_[i] + m;
        y_[i] = amp_[i] > 1e-6f ? std::sin(kTwoPi * (p - std::floor(p))) * amp_[i] : 0.f;
        ph_[i] += inc_[i];
        if (ph_[i] >= 1.f) ph_[i] -= std::floor(ph_[i]);
        if ((A.carriers >> i) & 1) { out += y_[i]; ++nCar; }
    }
    fbHist_[1] = fbHist_[0];
    fbHist_[0] = y_[A.fbSrc];
    return out * (0.4f / std::sqrt(float(std::max(1, nCar)))) * level;
}

} // namespace zircon::dx7
