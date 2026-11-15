#include "primal/engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace primal {

static constexpr float kPi = 3.14159265358979f;

Engine::Engine(Program p) : prog_(std::move(p)) {
    lanes_ = prog_.voices;
    voices_.resize(lanes_);
    paramTarget_.resize(prog_.params.size());
    paramValue_.resize(prog_.params.size());
    for (size_t i = 0; i < prog_.params.size(); ++i) paramTarget_[i] = paramValue_[i] = prog_.params[i].def;
    prepare(48000.0);
}

void Engine::prepare(double sampleRate) {
    sr_ = sampleRate;
    regs_.assign((size_t)std::max(1, prog_.numRegs) * kMaxLanes, 0.f);
    state_.assign((size_t)std::max(1, prog_.stateFloats) * kMaxLanes, 0.f);
    rng_.assign((size_t)std::max(1, prog_.stateFloats) * kMaxLanes, 0);
    for (size_t i = 0; i < rng_.size(); ++i) rng_[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    delays_.clear();
    for (auto& d : prog_.delays)
        for (int l = 0; l < kMaxLanes; ++l) {
            DelayBuf b; b.size = (int)std::ceil(d.maxSeconds * sr_) + 4; b.buf.assign(b.size, 0.f);
            delays_.push_back(std::move(b));
        }
    for (auto& [r, v] : prog_.constRegs)
        std::fill_n(&regs_[(size_t)r * kMaxLanes], kMaxLanes, v);
    std::fill_n(&regs_[(size_t)prog_.regSampleRate * kMaxLanes], kMaxLanes, (float)sr_);
    paramCoef_ = 1.f - std::exp(-1.f / (0.005f * (float)sr_));   // 5 ms smoothing
}

void Engine::reset() { prepare(sr_); for (auto& v : voices_) v = Voice{}; }

int Engine::findParam(const std::string& name) const {
    for (size_t i = 0; i < prog_.params.size(); ++i) if (prog_.params[i].name == name) return (int)i;
    return -1;
}
void Engine::setParam(int i, float v) {
    if (i < 0 || i >= numParams()) return;
    auto& p = prog_.params[i];
    paramTarget_[i] = std::clamp(v, std::min(p.min, p.max), std::max(p.min, p.max));
}
void Engine::setParamNormalized(int i, float x) {
    if (i < 0 || i >= numParams()) return;
    paramTarget_[i] = prog_.params[i].fromNormalized(x);
}

// ---------------------------------------------------------------- voices
bool Engine::laneIdle(int l) const {
    if (prog_.adsrStates.empty()) return !voices_[l].gate;
    for (int s : prog_.adsrStates)
        if (state_[(size_t)s * kMaxLanes + l] != 0.f) return false;   // stage 0 == idle
    return true;
}

void Engine::noteOn(int note, float vel) {
    if (vel <= 0.f) { noteOff(note); return; }
    int pick = -1;
    for (int l = 0; l < lanes_; ++l) if (voices_[l].note == note && voices_[l].active) { pick = l; break; }  // retrigger
    if (pick < 0) for (int l = 0; l < lanes_; ++l) if (!voices_[l].active) { pick = l; break; }
    if (pick < 0) {   // steal: prefer released voices, then oldest
        uint64_t best = UINT64_MAX;
        for (int l = 0; l < lanes_; ++l) {
            uint64_t score = voices_[l].age + (voices_[l].gate ? (1ull << 62) : 0);
            if (score < best) { best = score; pick = l; }
        }
    }
    voices_[pick] = Voice{note, true, true, ++clock_, vel};
}

void Engine::noteOff(int note) {
    for (int l = 0; l < lanes_; ++l) if (voices_[l].note == note && voices_[l].gate) voices_[l].gate = false;
}
void Engine::allNotesOff() { for (auto& v : voices_) v.gate = false; }

int Engine::activeVoices() const {
    int n = 0; for (auto& v : voices_) n += v.active; return n;
}

// ---------------------------------------------------------------- DSP
#define R(i) (&regs_[(size_t)(i) * kMaxLanes])
#define LANES for (int v = 0; v < nv; ++v)

void Engine::runSample() {
    const int nv = lanes_;
    const float sr = (float)sr_;
    float* pitch = R(prog_.regPitch); float* gate = R(prog_.regGate); float* vel = R(prog_.regVel);
    LANES { pitch[v] = (float)voices_[v].note + bend_; gate[v] = voices_[v].gate ? 1.f : 0.f; vel[v] = voices_[v].vel; }
    for (size_t i = 0; i < paramValue_.size(); ++i) {
        paramValue_[i] += (paramTarget_[i] - paramValue_[i]) * paramCoef_;
        std::fill_n(R(prog_.paramRegs[i]), nv, paramValue_[i]);
    }

    for (const Instr& I : prog_.code) {
        float* o = I.out[0] >= 0 ? R(I.out[0]) : nullptr;
        const float* a = I.in[0] >= 0 ? R(I.in[0]) : nullptr;
        const float* b = I.in[1] >= 0 ? R(I.in[1]) : nullptr;
        const float* c = I.in[2] >= 0 ? R(I.in[2]) : nullptr;
        switch (I.op) {
        case Op::Const: LANES o[v] = I.p0; break;
        case Op::Add: LANES o[v] = a[v] + b[v]; break;
        case Op::Sub: LANES o[v] = a[v] - b[v]; break;
        case Op::Mul: LANES o[v] = a[v] * b[v]; break;
        case Op::Div: LANES o[v] = b[v] != 0.f ? a[v] / b[v] : 0.f; break;
        case Op::Min: LANES o[v] = std::fmin(a[v], b[v]); break;
        case Op::Max: LANES o[v] = std::fmax(a[v], b[v]); break;
        case Op::Neg: LANES o[v] = -a[v]; break;
        case Op::Abs: LANES o[v] = std::fabs(a[v]); break;
        case Op::Sin: LANES o[v] = std::sin(a[v]); break;
        case Op::Cos: LANES o[v] = std::cos(a[v]); break;
        case Op::Tan: LANES o[v] = std::tan(a[v]); break;
        case Op::Tanh: LANES o[v] = std::tanh(a[v]); break;
        case Op::Exp: LANES o[v] = std::exp(std::fmin(a[v], 80.f)); break;
        case Op::Log: LANES o[v] = std::log(std::fmax(a[v], 1e-12f)); break;
        case Op::Pow: LANES { float r = std::pow(a[v], b[v]); o[v] = std::isfinite(r) ? r : 0.f; } break;
        case Op::Sqrt: LANES o[v] = std::sqrt(std::fmax(a[v], 0.f)); break;
        case Op::Floor: LANES o[v] = std::floor(a[v]); break;
        case Op::Frac: LANES o[v] = a[v] - std::floor(a[v]); break;
        case Op::Gt: LANES o[v] = a[v] > b[v] ? 1.f : 0.f; break;
        case Op::Select: LANES o[v] = a[v] > 0.5f ? b[v] : c[v]; break;
        case Op::Clip: LANES o[v] = std::fmin(std::fmax(a[v], b[v]), c[v]); break;
        case Op::Mtof: LANES o[v] = 440.f * std::exp2((a[v] - 69.f) / 12.f); break;

        case Op::Z1: { const float* s = &state_[(size_t)I.state * kMaxLanes]; LANES o[v] = s[v]; } break;
        case Op::Z1Write: { float* s = &state_[(size_t)I.state * kMaxLanes]; LANES s[v] = a[v]; } break;

        case Op::Delay: {  // read; time is latched by the write at the end of the previous sample
            const float* tm = &state_[(size_t)I.state * kMaxLanes];
            LANES {
                DelayBuf& d = delays_[(size_t)I.slot * kMaxLanes + v];
                float t = std::clamp(tm[v] * sr, 1.f, (float)(d.size - 3));
                float rp = (float)d.pos - t;
                while (rp < 0.f) rp += (float)d.size;
                int i0 = (int)rp; float fr = rp - (float)i0; int i1 = (i0 + 1) % d.size;
                o[v] = d.buf[i0] + (d.buf[i1] - d.buf[i0]) * fr;
            }
        } break;
        case Op::DelayWrite: {
            float* tm = &state_[(size_t)I.state * kMaxLanes];
            LANES {
                tm[v] = b[v];
                DelayBuf& d = delays_[(size_t)I.slot * kMaxLanes + v];
                d.buf[d.pos] = a[v];
                d.pos = (d.pos + 1) % d.size;
            }
        } break;

        case Op::Noise: {
            uint32_t* s = &rng_[(size_t)I.state * kMaxLanes];
            LANES { uint32_t x = s[v]; x ^= x << 13; x ^= x >> 17; x ^= x << 5; s[v] = x;
                    o[v] = (float)(int32_t)x * (1.f / 2147483648.f); }
        } break;

        case Op::Adsr: {
            // inputs: gate(a) attack(b) decay(c) sustain(in3) release(in4)
            const float* att = b; const float* dec = c; const float* sus = R(I.in[3]); const float* rel = R(I.in[4]);
            float* st = &state_[(size_t)I.state * kMaxLanes];          // stage
            float* lv = st + kMaxLanes;                                  // level
            float* pg = st + 2 * kMaxLanes;                              // previous gate
            LANES {
                bool g = a[v] > 0.5f;
                if (g && pg[v] <= 0.5f) st[v] = 1.f;                     // attack
                if (!g && pg[v] > 0.5f && st[v] != 0.f) st[v] = 4.f;     // release
                pg[v] = g ? 1.f : 0.f;
                float s = std::clamp(sus[v], 0.f, 1.f);
                switch ((int)st[v]) {
                case 1: lv[v] += 1.f / (std::fmax(att[v], 0.0005f) * sr);
                        if (lv[v] >= 1.f) { lv[v] = 1.f; st[v] = 2.f; } break;
                case 2: lv[v] = s + (lv[v] - s) * std::exp(-5.f / (std::fmax(dec[v], 0.001f) * sr));
                        if (std::fabs(lv[v] - s) < 1e-4f) { lv[v] = s; st[v] = 3.f; } break;
                case 3: lv[v] = s; break;
                case 4: lv[v] *= std::exp(-5.f / (std::fmax(rel[v], 0.001f) * sr));
                        if (lv[v] < 1e-4f) { lv[v] = 0.f; st[v] = 0.f; } break;
                default: lv[v] = 0.f;
                }
                o[v] = lv[v];
            }
        } break;

        case Op::Svf: {   // Zavalishin / Cytomic TPT state-variable filter
            float* ic1 = &state_[(size_t)I.state * kMaxLanes]; float* ic2 = ic1 + kMaxLanes;
            float* lp = R(I.out[0]); float* bp = R(I.out[1]); float* hp = R(I.out[2]);
            LANES {
                float fc = std::clamp(b[v], 5.f, 0.49f * sr);
                float g = std::tan(kPi * fc / sr);
                float k = 2.f - 2.f * std::clamp(c[v], 0.f, 0.98f);
                float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                float v3 = a[v] - ic2[v];
                float v1 = a1 * ic1[v] + a2 * v3;
                float v2 = ic2[v] + a2 * ic1[v] + a3 * v3;
                ic1[v] = 2.f * v1 - ic1[v]; ic2[v] = 2.f * v2 - ic2[v];
                lp[v] = v2; bp[v] = v1; hp[v] = a[v] - k * v1 - v2;
            }
        } break;

        default: break;
        }
    }
}

void Engine::process(float* left, float* right, int n) {
    for (int i = 0; i < n; ++i) {
        runSample();
        const float* l = R(prog_.outL); const float* r = R(prog_.outR);
        float sl = 0.f, sr = 0.f;
        for (int v = 0; v < lanes_; ++v) {
            if (!voices_[v].active) continue;
            sl += l[v]; sr += r[v];
        }
        if (!std::isfinite(sl)) sl = 0.f;
        if (!std::isfinite(sr)) sr = 0.f;
        left[i] = std::clamp(sl, -4.f, 4.f);
        right[i] = std::clamp(sr, -4.f, 4.f);
        // free voices whose envelopes finished (checked every 32 samples)
        if ((i & 31) == 0)
            for (int v = 0; v < lanes_; ++v)
                if (voices_[v].active && !voices_[v].gate && laneIdle(v)) voices_[v].active = false;
    }
}

} // namespace primal
