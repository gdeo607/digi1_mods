/* DH-101: the SH-101-style oscillator section (sh101.h).
 *
 * Numbers: a phase is a 32-bit fraction of a cycle; levels and outputs are Q15. The saw, pulse and sub are
 * band-limited with polyBLEP using the "ramp" method of Digi Mono (gdeo607/digi1_mods, mono.c, MIT):
 * a chunk's output is one straight ramp (the sum of every oscillator's slope) plus steps where an
 * oscillator jumps, each step smoothed over the sample either side. Finding the jumps costs a division a
 * wrap, not a test a sample, so the whole mix costs little more than one oscillator.
 *
 * As on the 101, saw, pulse and sub come from one VCO (one phase), so they are always in step; the sub
 * divides the VCO by counting its wraps.
 */
#include "sh101.h"
#include "sh101_tables.h"

#define CHUNK 32

/* The EMAC, fractional, for the VCF: a x / 2^31 with 32-bit operands. Saved and put back around its
 * use, as Digi Mono's CHORDS and Digi Macro do (the firmware's own audio code uses the unit). A PC build
 * computes the same in 64-bit C. (Digi Mono mono.c, MIT.) */
#if defined(__mcoldfire__)
static inline int32_t fm(int32_t a, int32_t x)
{
    int32_t r;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(x));
    return r;
}
struct emac_save { int32_t macsr, acc0, ext01; };
static inline void emac_enter(struct emac_save *e)
{
    int32_t z = 0;
    __asm__ volatile ("move.l %%macsr,%0\n\tmove.l %%acc0,%1\n\tmove.l %%accext01,%2\n\t"
                      "move.l #0x20,%%macsr\n\tmove.l %3,%%acc0\n\tmove.l %3,%%accext01"
                      : "=&d"(e->macsr), "=&d"(e->acc0), "=&d"(e->ext01) : "d"(z));
}
static inline void emac_leave(const struct emac_save *e)
{
    __asm__ volatile ("move.l %0,%%acc0\n\tmove.l %1,%%accext01\n\tmove.l %2,%%macsr"
                      : : "d"(e->acc0), "d"(e->ext01), "d"(e->macsr));
}
#else
static inline int32_t fm(int32_t a, int32_t x)
{
    return (int32_t)(((int64_t)a * x) >> 31);
}
struct emac_save { int32_t unused; };
static inline void emac_enter(struct emac_save *e) { (void)e; }
static inline void emac_leave(const struct emac_save *e) { (void)e; }
#endif

uint8_t sh101_eco;

/* ---- helpers ---------------------------------------------------------------------------------------- */

static inline int16_t sat16(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t)x;
}

static uint32_t rnd(struct sh101_voice *v)
{
    uint32_t r = v->rng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    v->rng = r;
    return r;
}

/* The polyBLEP residual of a falling jump of 2 at t = 0, over a phase t and step dt in 1/65536 cycle. */
static inline int32_t blep(uint32_t t, uint32_t dt)
{
    int32_t x;
    if (t < dt) {
        x = (int32_t)((t << 15) / dt);
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    if (t > 65535 - dt) {
        x = (int32_t)(((65536 - t) << 15) / dt);
        return ((x * x) >> 15) - 2 * x + 32768;
    }
    return 0;
}

/* ---- the ramp: one chunk's sum of oscillators ------------------------------------------------------- */

struct ramp {
    int32_t q, s;                   /* the level at the chunk's first sample (Q30) and its step a sample */
    int32_t h[CHUNK + 2];           /* changes, from the sample before, at each sample                   */
};

/* g * (x / 65536) for |g| < 2^15, x < 2^32: a phase or a step as a level, Q30 */
static inline int32_t gmul(int32_t g, uint32_t x)
{
    return g * (int32_t)(x >> 16) + ((g * (int32_t)(x & 0xffff)) >> 16);
}

/* A jump of j at each wrap of phase q (stepping inc < 2^31 over n samples), a blep residual of a times the
 * polyBLEP on the samples either side. A wrap just before the chunk has its jump in the level already.
 * Returns the number of wraps. */
static int steps(int32_t *h, uint32_t q, uint32_t inc, int32_t j, int32_t a, int n)
{
    uint32_t dt = inc >> 16;
    int32_t e, w = 0;
    int i = 0;
    if (!inc)
        return 0;
    if (q < inc && (e = a * blep(q >> 16, dt)) != 0) {
        h[0] += e;
        h[1] -= e;
    }
    for (;;) {
        uint32_t k, m = (uint32_t)(n - i);
        int32_t b, f = 0;
        if (inc < (1u << 26) && m * inc <= ~q)  /* no wrap left in the chunk */
            break;
        k = ~q / inc;                           /* samples until the last one before the next wrap */
        if (k >= m)
            break;
        i += (int)k;
        q += k * inc;
        b = a * blep(q >> 16, dt);              /* the sample before the wrap */
        q += inc;
        w++;
        i++;                                    /* the sample after it */
        if (i < n)
            f = a * blep(q >> 16, dt);
        h[i - 1] += b;
        h[i] += j + f - b;
        h[i + 1] -= f;
        if (i >= n)
            break;
    }
    return w;
}

/* a saw of level g (Q15, either sign) at phase q, stepping inc: rises from -g to +g, then falls */
static int ramp_saw(struct ramp *r, uint32_t q, uint32_t inc, int32_t g, int n)
{
    r->q += gmul(g, q) - g * 32768;
    r->s += gmul(g, inc);
    return steps(r->h, q, inc, -g * 65536, -g, n);
}

/* a pulse of level g (+-g), high for the duty o / 65536 of a cycle: the saw at q less the saw at q - o.
 * Its mean is zero whatever the duty, as an AC-coupled pulse's is. */
static void ramp_pulse(struct ramp *r, uint32_t q, uint32_t inc, uint32_t o, int32_t g, int n)
{
    ramp_saw(r, q, inc, g, n);
    ramp_saw(r, q - (o << 16), inc, -g, n);
}

/* a square of level g, +g for the first half cycle */
static void ramp_square(struct ramp *r, uint32_t q, uint32_t inc, int32_t g, int n)
{
    r->q += g * (q < 0x80000000u ? 32767 : -32768);
    steps(r->h, q, inc, g * 65535, g, n);
    steps(r->h, q + 0x80000000u, inc, -g * 65535, -g, n);
}

static void ramp_start(struct ramp *r, int n)
{
    int i;
    r->q = 8192;                                /* rounds the sum (Q29: a bit of headroom) */
    r->s = 0;
    for (i = 0; i < n + 2; i++)
        r->h[i] = 0;
}

/* The output stage, a sample at a time: the oscillators' ramp (x), a gentle treble lift that restores what
 * the two-sample polyBLEP takes off near the top (a 3-tap FIR, y = x1 + 3/16 (2 x1 - x - x2): +0.6 dB at
 * 5 kHz, +2.8 dB at 12 kHz; the reference 101 recordings are that bright), the noise, then the mixer's AC
 * coupling: a one-pole high-pass at 6.5 Hz (y += dx - y (2^-10 - 2^-13)), which gives a 101's low notes
 * their drooping square tops and bowed saw. Shifts and adds only: no multiply a sample but the noise's.
 * The lift delays the oscillators by one sample; under load (eco, latched at a note's start so the change
 * makes no click) it is left out, the delay kept. */
static inline __attribute__((always_inline)) void out_loop(struct sh101_voice *v, const struct ramp *r,
                                                           int32_t gn, int16_t *out, int n, int noise, int lift)
{
    int32_t q = r->q - r->s, s = r->s;
    int32_t x1 = v->x1, x2 = v->x2, hx = v->hx, hy = v->hy;
    const int32_t *h = r->h;
    while (n--) {
        int32_t x, y;
        q += s + *h++;
        x = q >> 14;
        if (lift) {
            int32_t d = (x1 << 1) - x - x2;
            y = x1 + ((d + (d << 1)) >> 4);
        } else
            y = x1;
        x2 = x1;
        x1 = x;
        if (noise)
            y += (((int32_t)rnd(v) >> 16) * gn) >> 15;      /* white, +-32768 */
        hy += (y - hx) << 8;
        hy -= (hy >> 10) - (hy >> 13);
        hx = y;
        *out++ = sat16(hy >> 8);
    }
    v->x1 = (int16_t)x1;
    v->x2 = (int16_t)x2;
    v->hx = hx;
    v->hy = hy;
}

static void ramp_out(struct sh101_voice *v, const struct ramp *r, int32_t gn, int16_t *out, int n)
{
    if (v->eco)
        out_loop(v, r, gn, out, n, gn != 0, 0);
    else if (gn)
        out_loop(v, r, gn, out, n, 1, 1);
    else
        out_loop(v, r, 0, out, n, 0, 1);
}

/* ---- portamento ------------------------------------------------------------------------------------ *
 * As the 101's: an RC glide of the pitch CV, so an exponential approach in octaves (up and down alike).
 * A new note starts at the last block's pitch, as an offset in octaves from its own; the offset decays a
 * block at a time. Only note changes glide: LFO and p-locked tuning reach the pitch as before. */

/* log2(x), Q16 octaves, x > 0: the top bit's place plus log2(1 + f) from the table, interpolated */
static int32_t log2q(uint32_t x)
{
    int32_t e = 31, i;
    uint32_t r;
    if (!(x & 0xffff0000u)) { x <<= 16; e -= 16; }
    if (!(x & 0xff000000u)) { x <<= 8; e -= 8; }
    if (!(x & 0xf0000000u)) { x <<= 4; e -= 4; }
    if (!(x & 0xc0000000u)) { x <<= 2; e -= 2; }
    if (!(x & 0x80000000u)) { x <<= 1; e -= 1; }
    i = (int32_t)((x >> 26) & 31);
    r = (x >> 10) & 0xffff;
    return (e << 16) + (int32_t)SH_LOG2[i] + (int32_t)(((SH_LOG2[i + 1] - SH_LOG2[i]) * r) >> 16);
}

/* inc * 2^(o / 65536), clamped to SH_INC_MAX */
static uint32_t mul_exp2(uint32_t inc, int32_t o)
{
    int32_t oi = o >> 16, f = o & 0xffff, i = f >> 11;
    uint32_t m = SH_EXP2[i] + (((SH_EXP2[i + 1] - SH_EXP2[i]) * (uint32_t)(f & 0x7ff)) >> 11);
    uint32_t r = (inc >> 16) * m + (((inc & 0xffff) * (m >> 1)) >> 15);
    if (oi > 0)
        return oi > 30 || r > (SH_INC_MAX >> oi) ? SH_INC_MAX : r << oi;
    if (oi < 0)
        return oi < -31 ? 0 : r >> -oi;
    return r > SH_INC_MAX ? SH_INC_MAX : r;
}

/* When to glide. A note's trig and its pitch need not reach the engine in the same block, and on the unit
 * the order is not what one would guess (the recordings of v0.6: no glide at all, so the pitch came first).
 * So both orders are caught, each within GLIDE_WIN blocks (~11 ms):
 *   - the pitch first: a jump of a quarter semitone or more (the note changing, not an LFO's wobble) is
 *     remembered, with the pitch the voice was playing before it; a trig soon after glides from there
 *     (the few blocks between played the new pitch already: under 11 ms, and in practice one block);
 *   - the trig first: it opens a window, and a jump within it glides from the pitch before the jump.
 * Outside those, pitch changes (LFO, p-locked tuning without a trig) are followed as they come.
 * (On the unit the pitch seems to come first: v0.6, which only waited after the trig, never glided.)
 * The offset is kept in Q24 octaves, so that even a slow glide's last steps (k small) still move it: a
 * Q16 offset stopped a few cents short, its decrement rounding to nothing. */
#define GLIDE_WIN 16
#define GLIDE_MIN 1365                          /* a quarter semitone, Q16 octaves: a note change */

static void glide_from(struct sh101_voice *v, uint32_t from, uint32_t inc)
{
    int32_t o = log2q(from) - log2q(inc);
    o = o > (8 << 16) ? (8 << 16) : o < -(8 << 16) ? -(8 << 16) : o;
    v->gl = o << 8;
}

static uint32_t glide(struct sh101_voice *v, uint32_t inc)
{
    uint32_t k = SH_PORT_K[v->port & 127];
    int jump = 0;
    if (inc != v->tgt && v->tgt && inc) {
        int32_t step = log2q(v->tgt) - log2q(inc);
        jump = step >= GLIDE_MIN || step <= -GLIDE_MIN;
    }
    if (v->retrig) {
        v->retrig = 0;
        v->win = 0;
        if (v->since < GLIDE_WIN && !jump) {    /* the pitch came first, `since` blocks ago */
            if (k && v->from)
                glide_from(v, v->from, inc);
            v->since = 255;
        } else
            v->win = GLIDE_WIN + 1;
    }
    if (jump) {
        if (v->win) {                           /* the trig came first */
            if (k && v->last)
                glide_from(v, v->last, inc);
            v->win = 0;
            v->since = 255;
        } else {                                /* remember it: a trig may follow */
            v->from = v->last;
            v->since = 0;
        }
    } else if (v->since < 255)
        v->since++;
    if (v->win)
        v->win--;
    v->tgt = inc;
    if (!k)
        v->gl = 0;
    if (v->gl) {
        uint32_t g = v->gl < 0 ? (uint32_t)-v->gl : (uint32_t)v->gl, d;
        inc = mul_exp2(inc, v->gl >> 8);
        d = (g >> 16) * k + (((g & 0xffff) * k) >> 16);     /* g k / 65536, no overflow */
        g = d < g ? g - d : 0;
        if (g < 1024)                                       /* 0.07 cent: arrived */
            g = 0;
        v->gl = v->gl < 0 ? -(int32_t)g : (int32_t)g;
    }
    v->last = inc;
    return inc;
}

/* ---- the 101 LFO and the VCF (page 2) -------------------------------------------------------------- *
 * The VCF is a four-pole ladder solved with zero-delay feedback (the trapezoidal "TPT" one-pole of
 * Zavalishin's "The Art of VA Filter Design" and Simper's Cytomic technical papers, both public): each
 * stage v = G (in - s), y = v + s, s = y + v, G = g / (1 + g), g = tan(pi fc / fs); the four stages and the
 * feedback u = x - k y4 are solved for y4 exactly each sample (y4 = (G^4 x + S) / (1 + k G^4), S the
 * stages' carried state), so the cutoff stays in tune to 20 kHz without oversampling. Its scale, the
 * resonance k and the bass it loses were fitted to TAL-BassLine-101 recordings (tools/gen_digi101_tables.py):
 * output gain 1 + 0.42 k keeps about as much bass as TAL. The feedback k y4 is soft-limited (from 0.625 of
 * full scale), as the 101's resonance saturates inside its loop: self-oscillation (RES above 120) settles
 * at about -6 dBFS, clean, and loud inputs tame the resonance as on the hardware. Signals inside are Q26. */
#define VCF_Q       1365                        /* 512 2^28 / 4 VCF_T: the bend's e^2 term (vcf.s too) */
#define VCF_X_SHIFT 11                          /* Q15 in, Q26 inside (finer steps at low cutoffs) */
#define VCF_T       (3 << 23)                   /* the feedback's soft limit: from 0.375 full scale, to 0.75 */
#define VCF_PMAX    (11 * 65536 + 16384)        /* 11.25 octaves above 8.18 Hz: 20 kHz */
#define LOG2_INC_C3 1538825                     /* log2q() of note 48's phase step (the KYB pivot) */

static inline int32_t tri16(uint32_t ph)        /* a triangle, -32768..32767, 0 at phase 0 rising */
{
    uint32_t u = (ph + 0x40000000u) >> 16;
    return u < 32768 ? (int32_t)u * 2 - 32768 : (int32_t)(65535 - u) * 2 - 32768;
}

static int32_t vcf_limit(int32_t u)
{
    /* past the knee T a quadratic bend, slope 1 at T and 0 at 3 T, where it holds 2 T: T + e - e^2 / 4T
     * (e = |u| - T; e^2 / 4T as VCF_Q (q^2 >> 9), q = e >> 14: 16-bit multiplies). No division: vcf.s does the same in a few cycles. */
    int32_t a = u < 0 ? -u : u, e, q;
    if (a <= VCF_T || a < 0)
        return a < 0 ? -2 * VCF_T : u;
    e = a - VCF_T;
    if (e >= 2 * VCF_T)
        a = 2 * VCF_T;
    else {
        q = e >> 14;
        a = VCF_T + e - (((q * q) >> 9) * VCF_Q);
    }
    return u < 0 ? -a : a;
}

#if !defined(__mcoldfire__)
int32_t sh101_k_override;                       /* desktop calibration only: k (Q28) for every RES */
#define VCF_KQ(v) (sh101_k_override ? sh101_k_override : (int32_t)SH_VCF_K[(v)->f_res & 127])
#else
#define VCF_KQ(v) ((int32_t)SH_VCF_K[(v)->f_res & 127])
#endif

/* the cutoff's pitch for this block, Q16 octaves above 8.18 Hz: CUT, the resonance's pull (TAL's peak
 * rises 0.2 octave from no resonance to full: 0.0516 octave a unit of k), KYB, ENV, MOD */
static int32_t vcf_pitch(const struct sh101_voice *v, uint32_t note_inc, int32_t lfo)
{
    int32_t p = (int32_t)SH_VCF_CUT[v->f_cut & 127];
    p += ((VCF_KQ(v) >> 14) * 3382) >> 14;
    if (v->f_kyb && note_inc)                   /* KYB 127 = 1 octave an octave, about C3 */
        p += (((log2q(note_inc) - LOG2_INC_C3) >> 7) * v->f_kyb) / 127 << 7;
    if (v->f_env)
        p += ((v->f_env * (7 * 65536 / 127)) >> 3) * (int32_t)v->envq >> 12;
    if (v->f_mod) {
        int32_t d = (v->f_mod * v->f_mod * 16) >> 0;        /* (mod / 127)^2 * 4 octaves, Q16: 4 * 65536 / 127^2 = 16.25 */
        p += ((d >> 4) * lfo) >> 11;
    }
    return p < 0 ? 0 : p > VCF_PMAX ? VCF_PMAX : p;
}

/* The per-sample loop. c = {e0..e4, G, comp}: the feedback k y4 = sum(e_i z_i) (z = x, s1..s4; e Q28: the
 * whole solve folded into one accumulator chain), the stages' G (Q31) and the output gain (Q28). On the
 * unit it is vcf.s (the same arithmetic, every coefficient and state in a register); this C is the
 * reference the desktop builds run and tests/check_vcf.py holds vcf.s to. */
#if defined(__mcoldfire__)
void sh101_vcf_loop(int16_t *io, int n, const int32_t *c, int32_t *s);
#else
static inline int32_t vcf_chain(const int32_t *c, int32_t x, const int32_t *s)
{
    int64_t a = (int64_t)c[0] * x + (int64_t)c[1] * s[0] + (int64_t)c[2] * s[1] + (int64_t)c[3] * s[2] +
                (int64_t)c[4] * s[3];
    return (int32_t)(a >> 31);
}
void sh101_vcf_loop(int16_t *io, int n, const int32_t *c, int32_t *s);
void sh101_vcf_loop(int16_t *io, int n, const int32_t *c, int32_t *s)
{
    int32_t G = c[5], comp = c[6];
    while (n-- > 0) {
        int32_t x = (int32_t)*io << VCF_X_SHIFT, F, u, w, y;
        int i;
        F = vcf_limit(vcf_chain(c, x, s) << 3);
        u = x - F;
        y = u;
        for (i = 0; i < 4; i++) {
            w = fm(G, y - s[i]);
            y = w + s[i];
            s[i] = y + w;
        }
        y = fm(comp, y) >> (VCF_X_SHIFT - 3);
        *io++ = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
    }
}
#endif

static void vcf_block(struct sh101_voice *v, int16_t *io, int n, int32_t pitch)
{
    struct emac_save es;
    uint32_t q = (uint32_t)pitch * 48u, idx = q >> 16, fr = q & 0xffff;
    int32_t G, d, G2, G3, G4, om, k, kinv, inv, c[7];
    uint32_t den;
    if (idx >= 544) {
        idx = 543;
        fr = 0xffff;
    }
    d = (int32_t)(SH_VCF_G[idx + 1] - SH_VCF_G[idx]);
    G = (int32_t)SH_VCF_G[idx] + (d >> 16) * (int32_t)fr + (((d & 0xffff) * (int32_t)fr) >> 16);
    k = VCF_KQ(v);                              /* Q28: k / 8 as a Q31 fraction */
    if (v->f_res > 120) {                       /* a whisper of circuit noise: self-oscillation starts */
        int i;                                  /* from silence, as on the hardware (about -60 dB) */
        for (i = 0; i < n; i++) {
            int32_t y = io[i] + ((int32_t)rnd(v) >> 26);
            io[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
    }
    emac_enter(&es);
    G2 = fm(G, G);
    G3 = fm(G2, G);
    G4 = fm(G3, G);
    om = 0x7fffffff - G;
    den = (uint32_t)((1 << 28) + fm(k, G4)) >> 13;      /* 1 + k G^4, Q15 */
    inv = (int32_t)((0xffffffffu / den) << 14);         /* 1 / (1 + k G^4), Q31 (just under 1 at k = 0) */
    kinv = fm(k, inv);                                  /* k / (1 + k G^4), Q28 */
    c[0] = fm(kinv, G4);                                /* y4 = (G^4 x + G^3 om s1 + G^2 om s2 + G om s3 + om s4) */
    c[1] = fm(kinv, fm(G3, om));                        /*      / (1 + k G^4), each term times k here */
    c[2] = fm(kinv, fm(G2, om));
    c[3] = fm(kinv, fm(G, om));
    c[4] = fm(kinv, om);
    c[5] = G;
    c[6] = (1 << 28) + fm(901943132, k);                /* 1 + 0.42 k, Q28 (0.42 in Q31) */
    sh101_vcf_loop(io, n, c, v->s);
    emac_leave(&es);
}

/* ---- the knobs ------------------------------------------------------------------------------------- */

int sh101_range(int v)      { return v >> 5; }
uint32_t sh101_lfo_rate(int v) { return SH_LFO_RATE[v & 127]; }
int sh101_pwm_mode(int v)   { return v < 10 ? 0 : v < 69 ? 1 : 2; }
int sh101_pwm_amount(int v) { return v < 10 ? 0 : v < 69 ? v - 9 : v - 68; }
int sh101_sub_mode(int v)   { return v < 43 ? 0 : v < 86 ? 1 : 2; }

int sh101_sub_level(int v)
{
    /* each zone's first step is off, its last full: 0..42, 43..85, 86..127 -> 0..127 */
    int z = v < 43 ? v : v < 86 ? v - 43 : v - 86, top = v < 86 ? 42 : 41;
    return (z * 127 + top / 2) / top;
}

/* ---- the voice --------------------------------------------------------------------------------------- */

void sh101_init(struct sh101_voice *v)
{
    v->ph = 0x80000000u;
    v->lfo = 0;
    v->rng = 0x6d2b79f5u;
    v->env = 0;
    v->wraps = 0;
    v->eco = 0;
    v->fresh = 0;
    v->spare[0] = 0;
    v->x1 = v->x2 = 0;
    v->hx = v->hy = 0;
    v->gl = 0;
    v->last = 0;
    v->tgt = 0;
    v->from = 0;
    v->retrig = 0;
    v->win = 0;
    v->since = 255;
    v->port = 0;
    v->f_cut = 127;
    v->f_res = v->f_env = v->f_mod = v->f_kyb = v->f_vib = v->f_rate = v->f_on = 0;
    v->envq = 0;
    v->fade = 0xffff;
    v->lfo2 = 0;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
}

void sh101_trig(struct sh101_voice *v)
{
    v->ph = 0x80000000u;                        /* the saw at its zero crossing, rising */
    v->wraps = 0;
    v->env = 65535;
    v->eco = sh101_eco;                         /* the cheaper path, from a note's start */
    v->retrig = 1;                              /* the render starts the glide from the last pitch */
}

void sh101_render(struct sh101_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct ramp r;
    int32_t gs, gp, gb, gn, sum, o, depth, mode, sm, fpitch = 0;
    uint32_t sinc, note_inc;
    int rng = sh101_range(p[SH_RNGE]), filt, n0 = n;
    int16_t *out0 = out;

    inc = glide(v, inc);                        /* portamento, on the note's own pitch (8') */

    /* page 2: the 101 LFO (one triangle, read once a block) on the pitch (VIB) and, below, the VCF */
    {
        int32_t lfo = tri16(v->lfo2);
        v->lfo2 += SH_LFO_RATE[v->f_rate & 127] * (uint32_t)n;
        note_inc = inc;
        if (v->f_vib) {                         /* (vib / 127)^2 octaves */
            int32_t d = v->f_vib * v->f_vib * 4;
            inc = mul_exp2(inc, ((d >> 4) * lfo) >> 11);
        }
        filt = v->f_cut != 127 || v->f_res || v->f_env || v->f_mod || v->f_kyb;
        if (filt && !v->f_on)                   /* the filter comes in: from rest */
            v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
        v->f_on = (uint8_t)filt;
        if (filt)
            fpitch = vcf_pitch(v, note_inc, lfo);
    }

    /* RANGE: the note is at 8'; 16' an octave down, 4' and 2' one and two up */
    if (rng == 0)
        inc >>= 1;
    else if (rng > 1) {
        int sh = rng - 1;
        inc = inc > (SH_INC_MAX >> sh) ? SH_INC_MAX : inc << sh;
    }
    if (inc > SH_INC_MAX)
        inc = SH_INC_MAX;

    /* PULSE WIDTH for this block: 50 % narrowing to 5 % (0.45 of a cycle at full), set by PW alone (MAN)
     * or by PW times the LFO (a triangle, 0..1) or the envelope */
    mode = sh101_pwm_mode(p[SH_PWM]);
    depth = (p[SH_PW] * 29491) / 127;           /* 0 .. 0.45 cycle in 1/65536 */
    if (mode == 1) {
        uint32_t u = v->lfo >> 16;
        int32_t t = u < 32768 ? (int32_t)u * 2 : (int32_t)(65535 - u) * 2;   /* 0..65534 */
        depth = (depth * (t >> 1)) >> 15;
        v->lfo += SH_LFO_INC[sh101_pwm_amount(p[SH_PWM]) - 1] * (uint32_t)n;
    } else {
        v->lfo += SH_LFO_INC[29] * (uint32_t)n;  /* keeps running, as the 101's LFO does */
        if (mode == 2)
            depth = (depth * (int32_t)(v->env >> 1)) >> 15;
    }
    if (mode == 2) {
        uint32_t k = SH_ENV_K[sh101_pwm_amount(p[SH_PWM]) - 1];
        v->env -= (v->env * k) >> 16;
        if (v->env && k * v->env < 65536)       /* the last step: to zero */
            v->env = 0;
    }
    o = 32768 - depth;
    if (o < 1024)
        o = 1024;

    /* the mixer, at the levels measured on a 101 with each slider full: the saw at 0.5 (-6 dB, so that a
     * few sources together fit), the pulse 0.83 and the sub 0.54 of that (its 25 % pulse: see below). Only when
     * the peaks would sum over 0.8 is the whole mix turned down together, the balance kept (a pulse of
     * duty o peaks at 2 (1 - o) times its level, the 25 % sub 1.72 times); 0.8 leaves room for the lift's
     * and the high-pass's overshoot. The ramp works at half level (Q29), its output doubled back. */
    sm = sh101_sub_mode(p[SH_SUB]);
    gs = SH_GAIN[p[SH_SAW]] >> 1;
    gp = (SH_GAIN[p[SH_PULS]] * 13599) >> 15;
    gb = (SH_GAIN[sh101_sub_level(p[SH_SUB])] * (sm == 2 ? 11272 : 8847)) >> 15;
    gn = SH_GAIN[p[SH_NOIS]] >> 1;
    sum = gs + ((gp * ((65536 - o) >> 1)) >> 14) + (sm == 2 ? gb + ((gb * 23593) >> 15) : gb) + gn;
    if (sum > 26214) {
        int32_t k = (26214 << 15) / sum;
        gs = (gs * k) >> 15;
        gp = (gp * k) >> 15;
        gb = (gb * k) >> 15;
        gn = (gn * k) >> 15;
    }
    gs >>= 1;
    gp >>= 1;
    gb >>= 1;

    sinc = sm == 0 ? inc >> 1 : inc >> 2;

    while (n > 0) {
        int c = n < CHUNK ? n : CHUNK;
        uint32_t p0 = v->ph, w = v->wraps;
        int wr = 0;
        ramp_start(&r, c);
        if (gs)
            wr = ramp_saw(&r, p0, inc, gs, c);
        else
            wr = steps(r.h, p0, inc, 0, 0, c);      /* only counts the wraps (adds nothing) */
        if (gp)
            ramp_pulse(&r, p0, inc, (uint32_t)o, gp, c);
        if (gb) {
            /* the sub's phase from the VCO's: wraps count the cycles a divider has seen */
            if (sm == 0)
                ramp_square(&r, (w << 31) | (p0 >> 1), sinc, gb, c);
            else if (sm == 1)
                ramp_square(&r, (w << 30) | (p0 >> 2), sinc, gb, c);
            else {
                /* the 101's 25 % sub is a staircase, not a clean pulse: a 25 % pulse and a square of the
                 * same period at 0.22 of its level, high over the pulse's low quarter and the quarter
                 * after (measured: three levels, -0.115 / +0.065 / +0.025, over 1/4, 1/4, 1/2) */
                uint32_t q2 = (w << 30) | (p0 >> 2);
                ramp_pulse(&r, q2, sinc, 16384, gb, c);
                ramp_square(&r, q2, sinc, (gb * 7288) >> 15, c);
            }
        }
        v->wraps = (uint8_t)((w + (uint32_t)wr) & 3);
        v->ph = p0 + (uint32_t)c * inc;
        ramp_out(v, &r, gn, out, c);
        out += c;
        n -= c;
    }
    if (filt)
        vcf_block(v, out0, n0, fpitch);
}

/* A block in which the track's AMP envelope is closed (digi101.c knows; the AMP stage would multiply
 * the block by zero): silence, at the cost of keeping time. The glide, the LFOs and the PWM envelope run
 * on as if rendered, the oscillator's phase too; the filter's states wait (a trig restarts the voice
 * anyway, and its attack starts from zero). */
void sh101_rest(struct sh101_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int i, mode = sh101_pwm_mode(p[SH_PWM]);
    inc = glide(v, inc);
    v->lfo2 += SH_LFO_RATE[v->f_rate & 127] * (uint32_t)n;
    v->lfo += (mode == 1 ? SH_LFO_INC[sh101_pwm_amount(p[SH_PWM]) - 1] : SH_LFO_INC[29]) * (uint32_t)n;
    if (mode == 2) {
        uint32_t k = SH_ENV_K[sh101_pwm_amount(p[SH_PWM]) - 1];
        v->env -= (v->env * k) >> 16;
        if (v->env && k * v->env < 65536)
            v->env = 0;
    }
    v->ph += (uint32_t)n * inc;
    for (i = 0; i < n; i++)
        out[i] = 0;
}
