// Hanomi native audio v4 - a faithful C++ port of js/audio.js (the Web Audio engine of the original game).
// Same graph: voices -> buses (gain/pan/reverb send) -> sfx/music/ambient buses -> master -> compressor.
// Same synthesis: oscillators (sine/triangle/saw/square), Chrome-style biquads (Q in dB for LP/HP), exponential/linear/target
// automation, Karplus-Strong koto, flute, pad, taiko, bell, horn, generated stereo reverb IR (FFT partitioned convolver with the
// Web Audio normalisation), DynamicsCompressor curve (+makeup gain), generative music (title/day/night/battle) and ambience beds.
#pragma once
#include "core.h"
#include <mutex>

namespace Audio {
constexpr int SR = 44100, BLK = 512, CTL = 16;
enum { MU_OFF = 0, MU_TITLE, MU_DAY, MU_NIGHT, MU_BATTLE };

static inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
static inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static u64 rs_ = 0x9E3779B97F4A7C15ull;
static inline double rnd01() { rs_ ^= rs_ << 13; rs_ ^= rs_ >> 7; rs_ ^= rs_ << 17; return (double)(rs_ >> 11) * (1.0 / 9007199254740992.0); }
static inline double rnd(double a, double b) { return a + rnd01() * (b - a); }

/* ---------------- AudioParam automation (closed form, evaluated at control rate) ---------------- */
struct Param {
  struct Ev { int ty; double t, v, tc; };
  Ev e[8]; int n = 0; double init = 0;
  Param() {} explicit Param(double v) : init(v) {}
  void add(int ty, double t, double v, double tc = 0) { if (n >= 8) return; int i = n++; while (i > 0 && e[i - 1].t > t) { e[i] = e[i - 1]; i--; } e[i] = {ty, t, v, tc}; }
  void set(double t, double v) { add(0, t, v); } void lin(double t, double v) { add(1, t, v); } void ex(double t, double v) { add(2, t, v); } void target(double t, double v, double tc) { add(3, t, v, tc); }
  double v0(int i) const { return e[i].ty == 3 ? (i > 0 ? seg(i - 1, e[i].t) : init) : e[i].v; }
  double seg(int i, double t) const {
    if (i + 1 < n && (e[i + 1].ty == 1 || e[i + 1].ty == 2)) {
      const Ev& b = e[i + 1]; double a0 = v0(i), dt = b.t - e[i].t; if (dt <= 0 || t >= b.t) return b.v; double f = (t - e[i].t) / dt;
      if (b.ty == 1) return a0 + (b.v - a0) * f; if (a0 <= 0 || b.v <= 0) return a0; return a0 * std::pow(b.v / a0, f);
    }
    if (e[i].ty == 3) return e[i].v + (v0(i) - e[i].v) * std::exp(-(t - e[i].t) / e[i].tc);
    return e[i].v;
  }
  double at(double t) const { if (!n) return init; if (t < e[0].t) return init; int i = 0; while (i + 1 < n && e[i + 1].t <= t) i++; return seg(i, t); }
};

/* ---------------- Chrome-style biquad ---------------- */
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void coef(double c0, double c1, double c2, double d1, double d2) { b0 = (float)c0; b1 = (float)c1; b2 = (float)c2; a1 = (float)d1; a2 = (float)d2; }
  void lowpass(double f, double resDb) {
    double c = clampd(f / (SR * 0.5), 0, 1); if (c >= 1) { coef(1, 0, 0, 0, 0); return; } if (c <= 0) { coef(0, 0, 0, 0, 0); return; }
    double g = std::pow(10.0, 0.05 * std::max(0.0, resDb)); double d = std::sqrt((4 - std::sqrt(std::max(0.0, 16 - 16 / (g * g)))) / 2);
    double th = 3.14159265358979 * c, sn = 0.5 * d * std::sin(th), beta = 0.5 * (1 - sn) / (1 + sn), gam = (0.5 + beta) * std::cos(th), al = 0.25 * (0.5 + beta - gam);
    coef(2 * al, 4 * al, 2 * al, 2 * -gam, 2 * beta);
  }
  void highpass(double f, double resDb) {
    double c = clampd(f / (SR * 0.5), 0, 1); if (c >= 1) { coef(0, 0, 0, 0, 0); return; } if (c <= 0) { coef(1, 0, 0, 0, 0); return; }
    double g = std::pow(10.0, 0.05 * std::max(0.0, resDb)); double d = std::sqrt((4 - std::sqrt(std::max(0.0, 16 - 16 / (g * g)))) / 2);
    double th = 3.14159265358979 * c, sn = 0.5 * d * std::sin(th), beta = 0.5 * (1 - sn) / (1 + sn), gam = (0.5 + beta) * std::cos(th), al = 0.25 * (0.5 + beta + gam);
    coef(2 * al, -4 * al, 2 * al, 2 * -gam, 2 * beta);
  }
  void bandpass(double f, double q) {
    double c = clampd(f / (SR * 0.5), 0, 1); if (c <= 0 || c >= 1) { coef(0, 0, 0, 0, 0); return; } if (q <= 0) { coef(1, 0, 0, 0, 0); return; }
    double w0 = 3.14159265358979 * c, al = std::sin(w0) / (2 * q), k = std::cos(w0), a0 = 1 + al; coef(al / a0, 0, -al / a0, -2 * k / a0, (1 - al) / a0);
  }
  void set(int type, double f, double q) { if (type == 0) lowpass(f, q); else if (type == 1) highpass(f, q); else bandpass(f, q); }
  inline float proc(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
enum { F_LP = 0, F_HP = 1, F_BP = 2, F_NONE = -1 };
enum { W_SINE = 0, W_TRI, W_SAW, W_SQ, W_NOISE, W_BUF };

/* ---------------- shared buffers ---------------- */
static std::vector<float> noiseBuf;
struct Pluck { std::vector<int16_t> d; double rate = 1; };
static std::unordered_map<int, Pluck> pluckCache;
static const Pluck& pluckBuf(int midi) {
  auto it = pluckCache.find(midi); if (it != pluckCache.end()) return it->second;
  Pluck& e = pluckCache[midi]; double f = mtof(midi); int N = std::max(2, (int)std::lround(SR / f - 0.5)); double actual = SR / (N + 0.5);
  double dur = midi < 50 ? 3.2 : midi < 70 ? 2.6 : 1.9; int len = (int)std::floor(SR * dur); std::vector<float> d(len);
  double lp = 0; std::vector<float> ex(N); for (int i = 0; i < N; i++) { lp += 0.55 * ((rnd01() * 2 - 1) - lp); ex[i] = (float)lp; }
  int pp = std::max(1, (int)std::floor(N * 0.18)); for (int i = 0; i < N; i++) d[i] = ex[i] - 0.6f * ex[(i + pp) % N];
  double rho = std::pow(0.001, 1.0 / (dur * actual * 1.1)); d[N] = (float)(d[0] * rho);
  for (int i = N + 1; i < len; i++) d[i] = (float)(rho * 0.5 * (d[i - N] + d[i - N - 1]));
  double peak = 0; int scan = std::min(len, N * 6); for (int i = 0; i < scan; i++) peak = std::max(peak, (double)std::fabs(d[i]));
  double norm = peak > 0 ? 0.9 / peak : 1; int fade = (int)std::floor(SR * 0.05); e.d.resize(len);
  for (int i = 0; i < len; i++) { double k = norm; if (i < 16) k *= i / 16.0; if (i > len - fade) k *= (double)(len - i) / fade; e.d[i] = (int16_t)clampd(d[i] * k * 32767.0, -32767, 32767); }
  e.rate = f / actual; return e;
}

/* ---------------- voices & buses ---------------- */
struct Voice {
  u64 s0 = 0, s1 = 0; int bus = -1; u32 gen = 0; int wave = W_SINE; double ph = 0, f0 = 440, detMul = 1; bool hasFreq = false; Param freq;
  double vibRate = 0, vibDepth = 0; bool vibRamp = false; Param vibP; double tStart = 0;
  int ft = F_NONE; double fq = 1; Param ff; bool ffVar = false; Biquad bq; Param gain; float gscale = 1;
  const int16_t* dI = nullptr; int len = 0; double pos = 0, rate = 1;
};
struct Bus {
  bool on = false; u32 gen = 0; int parent = -1, dest = 0; Param gain; bool hasPan = false; float pan = 0, send = 0; u64 killS = ~0ull;
  int ft = F_NONE; double fq = 1; Param ff; Biquad bq; bool counted = false; float buf[BLK];
};
constexpr int MAXB = 192, MAXV = 700;
static Bus buses[MAXB]; static std::vector<int> busOrder; static std::vector<Voice> voices;
static u64 sampleNow = 0; static int sfxVoices = 0; static const int MAX_SFX = 40;
static inline double now() { return (double)sampleNow / SR; }

static int newBus(int parent, int dest, bool hasPan, float pan, float send, double gain, double killAt = -1, bool count = false) {
  for (int i = 0; i < MAXB; i++) if (!buses[i].on) {
    Bus& b = buses[i]; b.on = true; b.gen++; b.parent = parent; b.dest = dest; b.gain = Param(gain); b.hasPan = hasPan; b.pan = pan; b.send = send; b.killS = killAt < 0 ? ~0ull : (u64)(killAt * SR);
    b.ft = F_NONE; b.ff = Param(); b.bq = Biquad(); b.counted = count; busOrder.push_back(i); return i;
  }
  return -1;
}
static void freeBus(int i) { Bus& b = buses[i]; if (b.counted) sfxVoices--; b.on = false; b.gen++; busOrder.erase(std::remove(busOrder.begin(), busOrder.end(), i), busOrder.end()); }

struct TO { double t = 0; int type = W_SINE; double f = 440, f2 = 0, fd = -1, det = 0, a = 0.005, d = 0.1, v = 0.1, lp = 0, lp2 = 0; int ft = F_LP; double q = 0.7, vibRate = 0, vibDepth = 0; };
struct NO { double t = 0; double a = 0.002, d = 0.1, v = 0.1; int type = F_BP; double f = 1000, f2 = 0, q = 1; };

static Voice* addVoice(int bus) {
  if (bus < 0 || voices.size() >= MAXV) return nullptr; voices.emplace_back(); Voice& v = voices.back(); v.bus = bus; v.gen = buses[bus].gen; return &v;
}
// oscillator tone with exp decay (js tone())
static void tone(int out, const TO& o) {
  Voice* v = addVoice(out); if (!v) return; double t = o.t, a = o.a, d = o.d; v->s0 = (u64)std::llround(t * SR); v->s1 = (u64)std::llround((t + a + d + 0.05) * SR); v->tStart = t;
  v->wave = o.type; v->f0 = o.f; v->hasFreq = o.f2 > 0; if (v->hasFreq) { v->freq.set(t, o.f); v->freq.ex(t + (o.fd >= 0 ? o.fd : a + d), std::max(1.0, o.f2)); }
  v->detMul = o.det ? std::pow(2.0, o.det / 1200.0) : 1; v->gain.set(t, 0.0001); v->gain.lin(t + a, o.v); v->gain.ex(t + a + d, 0.0001);
  if (o.lp > 0) { v->ft = o.ft; v->fq = o.q; v->ff.set(t, o.lp); if (o.lp2 > 0) { v->ff.ex(t + a + d, o.lp2); v->ffVar = true; } v->bq.set(v->ft, o.lp, o.q); }
  if (o.vibRate > 0) { v->vibRate = o.vibRate; v->vibDepth = o.vibDepth; }
}
// filtered noise burst (js nz())
static void nz(int out, const NO& o) {
  Voice* v = addVoice(out); if (!v) return; double t = o.t, a = o.a, d = o.d, dur = a + d + 0.05; v->s0 = (u64)std::llround(t * SR); v->s1 = (u64)std::llround((t + dur) * SR); v->tStart = t;
  v->wave = W_NOISE; v->pos = std::floor(rnd01() * std::max(0.0, 1.9 - dur) * SR); v->gain.set(t, 0.0001); v->gain.lin(t + a, o.v); v->gain.ex(t + a + d, 0.0001);
  v->ft = o.type; v->fq = o.q; v->ff.set(t, o.f); if (o.f2 > 0) { v->ff.ex(t + a + d, o.f2); v->ffVar = true; } v->bq.set(v->ft, o.f, o.q);
}
static void koto(int out, double midi, double t, double vol, double pitch = 1, double bright = 1) {
  const Pluck& e = pluckBuf((int)std::lround(midi)); Voice* v = addVoice(out); if (!v) return; double rate = e.rate * pitch; v->wave = W_BUF; v->dI = e.d.data(); v->len = (int)e.d.size(); v->rate = rate;
  v->s0 = (u64)std::llround(t * SR); v->s1 = v->s0 + (u64)std::ceil(v->len / rate) + 1; v->gain = Param(vol); v->ft = F_LP; v->fq = 1; v->ff = Param(std::min(12000.0, mtof(std::lround(midi)) * 7 * bright + 600)); v->bq.set(F_LP, v->ff.init, 1);
}
static void flute(int out, double midi, double t, double dur, double vol) {
  double f = mtof(midi), end = t + dur; double stop = end + 0.8;
  auto envG = [&](Param& g) { g.set(t, 0.0001); g.lin(t + 0.14, vol); g.lin(t + std::max(0.15, dur * 0.7), vol * 0.8); g.target(end, 0.0001, 0.12); };
  double vdepth = f * 0.007, vr = 4.8 + rnd01() * 0.6;
  for (int k = 0; k < 2; k++) {
    Voice* v = addVoice(out); if (!v) return; v->s0 = (u64)std::llround(t * SR); v->s1 = (u64)std::llround(stop * SR); v->tStart = t; v->wave = k ? W_TRI : W_SINE; v->detMul = k ? std::pow(2.0, 4 / 1200.0) : 1;
    v->hasFreq = true; v->freq.set(t, f * 0.982); v->freq.lin(t + 0.09, f); v->vibRate = vr; v->vibRamp = true; v->vibP.set(t, 0); v->vibP.lin(t + std::min(0.7, dur * 0.6), vdepth);
    envG(v->gain); v->gscale = 0.5f; if (k) { v->ft = F_LP; v->fq = 1; v->ff = Param(f * 2.2); v->bq.lowpass(f * 2.2, 1); }
  }
  Voice* n = addVoice(out); if (!n) return; n->s0 = (u64)std::llround(t * SR); n->s1 = (u64)std::llround(stop * SR); n->tStart = t; n->wave = W_NOISE; n->pos = std::floor(rnd01() * SR * 1.9);
  n->ft = F_BP; n->fq = 1.2; n->ff = Param(f * 2); n->bq.bandpass(f * 2, 1.2); n->gain.set(t, 0.0001); n->gain.lin(t + 0.05, vol * 0.35); n->gain.lin(t + 0.25, vol * 0.09); n->gain.target(end, 0.0001, 0.1);
}
static void padNote(int out, std::initializer_list<int> midis, double t, double dur, double vol) {
  double d2 = std::max(1.6, dur), stop = t + d2 + 1.9; int b = newBus(out, 0, false, 0, 0, 1, stop + 0.1); if (b < 0) return; Bus& B = buses[b];
  B.ft = F_LP; B.fq = 0.4; B.ff = Param(650); B.bq.lowpass(650, 0.4); B.gain = Param(); B.gain.set(t, 0.0001); B.gain.lin(t + 1.6, vol); B.gain.set(t + d2, vol); B.gain.lin(t + d2 + 1.8, 0.0001);
  for (int m : midis) for (int det : {-7, 7}) { Voice* v = addVoice(b); if (!v) return; v->wave = W_SAW; v->f0 = mtof(m); v->detMul = std::pow(2.0, det / 1200.0); v->s0 = (u64)std::llround(t * SR); v->s1 = (u64)std::llround(stop * SR); v->gain = Param(1); }
}
static void taiko(int out, double t, double v, bool big = true, double scale = 1) {
  v *= scale; double f0 = big ? 115 : 175;
  { TO o; o.t = t; o.f = f0; o.f2 = f0 * 0.45; o.fd = 0.25; o.a = 0.003; o.d = big ? 0.6 : 0.3; o.v = v; tone(out, o); }
  { TO o; o.t = t; o.f = f0 * 1.6; o.f2 = f0 * 0.8; o.fd = 0.1; o.a = 0.002; o.d = 0.12; o.v = v * 0.25; tone(out, o); }
  { NO o; o.t = t; o.type = F_LP; o.f = big ? 500 : 900; o.d = 0.08; o.v = v * 0.5; nz(out, o); }
}

/* ---------------- SFX bank (every def of js/audio.js SFX) ---------------- */
static const int YO[5] = {0, 2, 5, 7, 9};
static inline void T(int o, double t, int type, double f, double f2, double d, double v, double a = 0.005, double fd = -1) { TO x; x.t = t; x.type = type; x.f = f; x.f2 = f2; x.d = d; x.v = v; x.a = a; x.fd = fd; tone(o, x); }
static inline void TF(int o, double t, int type, double f, double f2, double a, double d, double v, double lp, double q, double lp2 = 0, double rate = 0, double depth = 0, double det = 0) { TO x; x.t = t; x.type = type; x.f = f; x.f2 = f2; x.a = a; x.d = d; x.v = v; x.lp = lp; x.q = q; x.lp2 = lp2; x.vibRate = rate; x.vibDepth = depth; x.det = det; tone(o, x); }
static inline void NZ(int o, double t, int type, double f, double f2, double q, double a, double d, double v) { NO x; x.t = t; x.type = type; x.f = f; x.f2 = f2; x.q = q; x.a = a; x.d = d; x.v = v; nz(o, x); }
#define BP F_BP
#define LPF F_LP
#define HPF F_HP
#define SIN W_SINE
#define TRI W_TRI
#define SAW W_SAW
#define SQR W_SQ

static const char* SFX_NAMES[] = {"chop", "mine", "break", "hit", "hurt", "swing", "pickup", "place", "eat", "splash", "cast", "bite", "reel", "catch", "bark", "growl", "zombie", "zombieDie", "arrow", "coin", "click", "hover", "error", "open", "close", "craft", "till", "water", "plant", "harvest", "door", "step", "bell", "horn", "levelup", "pet", "chicken", "cow", "sheep", "fire", "death", "save", "drum"};
static const float SFX_SEND[] = {0.08f, 0.12f, 0.1f, 0.03f, 0.04f, 0.03f, 0.15f, 0.06f, 0.02f, 0.2f, 0.1f, 0.3f, 0.02f, 0.35f, 0.06f, 0.05f, 0.25f, 0.2f, 0.08f, 0.2f, 0, 0, 0.02f, 0.2f, 0.2f, 0.3f, 0.02f, 0.1f, 0.02f, 0.15f, 0.08f, 0, 0.6f, 0.45f, 0.35f, 0.2f, 0.05f, 0.1f, 0.1f, 0.05f, 0.5f, 0.4f, 0.25f};
constexpr int NSFX = 43;
// returns duration (seconds)
static double sfxDef(int id, int o, double t, double p) {
  switch (id) {
    case 0: /*chop*/ T(o, t, TRI, 230 * p, 140 * p, 0.09, 0.38); T(o, t, SIN, 520 * p, 300 * p, 0.04, 0.16); NZ(o, t, BP, 1700 * p, 0, 1.2, 0.002, 0.08, 0.22); return 0.15;
    case 1: /*mine*/ T(o, t, SIN, 2350 * p, 0, 0.14, 0.1); T(o, t, SIN, 3650 * p, 0, 0.08, 0.05); NZ(o, t, HPF, 3000, 0, 1, 0.002, 0.05, 0.16); NZ(o, t, BP, 850 * p, 0, 0.9, 0.002, 0.1, 0.16); return 0.2;
    case 2: /*break*/ NZ(o, t, LPF, 2200 * p, 300, 1, 0.002, 0.35, 0.3); for (int i = 0; i < 5; i++) NZ(o, t + i * 0.045 + rnd(0, 0.02), BP, rnd(500, 2200) * p, 0, 2, 0.002, 0.05, 0.14); T(o, t, SIN, 140 * p, 60, 0.18, 0.25); return 0.45;
    case 3: /*hit*/ T(o, t, SIN, 165 * p, 70 * p, 0.12, 0.42); NZ(o, t, LPF, 900, 0, 1, 0.002, 0.07, 0.18); return 0.18;
    case 4: /*hurt*/ T(o, t, SIN, 125 * p, 50, 0.2, 0.45); T(o, t + 0.02, TRI, 620 * p, 300 * p, 0.13, 0.11); NZ(o, t, LPF, 600, 0, 1, 0.002, 0.08, 0.15); return 0.25;
    case 5: /*swing*/ NZ(o, t, BP, 600 * p, 2400 * p, 1.6, 0.06, 0.13, 0.32); return 0.22;
    case 6: /*pickup*/ T(o, t, TRI, 880 * p, 0, 0.07, 0.11); T(o, t, SIN, 880 * p, 0, 0.07, 0.06); T(o, t + 0.07, TRI, 1320 * p, 0, 0.16, 0.11); T(o, t + 0.07, SIN, 1320 * p, 0, 0.16, 0.06); return 0.28;
    case 7: /*place*/ T(o, t, TRI, 310 * p, 180 * p, 0.07, 0.32); NZ(o, t, BP, 1200 * p, 0, 2, 0.002, 0.04, 0.18); return 0.12;
    case 8: /*eat*/ for (int i = 0; i < 3; i++) { double tt = t + i * 0.12 + rnd(0, 0.02); NZ(o, tt, BP, rnd(1300, 2600) * p, 0, 0.8, 0.002, 0.06, 0.2 - i * 0.03); NZ(o, tt, HPF, 4000, 0, 1, 0.002, 0.03, 0.07); } return 0.42;
    case 9: /*splash*/ NZ(o, t, LPF, 4200 * p, 500, 1, 0.01, 0.45, 0.3); NZ(o, t + 0.05, BP, 2600 * p, 0, 0.8, 0.002, 0.2, 0.12); T(o, t + 0.02, SIN, 320 * p, 900 * p, 0.08, 0.08); T(o, t + 0.12, SIN, 450 * p, 1200 * p, 0.06, 0.05); return 0.55;
    case 10: /*cast*/ NZ(o, t, BP, 3200 * p, 1100 * p, 6, 0.05, 0.3, 0.14); T(o, t, SIN, 1900 * p, 900 * p, 0.28, 0.025, 0.03); return 0.4;
    case 11: /*bite*/ T(o, t, SIN, 520 * p, 170 * p, 0.1, 0.28); NZ(o, t, LPF, 1500, 0, 1, 0.002, 0.06, 0.1); T(o, t + 0.09, SIN, 2093 * p, 0, 0.6, 0.08, 0.003); T(o, t + 0.09, SIN, 2093 * 2.76 * p, 0, 0.25, 0.02, 0.003); return 0.75;
    case 12: /*reel*/ for (int i = 0; i < 12; i++) { double tt = t + i * 0.026; NZ(o, tt, HPF, 3200, 0, 1, 0.002, 0.012, 0.12); T(o, tt, TRI, 1750 * p, 0, 0.012, 0.04); } return 0.36;
    case 13: /*catch*/ { int ms[4] = {74, 78, 81, 86}; for (int i = 0; i < 4; i++) koto(o, ms[i], t + i * 0.09, 0.32, p); } return 1.6;
    case 14: /*bark*/ TF(o, t, SAW, 720 * p, 380 * p, 0.008, 0.1, 0.16, 1300, 4); T(o, t, SIN, 540 * p, 300 * p, 0.1, 0.14, 0.008); NZ(o, t, BP, 1100, 0, 1.5, 0.002, 0.04, 0.06); return 0.16;
    case 15: /*growl*/ TF(o, t, SAW, 78 * p, 62 * p, 0.12, 0.4, 0.16, 380, 2, 0, 22, 6); NZ(o, t, LPF, 320, 0, 1, 0.1, 0.4, 0.12); return 0.6;
    case 16: /*zombie*/ TF(o, t, SAW, 112 * p, 84 * p, 0.22, 0.6, 0.13, 480, 3, 700, 5, 5); TF(o, t, SAW, 168 * p, 125 * p, 0.25, 0.5, 0.04, 700, 6, 0, 4.4, 6); NZ(o, t, BP, 600, 0, 1, 0.2, 0.5, 0.04); return 0.9;
    case 17: /*zombieDie*/ TF(o, t, SAW, 125 * p, 48 * p, 0.05, 0.65, 0.15, 650, 3, 250, 6, 6); NZ(o, t + 0.3, LPF, 1800, 250, 1, 0.002, 0.3, 0.2); for (int i = 0; i < 4; i++) NZ(o, t + 0.32 + i * 0.05, BP, rnd(400, 1600), 0, 2, 0.002, 0.05, 0.1); return 0.8;
    case 18: /*arrow*/ T(o, t, TRI, 230 * p, 205 * p, 0.16, 0.18); T(o, t, SIN, 460 * p, 0, 0.08, 0.05); NZ(o, t + 0.02, BP, 2200 * p, 800 * p, 3, 0.02, 0.18, 0.12); return 0.25;
    case 19: /*coin*/ TF(o, t, SQR, 988 * p, 0, 0.005, 0.07, 0.06, 3000, 0.7); T(o, t, SIN, 988 * p, 0, 0.07, 0.06); TF(o, t + 0.075, SQR, 1319 * p, 0, 0.005, 0.35, 0.06, 3500, 0.7); T(o, t + 0.075, SIN, 1319 * p, 0, 0.35, 0.07); return 0.5;
    case 20: /*click*/ T(o, t, SIN, 1800 * p, 0, 0.025, 0.08, 0.002); NZ(o, t, HPF, 4000, 0, 1, 0.002, 0.01, 0.04); return 0.05;
    case 21: /*hover*/ T(o, t, SIN, 2400 * p, 0, 0.02, 0.022, 0.002); return 0.04;
    case 22: /*error*/ TF(o, t, SQR, 165 * p, 0, 0.005, 0.09, 0.09, 600, 0.7); TF(o, t + 0.13, SQR, 150 * p, 0, 0.005, 0.1, 0.09, 600, 0.7); return 0.28;
    case 23: /*open*/ NZ(o, t, BP, 400, 1600, 1.2, 0.06, 0.12, 0.06); koto(o, 81, t + 0.04, 0.2, p); return 0.6;
    case 24: /*close*/ NZ(o, t, BP, 1600, 400, 1.2, 0.04, 0.12, 0.06); koto(o, 74, t + 0.03, 0.18, p); return 0.6;
    case 25: /*craft*/ T(o, t, SIN, 2600 * p, 0, 0.2, 0.08); T(o, t, SIN, 3900 * p, 0, 0.1, 0.03); NZ(o, t, BP, 2500, 0, 1.5, 0.002, 0.03, 0.14);
      for (int i = 0; i < 3; i++) { int m = 86 + YO[(int)(rnd01() * 5)]; T(o, t + 0.09 + i * 0.06, SIN, mtof(m + i * 2) * p, 0, 0.18, 0.035); } return 0.45;
    case 26: /*till*/ NZ(o, t, BP, 850 * p, 380 * p, 1, 0.02, 0.16, 0.26); NZ(o, t, LPF, 400, 0, 1, 0.002, 0.1, 0.18); return 0.22;
    case 27: /*water*/ NZ(o, t, BP, 3000, 0, 0.7, 0.1, 0.42, 0.1); for (int i = 0; i < 8; i++) { double f = rnd(800, 1700) * p; T(o, t + rnd(0, 0.45), SIN, f, f * 1.5, 0.04, 0.035, 0.002); } return 0.6;
    case 28: /*plant*/ NZ(o, t, LPF, 520 * p, 0, 1, 0.002, 0.08, 0.42); NZ(o, t + 0.1, LPF, 450 * p, 0, 1, 0.002, 0.07, 0.16); return 0.2;
    case 29: /*harvest*/ T(o, t, SIN, 400 * p, 950 * p, 0.06, 0.18); koto(o, 86, t + 0.03, 0.16, p); NZ(o, t, HPF, 3000, 0, 1, 0.02, 0.14, 0.07); return 0.5;
    case 30: /*door*/ NZ(o, t, BP, 500 * p, 900 * p, 1.2, 0.1, 0.33, 0.18); NZ(o, t, LPF, 220, 0, 1, 0.1, 0.33, 0.1); T(o, t + 0.42, TRI, 130 * p, 0, 0.06, 0.14); NZ(o, t + 0.42, BP, 900, 0, 2, 0.002, 0.03, 0.08); return 0.55;
    case 31: /*step*/ NZ(o, t, LPF, 650 * p, 280, 1, 0.002, 0.06, 0.12); return 0.1;
    case 32: /*bell*/ { double b = 82 * p; static const double parts[9][3] = {{0.5, 0.4, 4.6}, {1, 0.32, 4.2}, {1.004, 0.22, 4.0}, {1.2, 0.16, 3.0}, {1.5, 0.1, 2.4}, {2, 0.13, 2.6}, {2.74, 0.07, 1.7}, {3.76, 0.045, 1.1}, {5.4, 0.025, 0.6}};
      for (auto& q : parts) T(o, t, SIN, b * q[0], 0, q[2], q[1] * 0.55, 0.004); NZ(o, t, LPF, 700, 0, 1, 0.002, 0.15, 0.2); } return 4.8;
    case 33: /*horn*/ { double f = 73.4 * p, d = 2.5; int g = newBus(o, 0, false, 0, 0, 1, t + d + 0.1); if (g < 0) return d; Bus& B = buses[g]; B.ft = F_LP; B.fq = 2; B.ff = Param(); B.ff.set(t, 180); B.ff.lin(t + 0.9, 850); B.ff.lin(t + d, 300); B.bq.lowpass(180, 2);
      B.gain = Param(); B.gain.set(t, 0.0001); B.gain.lin(t + 0.6, 0.16); B.gain.set(t + 1.7, 0.16); B.gain.lin(t + d, 0.0001);
      static const double R[4][2] = {{1, 0}, {1, 9}, {2, -5}, {1.5, 4}};
      for (int i = 0; i < 4; i++) { Voice* v = addVoice(g); if (!v) break; v->wave = W_SAW; v->hasFreq = true; v->freq.set(t, f * R[i][0] * 0.95); v->freq.lin(t + 0.5, f * R[i][0]); v->detMul = std::pow(2.0, R[i][1] / 1200.0);
        v->gscale = i == 3 ? 0.25f : i == 2 ? 0.4f : 0.6f; v->gain = Param(1); v->s0 = (u64)std::llround(t * SR); v->s1 = (u64)std::llround((t + d + 0.05) * SR); }
      NZ(o, t, BP, 400, 0, 1, 0.5, 1.8, 0.03); return d + 0.1; }
    case 34: /*levelup*/ for (int i = 0; i < 8; i++) { int m = 74 + YO[i % 5] + 12 * (i / 5); T(o, t + i * 0.06, TRI, mtof(m) * p, 0, 0.22, 0.08); T(o, t + i * 0.06, SIN, mtof(m) * p * 2, 0, 0.12, 0.025); } T(o, t + 0.5, SIN, mtof(98) * p, 0, 0.5, 0.04); return 1.1;
    case 35: /*pet*/ T(o, t, SIN, 1200 * p, 1800 * p, 0.08, 0.09); T(o, t + 0.09, SIN, 1600 * p, 2400 * p, 0.1, 0.07); T(o, t + 0.16, SIN, 3200 * p, 0, 0.15, 0.025); return 0.35;
    case 36: /*chicken*/ TF(o, t, SAW, 620 * p, 450 * p, 0.005, 0.05, 0.08, 1800, 5); TF(o, t + 0.09, SAW, 720 * p, 500 * p, 0.005, 0.07, 0.09, 1900, 5); TF(o, t + 0.2, SAW, 650 * p, 480 * p, 0.005, 0.05, 0.06, 1800, 5); return 0.3;
    case 37: /*cow*/ TF(o, t, SAW, 145 * p, 108 * p, 0.15, 0.75, 0.13, 420, 3, 700, 4, 2); TF(o, t, SAW, 145 * p, 108 * p, 0.18, 0.7, 0.04, 900, 8, 0, 0, 0, 8); return 0.95;
    case 38: /*sheep*/ TF(o, t, SAW, 340 * p, 310 * p, 0.05, 0.5, 0.08, 1700, 3, 0, 7.5, 14); { TO x; x.t = t; x.f = 340 * p; x.f2 = 310 * p; x.a = 0.05; x.d = 0.45; x.v = 0.04; x.vibRate = 7.5; x.vibDepth = 14; tone(o, x); } return 0.6;
    case 39: /*fire*/ NZ(o, t, LPF, 650, 0, 1, 0.1, 0.35, 0.1); for (int i = 0; i < 9; i++) NZ(o, t + rnd(0, 0.42), HPF, rnd(1800, 5000) * p, 0, 1, 0.002, rnd(0.006, 0.02), rnd(0.04, 0.13)); return 0.5;
    case 40: /*death*/ { int ms[5] = {82, 81, 79, 75, 74}; for (int i = 0; i < 5; i++) koto(o, ms[i] - 12, t + i * 0.3 + (i == 4 ? 0.15 : 0), 0.3, p); koto(o, 50, t + 1.35, 0.22, p); } return 3.2;
    case 41: /*save*/ T(o, t, SIN, 1175 * p, 0, 0.6, 0.08, 0.004); T(o, t, SIN, 1175 * 2.76 * p, 0, 0.2, 0.015, 0.004); T(o, t + 0.12, SIN, 1568 * p, 0, 0.9, 0.08, 0.004); T(o, t + 0.12, SIN, 1568 * 2.76 * p, 0, 0.25, 0.015, 0.004); return 1.1;
    case 42: /*drum*/ taiko(o, t, 0.5, true); return 0.7;
  }
  return 0.5;
}

#undef BP
#undef LPF
#undef HPF
#undef SIN
#undef TRI
#undef SAW
#undef SQR

/* ---------------- music ---------------- */
struct Note { int s, d, len; };
typedef std::vector<Note> Motif;
struct MRng { u32 s; double next() { s += 0x6d2b79f5u; u32 t = s; t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; } };
struct Player { int mode = 0, out = -1; u32 outGen = 0; int bar = 0; double next = 0, sp = 0; MRng rng{1}; bool stopped = false, hasA = false, fl = false; Motif A; Motif phrase[4]; };
static std::vector<Player> players; static int curMode = MU_OFF, seedCounter = 1;
static const int SC_YO[5] = {0, 2, 5, 7, 9}, SC_IN[5] = {0, 1, 5, 7, 8}, SC_HIRA[5] = {0, 2, 3, 7, 8};
static inline int degMidi(int root, const int* sc, int deg) { int n = 5; return root + sc[((deg % n) + n) % n] + 12 * (int)std::floor(deg / (double)n); }
static void fixLens(Motif& m) { for (size_t i = 0; i < m.size(); i++) m[i].len = (i + 1 < m.size() ? m[i + 1].s : 8) - m[i].s; }
static Motif makeMotif(MRng& r, double density, int lo = 1, int hi = 7) {
  Motif n; int d = lo + 1 + (int)std::floor(r.next() * std::max(1, hi - lo - 2));
  for (int s = 0; s < 8; s++) if (r.next() < (s % 2 == 0 ? density : density * 0.45)) { n.push_back({s, d, 0}); double q = r.next(); d += q < 0.35 ? 1 : q < 0.7 ? -1 : q < 0.82 ? 2 : q < 0.94 ? -2 : 0; d = clampi(d, lo, hi); }
  if (n.empty()) n.push_back({0, d, 0}); fixLens(n); return n;
}
static Motif vary(const Motif& m, MRng& r) {
  Motif n = m; double q = r.next();
  if (q < 0.3) { int k = r.next() < 0.5 ? 1 : -1; for (size_t i = 0; i < n.size(); i++) if (i >= n.size() / 2.0) n[i].d += k; }
  else if (q < 0.55) n.back().d += r.next() < 0.5 ? 1 : -1;
  else if (q < 0.75 && n.size() > 2) n.erase(n.begin() + 1 + (int)std::floor(r.next() * (n.size() - 1)));
  else if (q < 0.9) { int a = n[0].d; for (auto& x : n) x.d = a - (x.d - a); }
  for (auto& x : n) x.d = clampi(x.d, 0, 9); fixLens(n); return n;
}
static Motif cadence(const Motif& m) {
  Motif n; for (auto& x : m) if (x.s < 4) n.push_back(x); int last = n.empty() ? 5 : n.back().d; n.push_back({!n.empty() && n.back().s >= 3 ? 5 : 4, last >= 3 ? 5 : 0, 0}); fixLens(n); return n;
}
static void nextPhrase(Player& p, double density, int lo, int hi) {
  if (!p.hasA || (p.bar % 8 == 0 && p.rng.next() < 0.5)) { p.A = makeMotif(p.rng, density, lo, hi); p.hasA = true; }
  Motif B = makeMotif(p.rng, density, lo, hi); Motif v1 = vary(p.A, p.rng); Motif v2 = vary(p.A, p.rng); p.phrase[0] = p.A; p.phrase[1] = v1; p.phrase[2] = B; p.phrase[3] = cadence(v2);
}
static void barTitle(Player& p, double t0, double sp, int out) {
  const int* sc = SC_HIRA; static const int prog[4] = {0, -1, 2, 3}; int r = prog[p.bar % 4]; static const int arp[6] = {0, 2, 4, 5, 4, 2};
  for (int s = 0; s < 6; s++) { if (s > 0 && p.rng.next() < 0.15) continue; koto(out, degMidi(52, sc, r + arp[s]), t0 + s * sp + (p.rng.next() - 0.5) * 0.015, s == 0 ? 0.26 : 0.17); }
  if (p.bar % 2 == 0) padNote(out, {degMidi(40, sc, r), degMidi(40, sc, r) + 7}, t0, sp * 16, 0.035);
  if (p.bar % 8 == 0) p.fl = p.rng.next() < 0.7; if (p.bar % 4 == 0) nextPhrase(p, 0.4, 2, 8);
  if (p.fl && p.bar % 8 >= 4) for (auto& n : p.phrase[p.bar % 4]) flute(out, degMidi(64, sc, n.d), t0 + n.s * sp, n.len * sp * 0.92, 0.09);
}
static void barDay(Player& p, double t0, double sp, int out) {
  const int* sc = SC_YO; static const int prog[4] = {0, 2, 3, 0}; int r = prog[p.bar % 4]; int root = degMidi(50, sc, r);
  if (p.bar % 4 == 0) nextPhrase(p, 0.5, 1, 8); int phraseIdx = p.bar / 4; bool active = phraseIdx % 3 != 2;
  if (p.bar % 2 == 0) padNote(out, {root - 12, root - 5, root}, t0, sp * 16, 0.03);
  if (p.rng.next() < 0.8) koto(out, root, t0, 0.2); if (p.rng.next() < 0.45) koto(out, root + 7, t0 + 4 * sp, 0.13);
  if (active && (p.bar % 4 == 3 || p.rng.next() > 0.15)) {
    for (auto& n : p.phrase[p.bar % 4]) { double tt = t0 + n.s * sp + (p.rng.next() - 0.5) * 0.012; koto(out, degMidi(62, sc, n.d), tt, n.s % 4 == 0 ? 0.24 : 0.18); if (p.rng.next() < 0.2) koto(out, degMidi(62, sc, n.d - 2), tt + 0.02, 0.09); }
  } else if (p.rng.next() < 0.5) koto(out, degMidi(74, sc, (int)std::floor(p.rng.next() * 5)), t0 + (2 + (int)std::floor(p.rng.next() * 4)) * sp, 0.1);
}
static void barNight(Player& p, double t0, double sp, int out) {
  const int* sc = SC_IN; static const int prog[4] = {0, 0, 3, 2}; int r = prog[p.bar % 4]; int root = degMidi(50, sc, r);
  if (p.bar % 4 == 0) nextPhrase(p, 0.32, 1, 7); if (p.bar % 2 == 0) padNote(out, {root - 12, root - 5}, t0, sp * 16, 0.025);
  koto(out, root - 12 < 38 ? root : root - 12, t0, 0.2, 1, 0.7); if (p.rng.next() < 0.4) koto(out, root + 7 - 12, t0 + 5 * sp, 0.12, 1, 0.7);
  int phraseIdx = p.bar / 4;
  if (phraseIdx % 2 == 0 && p.bar % 4 != 2) { for (auto& n : p.phrase[p.bar % 4]) flute(out, degMidi(62, sc, n.d), t0 + n.s * sp, n.len * sp * 0.9, 0.1); }
  else if (p.rng.next() < 0.3) koto(out, degMidi(74, sc, (int)std::floor(p.rng.next() * 5)), t0 + (1 + (int)std::floor(p.rng.next() * 6)) * sp, 0.08);
}
static void barBattle(Player& p, double t0, double sp, int out) {
  const int* sc = SC_IN; static const int o1[8] = {0, 1, 2, 3, 4, 3, 2, 1}, o2[8] = {0, 0, 2, 0, 1, 0, 2, 3}; const int* ost = p.bar % 4 == 3 ? o1 : o2; static const int sh[4] = {0, 0, 2, -1}; int shift = sh[(p.bar / 4) % 4];
  for (int s = 0; s < 8; s++) koto(out, degMidi(45, sc, ost[s] + shift), t0 + s * sp, s % 2 ? 0.08 : 0.12, 1, 0.6);
  if (p.bar % 8 == 7) { taiko(out, t0, 0.42, true, 0.5); taiko(out, t0 + 2 * sp, 0.2, false, 0.5); taiko(out, t0 + 3 * sp, 0.22, false, 0.5); for (int s = 4; s < 8; s++) taiko(out, t0 + s * sp, 0.18 + (s - 4) * 0.06, s == 7, 0.5); }
  else { taiko(out, t0, 0.42, true, 0.5); taiko(out, t0 + 3 * sp, 0.2, false, 0.5); taiko(out, t0 + 4 * sp, 0.34, true, 0.5); taiko(out, t0 + 6 * sp, 0.18, false, 0.5); if (p.rng.next() < 0.4) taiko(out, t0 + 7 * sp, 0.12, false, 0.5); }
  if (p.bar % 4 == 0) padNote(out, {38, 45}, t0, sp * 32, 0.03);
}
static double modeBpm(int m) { return m == MU_TITLE ? 70 : m == MU_DAY ? 84 : m == MU_NIGHT ? 60 : 120; }
static void startPlayer(int mode) {
  double t = now(); int out = newBus(-1, 1, false, 0, 0.4f, 1); if (out < 0) return; Bus& b = buses[out]; b.gain = Param(); b.gain.set(t, 0.0001); b.gain.lin(t + 2, 1);
  Player p; p.mode = mode; p.out = out; p.outGen = b.gen; p.next = t + 0.1; p.sp = 60.0 / modeBpm(mode) / 2; static const int len[5] = {3, 5, 3, 5, 6}; p.rng.s = (u32)((len[mode] * 7919 + seedCounter++ * 104729 + (int)(rnd01() * 1e6)));
  players.push_back(std::move(p));
}
static void fadePlayers() {
  double t = now();
  for (auto& p : players) if (!p.stopped) {
    p.stopped = true; Bus& b = buses[p.out]; if (!b.on || b.gen != p.outGen) continue; double cur = b.gain.at(t); b.gain.n = 0; b.gain.set(t, std::max(0.0001, cur)); b.gain.lin(t + 2, 0.0001); b.killS = (u64)((t + 2.2 + p.sp * 8) * SR);
  }
}
static void musicTick() {
  double t = now();
  for (size_t i = 0; i < players.size();) {
    Player& p = players[i]; if (p.stopped) { Bus& b = buses[p.out]; if (!b.on || b.gen != p.outGen) { players.erase(players.begin() + i); continue; } i++; continue; }
    if (p.next < t - 0.2) p.next = t + 0.05;
    while (p.next < t + 0.3) {
      switch (p.mode) { case MU_TITLE: barTitle(p, p.next, p.sp, p.out); break; case MU_DAY: barDay(p, p.next, p.sp, p.out); break; case MU_NIGHT: barNight(p, p.next, p.sp, p.out); break; case MU_BATTLE: barBattle(p, p.next, p.sp, p.out); break; }
      p.next += p.sp * 8; p.bar++;
    }
    i++;
  }
}
static void setMusicMode(int m) { if (m == curMode) return; curMode = m; fadePlayers(); if (m != MU_OFF) startPlayer(m); }

/* ---------------- ambience beds ---------------- */
struct Loop { int pos = 0; void init() { pos = (int)(rnd01() * 1.9 * SR); } float next() { float v = noiseBuf[pos]; if (++pos >= (int)noiseBuf.size()) pos = 0; return v; } };
static inline float sinp(double ph) { return (float)std::sin(6.283185307179586 * ph); }
static const double AMB_MAX[4] = {0.13, 0.24, 0.03, 0.24};   // rain, wind, night, water
struct Bed {
  bool built = false; float gain = 0, target = 0, last = 0; double t = 0; Loop a, b; Biquad f1, f2, f3, f4; float curve[256];
  void build(int k) {
    built = true; a.init(); b.init();
    if (k == 0) { f1.highpass(900, 0.7); f2.lowpass(6500, 0.7); f3.lowpass(380, 0.7); }
    if (k == 1) { f1.bandpass(480, 0.9); }
    if (k == 2) for (int i = 0; i < 256; i++) { double x = i / 127.5 - 1; curve[i] = (float)clampd((x - 0.45) * 6, 0, 1); }
    if (k == 3) { f1.lowpass(480, 0.7); f2.bandpass(1300, 2); }
  }
  inline float shaper(float x) { float u = (clampd(x, -1, 1) + 1) * 0.5f * 255.f; int i = (int)u; if (i >= 255) return curve[255]; float fr = u - i; return curve[i] + (curve[i + 1] - curve[i]) * fr; }
  void render(int k, float* L, float* R, int n, float smooth) {
    for (int i = 0; i < n; i++) {
      gain += (target - gain) * smooth; t += 1.0 / SR; if (gain < 1e-6f && target < 1e-6f) continue; float o = 0, oL = 0, oR = 0;
      if (k == 0) { float x = a.next(); float ga = (float)(0.55 + 0.12 * sinp(0.11 * t)); o = f2.proc(f1.proc(x)) * ga + f3.proc(b.next()) * 0.7f; oL = oR = o; }
      else if (k == 1) { if ((i & 15) == 0) f1.bandpass(480 + 260 * sinp(0.07 * t), 0.9); float ga = (float)(0.6 + 0.32 * sinp(0.13 * t) + 0.15 * sinp(0.031 * t)); oL = oR = f1.proc(a.next()) * ga; }
      else if (k == 2) {
        static const double P[2][4] = {{4300, 31, 0.83, -0.5}, {4750, 26, 0.61, 0.55}};
        for (int c = 0; c < 2; c++) { float car = sinp(P[c][0] * t); float am = (float)(0.5 + 0.5 * sinp(P[c][1] * t)); float gt = shaper(sinp(P[c][2] * t)); float s = car * am * gt; double x = (P[c][3] + 1) * 0.5; oL += s * (float)std::cos(x * 1.5707963); oR += s * (float)std::sin(x * 1.5707963); }
      } else { if ((i & 15) == 0) f2.bandpass(1300 + 300 * sinp(0.17 * t), 2); float ga = (float)(0.5 + 0.25 * sinp(0.23 * t) + 0.18 * sinp(0.41 * t)); float gb = (float)(0.12 + 0.1 * sinp(0.33 * t)); o = f1.proc(a.next()) * ga + f2.proc(b.next()) * gb; oL = oR = o; }
      L[i] += oL * gain; R[i] += oR * gain;
    }
  }
};
static Bed beds[4]; static float ambTargetV[4] = {0, 0, 0, 0};

/* ---------------- FFT partitioned convolver (reverb) ---------------- */
struct Cx { float r, i; };
constexpr int FN = BLK * 2, FB = FN / 2 + 1;
static Cx tw[FN / 2]; static int brev[FN]; static bool fftInit = false;
static void fftSetup() { if (fftInit) return; fftInit = true; for (int i = 0; i < FN / 2; i++) { double a = -6.283185307179586 * i / FN; tw[i] = {(float)std::cos(a), (float)std::sin(a)}; } int lg = 0; while ((1 << lg) < FN) lg++; for (int i = 0; i < FN; i++) { int r = 0; for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b); brev[i] = r; } }
static void fft(Cx* a, bool inv) {
  for (int i = 0; i < FN; i++) if (i < brev[i]) std::swap(a[i], a[brev[i]]);
  for (int len = 2; len <= FN; len <<= 1) { int h = len >> 1, st = FN / len; for (int i = 0; i < FN; i += len) for (int j = 0; j < h; j++) { Cx w = tw[j * st]; if (inv) w.i = -w.i; Cx& u = a[i + j]; Cx& v = a[i + j + h]; Cx x = {v.r * w.r - v.i * w.i, v.r * w.i + v.i * w.r}; v = {u.r - x.r, u.i - x.i}; u = {u.r + x.r, u.i + x.i}; } }
}
struct Conv {
  int P = 0, pos = 0, silent = 1 << 30; std::vector<Cx> HL, HR, XL, XR; float prevL[BLK] = {}, prevR[BLK] = {}; Cx z[FN], YL[FB], YR[FB];
  void unpack(const Cx* zz, Cx* L, Cx* R) { for (int k = 0; k < FB; k++) { Cx a = zz[k], b = zz[(FN - k) % FN]; b.i = -b.i; L[k] = {(a.r + b.r) * 0.5f, (a.i + b.i) * 0.5f}; Cx d = {a.r - b.r, a.i - b.i}; R[k] = {d.i * 0.5f, -d.r * 0.5f}; } }
  void init(const std::vector<float>& irL, const std::vector<float>& irR) {
    fftSetup(); int len = (int)irL.size(); P = (len + BLK - 1) / BLK; HL.assign((size_t)P * FB, {0, 0}); HR = HL; XL = HL; XR = HL;
    for (int j = 0; j < P; j++) { for (int k = 0; k < FN; k++) { int idx = j * BLK + k; z[k] = {k < BLK && idx < len ? irL[idx] : 0.f, k < BLK && idx < len ? irR[idx] : 0.f}; } fft(z, false); unpack(z, &HL[(size_t)j * FB], &HR[(size_t)j * FB]); }
  }
  void process(const float* inL, const float* inR, float* oL, float* oR) {
    if (!P) { memset(oL, 0, BLK * 4); memset(oR, 0, BLK * 4); return; }
    float pk = 0; for (int i = 0; i < BLK; i++) pk = std::max(pk, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
    if (pk < 1e-9f) { if (silent < P + 2) silent++; } else silent = 0;
    if (silent > P + 1) { memset(oL, 0, BLK * 4); memset(oR, 0, BLK * 4); memset(prevL, 0, sizeof prevL); memset(prevR, 0, sizeof prevR); return; }
    for (int i = 0; i < BLK; i++) { z[i] = {prevL[i], prevR[i]}; z[BLK + i] = {inL[i], inR[i]}; prevL[i] = inL[i]; prevR[i] = inR[i]; }
    fft(z, false); pos = (pos + 1) % P; unpack(z, &XL[(size_t)pos * FB], &XR[(size_t)pos * FB]);
    for (int k = 0; k < FB; k++) YL[k] = YR[k] = {0, 0};
    for (int j = 0; j < P; j++) {
      int q = pos - j; if (q < 0) q += P; const Cx* xl = &XL[(size_t)q * FB]; const Cx* xr = &XR[(size_t)q * FB]; const Cx* hl = &HL[(size_t)j * FB]; const Cx* hr = &HR[(size_t)j * FB];
      for (int k = 0; k < FB; k++) { YL[k].r += xl[k].r * hl[k].r - xl[k].i * hl[k].i; YL[k].i += xl[k].r * hl[k].i + xl[k].i * hl[k].r; YR[k].r += xr[k].r * hr[k].r - xr[k].i * hr[k].i; YR[k].i += xr[k].r * hr[k].i + xr[k].i * hr[k].r; }
    }
    for (int k = 0; k < FB; k++) { z[k] = {YL[k].r - YR[k].i, YL[k].i + YR[k].r}; if (k > 0 && k < FN / 2) z[FN - k] = {YL[k].r + YR[k].i, -YL[k].i + YR[k].r}; }
    z[FN / 2] = {YL[FN / 2].r, YR[FN / 2].r};
    fft(z, true); float sc = 1.f / FN; for (int i = 0; i < BLK; i++) { oL[i] = z[BLK + i].r * sc; oR[i] = z[BLK + i].i * sc; }
  }
};
static Conv conv;

/* ---------------- compressor (Chrome DynamicsCompressor static curve, threshold -18 knee 12 ratio 3 attack .01 release .25) ---------------- */
struct Comp {
  double thr = -18, knee = 12, ratio = 3, lin_thr, kneeThr, ykneeDb, K, makeup, slope; float dl[2][1024] = {}; int dp = 0, delay = 265;
  static double l2d(double x) { return 20 * std::log10(std::max(x, 1e-9)); } static double d2l(double d) { return std::pow(10.0, d / 20); }
  double kneeCurve(double x, double k) const { if (x < lin_thr) return x; return lin_thr + (1 - std::exp(-k * (x - lin_thr))) / k; }
  double slopeAt(double x, double k) const { if (x < lin_thr) return 1; double x2 = x * 1.001; double xd = l2d(x), x2d = l2d(x2), yd = l2d(kneeCurve(x, k)), y2d = l2d(kneeCurve(x2, k)); return (y2d - yd) / (x2d - xd); }
  double sat(double x) const { if (x < kneeThr) return kneeCurve(x, K); double xd = l2d(x); double yd = ykneeDb + slope * (xd - (thr + knee)); return d2l(yd); }
  void init() {
    lin_thr = d2l(thr); slope = 1.0 / ratio; kneeThr = d2l(thr + knee); double mn = 0.1, mx = 10000, k = 5;
    for (int i = 0; i < 15; i++) { double s = slopeAt(kneeThr, k); if (s < slope) mx = k; else mn = k; k = std::sqrt(mn * mx); } K = k;
    ykneeDb = l2d(kneeCurve(kneeThr, K)); makeup = std::pow(1.0 / sat(1.0), 0.6);
  }
  // --- port of Blink DynamicsCompressorKernel::Process (detector with sat-release, adaptive release polynomial, sin warp, 6 ms predelay)
  float det = 1, cg = 1, maxAtt = -1, envRate = 1, scaledDesired = 1; int div = 0; double ca = 0, cb = 0, cc = 0, cd = 0, ce = 0, satRelFrames = 0.0025 * SR, relFrames = 0.25 * SR, attFrames = 0.01 * SR;
  void initDyn() {
    const double z1 = 0.09, z2 = 0.16, z3 = 0.42, z4 = 0.98;
    double A = 0.9999999999999998 * z1 + 1.8432219684323923e-16 * z2 - 1.9373394351676423e-16 * z3 + 8.824516011816245e-18 * z4;
    double B = -1.5788320352845888 * z1 + 2.3305837032074286 * z2 - 0.9141194204840429 * z3 + 0.1623677525612032 * z4;
    double C = 0.5334142869106424 * z1 - 1.272736789213631 * z2 + 0.9258856042207512 * z3 - 0.18656310191776226 * z4;
    double D = 0.08783463138207234 * z1 - 0.1694162967925622 * z2 + 0.08588057951595272 * z3 - 0.00429891410546283 * z4;
    double E = -0.042416883008123074 * z1 + 0.1115693827987602 * z2 - 0.09764676325265872 * z3 + 0.028494263462021576 * z4;
    ca = relFrames * A; cb = relFrames * B; cc = relFrames * C; cd = relFrames * D; ce = relFrames * E; delay = (int)(0.006 * SR);
  }
  inline void proc(float& L, float& R) {
    if (div == 0) {
      float desired = det; if (!(desired == desired)) desired = 1; scaledDesired = (float)(std::asin(std::min(1.f, desired)) / 1.5707963267949); bool releasing = scaledDesired > cg; double diff;
      if (scaledDesired == 0) diff = releasing ? -1 : 1; else diff = l2d(cg / scaledDesired);
      if (releasing) { maxAtt = -1; double x = clampd(diff, -12, 0); x = 0.25 * (x + 12); double x2 = x * x, x3 = x2 * x, x4 = x2 * x2; double calc = ca + cb * x + cc * x2 + cd * x3 + ce * x4; envRate = (float)d2l(5.0 / calc); }
      else { if (maxAtt == -1 || maxAtt < diff) maxAtt = (float)diff; double eff = std::max(0.5, (double)maxAtt); double x = 0.25 / eff; envRate = (float)(1 - std::pow(x, 1.0 / attFrames)); }
    }
    div = (div + 1) & 31;
    float ax = std::max(std::fabs(L), std::fabs(R)); dl[0][dp] = L; dl[1][dp] = R;
    float att = 1; if (ax > 0.0001f && ax >= lin_thr) att = (float)(sat(ax) / ax);
    if (att > det) { double dbAtt = std::max(2.0, -l2d(att)); double sr = d2l(dbAtt / satRelFrames) - 1; det += (float)((att - det) * sr); } else det += (att - det);
    det = std::min(1.f, det);
    if (envRate < 1) cg += (scaledDesired - cg) * envRate; else { cg *= envRate; cg = std::min(1.f, cg); }
    float g = (float)(makeup * std::sin(1.5707963267949 * cg)); int rd = (dp - delay + 1024) & 1023; dp = (dp + 1) & 1023; L = dl[0][rd] * g; R = dl[1][rd] * g;
  }
};
static Comp comp;

/* ---------------- globals, commands ---------------- */
struct Cmd { int kind; int id; float a, b, c; };
static std::vector<Cmd> cmdq; static std::mutex mtx;
static double volMaster = 0.8, volSfx = 0.8, volMusic = 0.55; static float gM = -1, gS = -1, gMu = -1; static bool suspended = false, built = false;
static double lastPlay[NSFX]; static std::unordered_map<std::string, int> sfxIndex;
static float sfxL[BLK], sfxR[BLK], musL[BLK], musR[BLK], rvL[BLK], rvR[BLK], rvOL[BLK], rvOR[BLK], ambL[BLK], ambR[BLK];
static float outBufL[BLK], outBufR[BLK]; static int outPos = BLK; static bool bypassComp = false;  // bypassComp: QA only

inline void build() {
  if (built) return; built = true; rs_ ^= (u64)(uintptr_t)&built * 0x9E3779B97F4A7C15ull + 12345; voices.reserve(MAXV);
  noiseBuf.resize(SR * 2); for (auto& v : noiseBuf) v = (float)(rnd01() * 2 - 1);
  int len = (int)std::floor(SR * 2.2); std::vector<float> ir[2]; double pw = 0;
  for (int c = 0; c < 2; c++) { ir[c].resize(len); double lp = 0; for (int i = 0; i < len; i++) { double x = (double)i / len, k = 0.25 + 0.7 * x; lp += (1 - k) * ((rnd01() * 2 - 1) - lp); ir[c][i] = (float)(lp * std::pow(1 - x, 3.2) * (i < SR * 0.012 ? i / (SR * 0.012) : 1)); pw += (double)ir[c][i] * ir[c][i]; } }
  pw = std::sqrt(pw / (2.0 * len)); if (!(pw >= 0.000125)) pw = 0.000125; double scale = (1.0 / pw) * 0.00125 * (44100.0 / SR); for (int c = 0; c < 2; c++) for (auto& v : ir[c]) v = (float)(v * scale);
  conv.init(ir[0], ir[1]); comp.init(); comp.initDyn();
  for (int i = 0; i < NSFX; i++) { sfxIndex[SFX_NAMES[i]] = i; lastPlay[i] = -1e9; }
}
inline int sfxId(const char* n) { auto it = sfxIndex.find(n); return it == sfxIndex.end() ? -1 : it->second; }
inline void playId(int id, float vol = 1, float pitch = 1, float pan = 0) { if (id < 0) return; std::lock_guard<std::mutex> l(mtx); if (cmdq.size() < 96) cmdq.push_back({0, id, vol, pitch, pan}); }
inline void play(const char* name, float vol = 1, float pitch = 1, float pan = 0) { playId(sfxId(name), vol, pitch, pan); }
inline void music(int mode) { std::lock_guard<std::mutex> l(mtx); cmdq.push_back({1, mode, 0, 0, 0}); }
inline void setMood(int m) { music(m == 2 ? MU_BATTLE : m == 1 ? MU_NIGHT : MU_DAY); }   // 0 day, 1 night, 2 battle (legacy)
inline void ambient(float rain, float wind, float night, float water) { std::lock_guard<std::mutex> l(mtx); cmdq.push_back({2, 0, rain, wind, night}); cmdq.push_back({3, 0, water, 0, 0}); }
inline void setVolumes(float m, float s) { std::lock_guard<std::mutex> l(mtx); cmdq.push_back({4, 0, m, s, 0}); }
inline void suspend(bool on) { std::lock_guard<std::mutex> l(mtx); cmdq.push_back({5, on, 0, 0, 0}); }

static void doPlay(int id, double vol, double pitch, double pan) {
  double t = now(); if (t - lastPlay[id] < 0.04 && t >= lastPlay[id]) return; if (sfxVoices >= MAX_SFX) return; lastPlay[id] = t; vol = clampd(vol, 0, 1); if (vol <= 0.001) return;
  double p = clampd(pitch, 0.1, 8) * (1 + (rnd01() - 0.5) * 0.08); int b = newBus(-1, 0, true, (float)clampd(pan, -1, 1), SFX_SEND[id], vol, t + 600, true); if (b < 0) return; sfxVoices++;
  double dur = sfxDef(id, b, t + 0.005, p); buses[b].killS = (u64)((t + dur + 0.6) * SR);
}
static void applyCmds() {
  std::vector<Cmd> q; { std::lock_guard<std::mutex> l(mtx); q.swap(cmdq); }
  for (auto& c : q) switch (c.kind) {
    case 0: doPlay(c.id, c.a, c.b, c.c); break;
    case 1: setMusicMode(c.id); break;
    case 2: ambTargetV[0] = clampf(c.a, 0, 1); ambTargetV[1] = clampf(c.b, 0, 1); ambTargetV[2] = clampf(c.c, 0, 1); break;
    case 3: ambTargetV[3] = clampf(c.a, 0, 1); break;
    case 4: volMusic = clampf(c.a, 0, 1); volSfx = clampf(c.b, 0, 1); break;
    case 5: suspended = c.id != 0; break;
  }
  for (int k = 0; k < 4; k++) { Bed& b = beds[k]; float v = ambTargetV[k]; if (std::fabs(v - b.last) < 0.01f && (v > 0 || b.last == 0)) continue; b.last = v; if (!b.built) { if (v < 0.005f) continue; b.build(k); } b.target = (float)(v * AMB_MAX[k]); }
}

static inline float oscSample(int w, double ph, double dt) {
  switch (w) {
    case W_SINE: return sinp(ph);
    case W_TRI: { double x = ph - std::floor(ph); return (float)(x < 0.25 ? 4 * x : x < 0.75 ? 2 - 4 * x : 4 * x - 4); }
    case W_SAW: { double x = ph + 0.5; x -= std::floor(x); double y = 2 * x - 1; if (x < dt) { double u = x / dt; y -= 2 * u - u * u - 1; } else if (x > 1 - dt) { double u = (x - 1) / dt; y -= u * u + 2 * u + 1; } return (float)y; }
    default: { double x = ph - std::floor(ph); double y = x < 0.5 ? 1 : -1; double u = x; if (u < dt) { double r = u / dt; y += 2 * r - r * r - 1; } else if (u > 1 - dt) { double r = (u - 1) / dt; y += r * r + 2 * r + 1; } double x2 = x + 0.5; x2 -= std::floor(x2); if (x2 < dt) { double r = x2 / dt; y -= 2 * r - r * r - 1; } else if (x2 > 1 - dt) { double r = (x2 - 1) / dt; y -= r * r + 2 * r + 1; } return (float)y; }
  }
}
// render one voice into out[0..BLK) for the block starting at sample bs. returns false if finished
static bool renderVoice(Voice& v, float* out, u64 bs) {
  u64 be = bs + BLK; if (v.s1 <= bs) return false; if (v.s0 >= be) return true; u64 vs = std::max(v.s0, bs), ve = std::min(v.s1, be);
  for (u64 s = vs; s < ve;) {
    int n = (int)std::min<u64>(CTL - (s % CTL), ve - s); double t = (double)s / SR, t2 = (double)(s + n) / SR;
    float g0 = (float)(v.gain.at(t) * v.gscale), g1 = (float)(v.gain.at(t2) * v.gscale), gs = (n > 0) ? (g1 - g0) / n : 0; float g = g0;
    if (v.ft != F_NONE && v.ffVar) v.bq.set(v.ft, v.ff.at(t), v.fq);
    double f = 0, dt = 0; if (v.wave < W_NOISE) {
      f = v.hasFreq ? v.freq.at(t) : v.f0; f *= v.detMul; if (v.vibRate > 0) { double depth = v.vibRamp ? v.vibP.at(t) : v.vibDepth; f += depth * std::sin(6.283185307179586 * v.vibRate * (t - v.tStart)); } dt = std::fabs(f) / SR;
    }
    float* o = out + (s - bs);
    for (int i = 0; i < n; i++, g += gs) {
      float x;
      if (v.wave < W_NOISE) { x = oscSample(v.wave, v.ph, dt); if (v.wave >= W_SAW) x *= 0.918f; /* Chrome normalises band-limited saw/square tables (Gibbs overshoot) */ v.ph += dt; if (v.ph >= 1) v.ph -= std::floor(v.ph); }
      else if (v.wave == W_NOISE) { x = noiseBuf[(int)v.pos]; v.pos += 1; if (v.pos >= noiseBuf.size()) v.pos = 0; }
      else { int ip = (int)v.pos; if (ip + 1 >= v.len) { return false; } float fr = (float)(v.pos - ip); x = ((float)v.dI[ip] + ((float)v.dI[ip + 1] - (float)v.dI[ip]) * fr) * (1.f / 32768.f); v.pos += v.rate; }
      if (v.ft != F_NONE) x = v.bq.proc(x);
      o[i] += x * g;
    }
    s += n;
  }
  return v.s1 > be;
}
static void renderBlock() {
  u64 bs = sampleNow; double tb = now();
  applyCmds();
  musicTick();
  for (int b : busOrder) memset(buses[b].buf, 0, sizeof(float) * BLK);
  memset(sfxL, 0, sizeof sfxL); memset(sfxR, 0, sizeof sfxR); memset(musL, 0, sizeof musL); memset(musR, 0, sizeof musR); memset(rvL, 0, sizeof rvL); memset(rvR, 0, sizeof rvR); memset(ambL, 0, sizeof ambL); memset(ambR, 0, sizeof ambR);
  for (size_t i = 0; i < voices.size();) {
    Voice& v = voices[i]; Bus& B = buses[v.bus]; bool alive = B.on && B.gen == v.gen && renderVoice(v, B.buf, bs);
    if (!alive) { v = voices.back(); voices.pop_back(); } else i++;
  }
  // buses: children were created after parents -> process in reverse creation order
  static std::vector<int> order; order = busOrder;
  for (int oi = (int)order.size() - 1; oi >= 0; oi--) {
    int bi = order[oi]; Bus& B = buses[bi]; if (!B.on) continue; float* x = B.buf;
    if (B.ft != F_NONE) { if (B.ff.n > 1) { for (int c = 0; c < BLK; c += CTL) { B.bq.set(B.ft, B.ff.at(tb + (double)c / SR), B.fq); for (int j = c; j < c + CTL; j++) x[j] = B.bq.proc(x[j]); } } else for (int j = 0; j < BLK; j++) x[j] = B.bq.proc(x[j]); }
    if (B.gain.n > 0) { for (int c = 0; c < BLK; c += CTL) { float g0 = (float)B.gain.at(tb + (double)c / SR), g1 = (float)B.gain.at(tb + (double)(c + CTL) / SR), gs = (g1 - g0) / CTL, g = g0; for (int j = c; j < c + CTL; j++, g += gs) x[j] *= g; } } else if (B.gain.init != 1) for (int j = 0; j < BLK; j++) x[j] *= (float)B.gain.init;
    if (B.parent >= 0) { Bus& P = buses[B.parent]; if (P.on) for (int j = 0; j < BLK; j++) P.buf[j] += x[j]; }
    else {
      float gl = 1, gr = 1; if (B.hasPan) { double xx = (clampd(B.pan, -1, 1) + 1) * 0.5; gl = (float)std::cos(xx * 1.5707963267949); gr = (float)std::sin(xx * 1.5707963267949); }
      float* dl = B.dest ? musL : sfxL; float* dr = B.dest ? musR : sfxR; double vs = B.dest ? volMusic : volSfx; float sk = vs > 0.001 ? B.send : 0;
      for (int j = 0; j < BLK; j++) { float l = x[j] * gl, r = x[j] * gr; dl[j] += l; dr[j] += r; rvL[j] += l * sk; rvR[j] += r * sk; }
    }
  }
  for (int b : order) { Bus& B = buses[b]; if (B.on && B.killS <= bs + BLK) freeBus(b); }
  for (int k = 0; k < 4; k++) if (beds[k].built) beds[k].render(k, ambL, ambR, BLK, (float)(1 - std::exp(-1.0 / (0.6 * SR))));
  conv.process(rvL, rvR, rvOL, rvOR);
  float tM = (float)(volMaster * 0.9), tS = (float)(volSfx * 0.9), tMu = (float)(volMusic * 0.9);
  if (gM < 0) { gM = tM; gS = tS; gMu = tMu; } float sm = (float)(1 - std::exp(-1.0 / (0.08 * SR)));
  for (int i = 0; i < BLK; i++) {
    gM += (tM - gM) * sm; gS += (tS - gS) * sm; gMu += (tMu - gMu) * sm;
    float L = ((sfxL[i] + ambL[i]) * gS + musL[i] * gMu + rvOL[i] * 0.32f) * gM, R = ((sfxR[i] + ambR[i]) * gS + musR[i] * gMu + rvOR[i] * 0.32f) * gM;
    if (!bypassComp) comp.proc(L, R); outBufL[i] = clampf(L, -1, 1); outBufR[i] = clampf(R, -1, 1);
  }
  sampleNow += BLK;
}
inline void mix(int16_t* out, int frames) {
  for (int i = 0; i < frames; i++) {
    if (outPos >= BLK) { if (suspended) { memset(outBufL, 0, sizeof outBufL); memset(outBufR, 0, sizeof outBufR); applyCmds(); } else renderBlock(); outPos = 0; }
    out[i * 2] = (int16_t)(outBufL[outPos] * 32767.f); out[i * 2 + 1] = (int16_t)(outBufR[outPos] * 32767.f); outPos++;
  }
}
// QA: reset engine state (offline rendering / tests)
inline void reset() { voices.clear(); busOrder.clear(); for (auto& b : buses) { b.on = false; b.gen++; } players.clear(); curMode = MU_OFF; sampleNow = 0; sfxVoices = 0; for (auto& l : lastPlay) l = -1e9; outPos = BLK; gM = -1; for (auto& b : beds) b = Bed(); for (int k = 0; k < 4; k++) ambTargetV[k] = 0; suspended = false; std::lock_guard<std::mutex> l(mtx); cmdq.clear(); }
}
