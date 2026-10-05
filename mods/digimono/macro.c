/* Digi Mono's MACRO machine: engines ported from Plaits, in 32-bit integer arithmetic.
 *
 * The synthesis is Plaits' (github.com/pichenettes/eurorack, plaits/dsp), by Emilie Gillet, under the MIT
 * licence below; this file restates it in fixed point for a CPU without an FPU. Each engine says which
 * Plaits file it follows. Numbers: a phase is a 32-bit fraction of a cycle; a signal is Q15 (32768 = 1)
 * unless named Q16; a knob is 0..127 as the SRC page gives it.
 *
 * Copyright 2016 Emilie Gillet (the synthesis); the fixed-point port, the digi1_mods authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "macro.h"
#include "macro_tables.h"

/* ---- the multiply-accumulate unit ------------------------------------------------------------------- *
 * The filters multiply on the ColdFire's EMAC: signed fractional, truncating (MACSR 0x20), ACC0 only. Two
 * 32-bit products summed come out as (floor(a x / 2^23) + floor(b y / 2^23)) >> 8, which a PC build
 * computes in 64-bit C (the tests compare the two). A third product, 2^15 x 2^15, adds half the last bit:
 * rounded, not truncated (truncation's bias builds up in a resonant filter's states). macro_render() saves MACSR, ACC0 and ACCEXT01 and puts
 * them back, as Digi EQ does: the firmware's own audio code uses the unit. */
#if defined(__mcoldfire__)
static inline int32_t fmac2(int32_t a, int32_t x, int32_t b, int32_t y)        /* (a x + b y) / 2^31, rounded */
{
    int32_t r, h = 32768;                                   /* + h h = 2^30: half the result's last bit */
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%5,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=d"(r) : "r"(a), "r"(x), "r"(b), "r"(y), "r"(h));
    return r;
}
static inline int32_t fmac1(int32_t a, int32_t x)                              /* a x / 2^31, rounded */
{
    int32_t r, h = 32768;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%3,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=d"(r) : "r"(a), "r"(x), "r"(h));
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
static inline int32_t fmac2(int32_t a, int32_t x, int32_t b, int32_t y)
{
    return (int32_t)(((((int64_t)a * x) >> 23) + (((int64_t)b * y) >> 23) + 128) >> 8);
}
static inline int32_t fmac1(int32_t a, int32_t x)
{
    return (int32_t)(((((int64_t)a * x) >> 23) + 128) >> 8);
}
struct emac_save { int32_t unused; };
static inline void emac_enter(struct emac_save *e) { (void)e; }
static inline void emac_leave(const struct emac_save *e) { (void)e; }
#endif

#define INC_MAX 0x40000000u                 /* Plaits' kMaxFrequency, 0.25 of the sample rate */
#define INC_MIN 4295u                       /* kMinFrequency, 1e-6                            */

/* ---- shared pieces ------------------------------------------------------------------------------- */

/* 0..127 -> Q16 (127 -> 65535) and Q15 */
static inline int32_t k16(int x) { return (x * 33026) >> 6; }
static inline int32_t k15(int x) { return (x * 33026) >> 7; }

static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }

/* stmlib's integrated polyBLEP, Q16 in and out: NextIntegratedBlepSample(t) */
static inline int32_t iblep_next(int32_t t)
{
    int32_t t1 = t >> 1, t2 = (t1 * t1) >> 16, t4 = (t2 * t2) >> 16;
    return 12288 - t1 + ((3 * t2) >> 1) - t4;
}

/* t = d / inc as a Q16 fraction of a sample, d < inc */
static inline int32_t sub_sample(uint32_t d, uint32_t inc)
{
    if (d >= inc)                       /* (a step a knob moved behind the phase: at most a sample) */
        return 65536;
    if (inc < (1u << 24))
        return (int32_t)((d << 8) / ((inc >> 8) | 1));
    return (int32_t)(d / ((inc >> 16) | 1));
}

/* stmlib's InterpolateHermite over a Q15 table, index Q15 in 0..1 scaled by 512 (Plaits' fold tables) */
static inline int32_t hermite512(const int16_t *table, int32_t index)
{
    int32_t i = index >> 6, f = (index & 63) << 6;      /* f: Q12 */
    const int16_t *t = table + i;                        /* table + 1 + i - 1 */
    int32_t xm1 = t[0], x0 = t[1], x1 = t[2], x2 = t[3];
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w = c + v;
    int32_t a = w + v + ((x2 - x0) >> 1), b_neg = w + a;
    return ((((((a * f) >> 12) - b_neg) * f >> 12) + c) * f >> 12) + x0;
}

/* Plaits' Sine(phase) over lut_sine (512 points a cycle), phase Q15 in 0..1.25 */
static inline int32_t sine15(int32_t ph)
{
    int32_t i = ph >> 6, f = ph & 63;
    int32_t a = MACRO_SINE[i], b = MACRO_SINE[i + 1];
    return a + (((b - a) * f) >> 6);
}

/* waveshaping_engine.cc's Tame(): how much of a control to keep as the fundamental rises, Q15 */
static int32_t tame(int32_t f0_q16, int32_t mult_q8, int order)
{
    int32_t f = (f0_q16 * mult_q8) >> 8, max_f = 32768 / order, denom = 32768 - max_f, a;
    if (f <= max_f)
        return 32768;
    if (f - max_f >= denom)
        return 0;
    a = 32768 - (int32_t)(((uint32_t)(f - max_f) << 15) / (uint32_t)denom);
    return (((a * a) >> 15) * a) >> 15;
}

/* A linear ramp from a block's start value to its target (stmlib's ParameterInterpolator). Q23.
 * RAMP_NEW in *prev (an engine just started): no ramp, the block starts at the target. */
#define RAMP_NEW ((int32_t)0x80000000)
struct ramp { int32_t v, d; };
static inline void ramp_init(struct ramp *r, int32_t *prev, int32_t target, int n)
{
    if (*prev == RAMP_NEW)
        *prev = target;
    r->v = *prev * 256;
    r->d = ((target - *prev) * 256) / n;
    *prev = target;
}
static inline int32_t ramp_next(struct ramp *r) { r->v += r->d; return r->v >> 8; }

/* 2^t for t in 0..1 (Q16), as Q16 (65536..131071): a quartic, within 5e-6 (0.01 cent) */
static inline uint32_t exp2_frac(uint32_t t)
{
    return 65536 + ((t * (45416 + ((t * (15831 + ((t * (3392 + ((t * 897) >> 16))) >> 16))) >> 16))) >> 16);
}

/* 2^x, x Q16 (any sign), as Q16 */
static uint32_t exp2_q16(int32_t x)
{
    int32_t e = x >> 16;
    uint32_t y = exp2_frac((uint32_t)x & 0xffff);
    if (e >= 0)
        return e > 14 ? 0x7fffffffu : y << e;
    return e < -16 ? 0 : y >> -e;
}

/* log2(x), x > 0, as Q16; a quartic on the mantissa, within 0.00015 */
static int32_t log2_q16(uint32_t x)
{
    int32_t e = 31, m, q;
    if (!x)
        return -(32 << 16);
    while (!(x & 0x80000000u)) {
        x <<= 1;
        e--;
    }
    m = (int32_t)((x >> 16) & 0x7fff);                   /* the mantissa's fraction, Q15 */
    q = 20775 + ((m * -5263) >> 15);
    q = -44221 + ((m * q) >> 15);
    q = 94245 + ((m * q) >> 15);
    return (e << 16) + (int32_t)(((uint32_t)m * (uint32_t)q) >> 15);
}

/* The MIDI note of a phase increment, Q8 (note 69 = 440 Hz at 48 kHz) */
static int32_t note_q8(uint32_t inc)
{
    return 69 * 256 + (((log2_q16(inc) - 1653513) * 3) >> 6);   /* 12 x 256 / 65536 = 3 / 64 */
}

/* a x b / 65536 for a phase increment a and a ratio b (Q16, up to 2^19) */
static uint32_t mul_inc(uint32_t a, uint32_t b)
{
    return (a >> 16) * b + (((a & 0xffff) * (b >> 4)) >> 12);
}

/* The phase increment whose log2 is x (Q16): 2^x, saturating at 0xffffffff (a frequency of 1) */
static uint32_t inc_of_log2(int32_t x)
{
    int32_t e = x >> 16;
    uint32_t y;
    if (x >= (32 << 16))
        return 0xffffffffu;
    y = exp2_frac((uint32_t)x & 0xffff);
    if (e >= 16)
        return y << (e - 16);
    return e < -1 ? 0 : y >> (16 - e);
}

/* ---- coefficients and the state-variable filter ----------------------------------------------------- *
 * A filter coefficient is a float of sorts: m x 2^-s, the mantissa m in 16384..32767, 11 <= s <= 31 (a value
 * from about 2^-17 to 16, coef_fit()). cmul() multiplies a signal (|x| < 2^27) by it to full precision, in two 16 x 16
 * products: no 64-bit arithmetic. */
struct coef { int32_t m, s; };

/* leading zeros of v > 0 (in C, not the CPU's FF1, which the emulators the tests use do not know) */
static int clz32(uint32_t v)
{
    int z = 0;
    if (!(v & 0xffff0000u)) { z += 16; v <<= 16; }
    if (!(v & 0xff000000u)) { z += 8; v <<= 8; }
    if (!(v & 0xf0000000u)) { z += 4; v <<= 4; }
    if (!(v & 0xc0000000u)) { z += 2; v <<= 2; }
    if (!(v & 0x80000000u)) z += 1;
    return z;
}

static struct coef coef_norm(uint32_t v, int32_t s)    /* v x 2^-s, any v; any s (not yet for cmul) */
{
    struct coef c;
    int z;
    if (!v) {
        c.m = 0;
        c.s = 31;
        return c;
    }
    z = clz32(v);
    c.m = (int32_t)((v << z) >> 17);
    c.s = s + z - 17;
    return c;
}

static struct coef coef_fit(struct coef c)             /* into cmul()'s range: 11 <= s <= 31 */
{
    if (c.s < 11) {
        c.m = 32767;
        c.s = 11;
    } else if (c.s > 31) {
        c.m = c.s - 31 > 15 ? 0 : c.m >> (c.s - 31);
        c.s = 31;
    }
    return c;
}

static struct coef coef_of_log2(int32_t x)             /* 2^x, x Q16 */
{
    return coef_norm(exp2_frac((uint32_t)x & 0xffff), 16 - (x >> 16));
}

static struct coef coef_mul(struct coef a, struct coef b)
{
    return coef_norm((uint32_t)(a.m * b.m), a.s + b.s);
}

static inline int32_t cmul(int32_t m, int32_t s, int32_t x)
{
    return ((m * (x >> 11)) >> (s - 11)) + ((m * (x & 0x7ff)) >> s);
}

static int32_t coef_q(struct coef c, int q)                    /* the value, Qq (below 2^31) */
{
    if (c.s >= q)
        return c.s - q > 31 ? 0 : c.m >> (c.s - q);
    return c.m << (q - c.s);
}

/* stmlib's Svf, in A. Simper's form of the same trapezoidal filter, on the EMAC (Q31 coefficients):
 *   v3 = in - ic2;  bp = v1 = a1 ic1 + a2 v3;  lp = v2 = ic2 + a2 ic1 + a3 v3;  ic1 = 2 v1 - ic1;
 *   ic2 = 2 v2 - ic2;  hp = in - k bp - lp
 * a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 (k = r = 1/q). Signals Q24 (|x| < 128); the states ic1,
 * ic2 are struct macro_svf's s1, s2. */
struct svf_c { int32_t a1, a2, a3, k2; };                           /* Q31; k2 = k / 2 */

static int32_t shift_q(int32_t t, int sh)                     /* t x 2^sh, sh -31..31 */
{
    if (sh >= 0)
        return t * (1 << sh);
    return sh < -31 ? 0 : t >> -sh;
}

/* a1 = 1/D to 31 bits (a hardware division to 16, one Newton step on the EMAC), a2 = g a1, a3 = g a2 with
 * the same g: a filter as stable as the float one up to q 512 at the top of the band (where 1 - |pole|^2
 * is 5e-4 and a1, a2, a3 rounded to 15 bits each were not). */
static void svf_coefs_g(struct svf_c *c, struct coef g, struct coef r)
{
    int32_t g27, k27, d23, rc, e;
    g = coef_fit(g);
    r = coef_fit(r);
    g27 = coef_q(g, 27);
    k27 = coef_q(r, 27);
    d23 = (1 << 23) + fmac1(g27, g27 + k27);                         /* D = 1 + g (g + k), Q23 */
    rc = (int32_t)(0x7fffffffu / (uint32_t)(d23 >> 8)) << 15;          /* 1/D, Q31, 16 bits */
    if (rc <= 0)
        rc = 0x7fffffff;
    e = (1 << 23) - fmac1(d23, rc);                                  /* 1 - D/D', Q23 */
    e = fmac1(rc, e * 256);
    rc = e > 0 && rc > 0x7fffffff - e ? 0x7fffffff : rc + e;
    c->a1 = rc;
    c->a2 = shift_q(fmac1(rc, g.m << 16), 15 - g.s);                   /* g / D */
    c->a3 = shift_q(fmac1(c->a2, g.m << 16), 15 - g.s);                /* g^2 / D */
    c->k2 = r.s <= 13 ? 0x7fffffff : coef_q(r, 30);                    /* k / 2, Q31 (k = 2 at most) */
}

/* g = tan(pi f), FREQUENCY_ACCURATE (Plaits' polynomial, tabulated as tan(pi f) / f), f up to 0.5 */
static struct coef tan_accurate(uint32_t finc)
{
    uint32_t i, f, t;
    if (finc > 0x80000000u)
        finc = 0x80000000u;
    i = finc >> 25;
    f = (finc >> 9) & 0xffff;
    t = MACRO_SVF_TAN[i] + (uint32_t)((((int32_t)MACRO_SVF_TAN[i + (i < 64)] - MACRO_SVF_TAN[i]) * (int32_t)f) >> 16);
    return coef_mul(coef_norm(finc, 32), coef_norm(t, 12));
}

/* g = tan(pi f), FREQUENCY_DIRTY: f (pi + 3.736e-1 pi^3 f^2), f up to 0.25 */
static struct coef tan_dirty(uint32_t finc)
{
    uint32_t f16;
    if (finc > 0x40000000u)
        finc = 0x40000000u;
    f16 = finc >> 16;                                                   /* Q16, up to 16384 */
    return coef_mul(coef_norm(finc, 32), coef_norm(12868 + ((47452 * ((f16 * f16) >> 16)) >> 16), 12));
}

/* stmlib's Svf::set_f_q<FREQUENCY_ACCURATE>: the frequency as a phase increment, the damping r = 1/q */
static void svf_coefs(struct svf_c *k, uint32_t finc, struct coef r)
{
    svf_coefs_g(k, tan_accurate(finc), r);
}

/* One sample: BP and LP (Q24) */
#define SVF_STEP(F, K, IN, BP, LP) do {                                                             \
        int32_t v3_ = (IN) - (F).s2;                                                               \
        BP = fmac2((K).a1, (F).s1, (K).a2, v3_);                                                   \
        LP = (F).s2 + fmac2((K).a2, (F).s1, (K).a3, v3_);                                          \
        (F).s1 = BP + BP - (F).s1;                                                                 \
        (F).s2 = LP + LP - (F).s2;                                                                 \
    } while (0)
#define SVF_HP(K, IN, BP, LP) ((IN) - 2 * fmac1((K).k2, BP) - (LP))


/* After a block: the states kept within +-32 (Q24), so nothing runs away out of range (Plaits' float filter
 * never needs it at the levels its engines feed it) */
static void svf_guard(struct macro_svf *f)
{
    const int32_t lim = 32 << 24;
    f->s1 = f->s1 > lim ? lim : f->s1 < -lim ? -lim : f->s1;
    f->s2 = f->s2 > lim ? lim : f->s2 < -lim ? -lim : f->s2;
}

/* stmlib's Limiter (as Plaits' voice applies it to the engines registered with a negative gain): a peak
 * follower (attack 0.05, release 0.00002 a sample) and 1/peak above 1. x: Q17 in, Q15 out (the 0.8 after
 * it is in the machine's gain). */
static void limit(int32_t *peak, int32_t *x, int n)
{
    int32_t pk = *peak, rp = 0, i;
    if (pk > (1 << 17))
        rp = (int32_t)(0x40000000u / (uint32_t)(pk >> 2));         /* 1/peak, Q15 */
    for (i = 0; i < n; i++) {
        int32_t s = x[i], err;
        if (s > (31 << 17))
            s = 31 << 17;
        if (s < -(31 << 17))
            s = -(31 << 17);
        err = iabs(s) - pk;
        if (err > 0) {                                          /* attack: 1/peak anew */
            pk += ((err >> 6) * 1638) >> 9;
            if (pk > (1 << 17))
                rp = (int32_t)(0x40000000u / (uint32_t)(pk >> 2));
        } else {                                                /* release: one Newton step keeps 1/peak */
            pk += ((err >> 6) * 21475) >> 24;
            if (pk > (1 << 17))
                rp = (rp * (65536 - (((pk >> 2) * rp) >> 15))) >> 15;
        }
        if (pk <= (1 << 17))
            s >>= 2;
        else
            s = ((s >> 6) * rp) >> 11;
        x[i] = s > 65535 ? 65535 : s < -65535 ? -65535 : s;     /* (clipped later; keeps the gain in range) */
    }
    *peak = pk;
}

/* a random 32-bit word (stmlib's Random::GetWord()) */
static inline uint32_t rnd32(uint32_t *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return *rng;
}

/* a random Q15 value, -1..1 (stmlib's Random::GetFloat() x 2 - 1) */
static inline int32_t rnd15(uint32_t *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (int32_t)(*rng >> 16) - 32768;
}

/* Plaits' SinePM: the sine at a 32-bit phase (512 points a cycle, linear), Q15 */
static inline int32_t sin32(uint32_t ph)
{
    int32_t i = (int32_t)(ph >> 23), f = (int32_t)(ph >> 8) & 0x7fff;
    int32_t a = MACRO_SINE[i], b = MACRO_SINE[i + 1];
    return a + (((b - a) * f) >> 15);
}

/* ---- the slope oscillator: plaits/dsp/oscillator/oscillator.h, OSCILLATOR_SHAPE_SLOPE ------------ */

static void slope_init(struct macro_slope *o)
{
    o->phase = 0x80000000u;
    o->next = 0;
    o->high = 1;
}

/* pw: Q16, already kept in 2f..1-2f. out: Q15. */
static void slope_render(struct macro_slope *o, uint32_t inc, int32_t pw, int32_t *out, int n)
{
    uint32_t pw32 = (uint32_t)pw << 16;
    int32_t rup = (int32_t)(0x80000000u / (uint32_t)pw);            /* 1/pw, Q15       */
    int32_t rdown = (int32_t)(0x80000000u / (uint32_t)(65536 - pw)); /* 1/(1-pw), Q15  */
    int32_t disc = (((rup + rdown) >> 7) * (int32_t)(inc >> 16)) >> 12;   /* (up+down) f, Q12 */
    uint32_t phase = o->phase;
    int32_t next = o->next;
    int high = o->high;
    while (n--) {
        uint32_t old = phase;
        int32_t this_s = next, t, b;
        next = 0;
        phase += inc;
        if (high && (phase < old || phase >= pw32)) {               /* the slope turns down */
            t = sub_sample(phase - pw32, inc);
            b = iblep_next(65536 - t);
            this_s -= (b * disc) >> 12;
            next -= (iblep_next(t) * disc) >> 12;
            high = 0;
        }
        if (phase < old) {                                          /* a new cycle: up again */
            t = sub_sample(phase, inc);
            this_s += (iblep_next(65536 - t) * disc) >> 12;
            next += (iblep_next(t) * disc) >> 12;
            high = 1;
        }
        if (high)
            next += (int32_t)(((phase >> 16) * (uint32_t)rup) >> 15);
        else
            next += 65536 - (int32_t)((((phase - pw32) >> 16) * (uint32_t)rdown) >> 15);
        *out++ = this_s - 32768;
    }
    o->phase = phase;
    o->next = next;
    o->high = high;
}

/* ---- WSH: plaits/dsp/engine/waveshaping_engine.cc ----------------------------------------------- */

static const int16_t *const ws_table[6] = {
    MACRO_WS_INVERSE_TAN, MACRO_WS_INVERSE_SIN, MACRO_WS_LINEAR, MACRO_WS_BUMP, MACRO_WS_DOUBLE_BUMP,
    MACRO_WS_DOUBLE_BUMP,
};

static void wsh_init(struct macro_wsh *w)
{
    slope_init(&w->slope);
    slope_init(&w->tri);
    w->prev_shape = RAMP_NEW;
    w->prev_gain = RAMP_NEW;
    w->prev_overtone = RAMP_NEW;
}

/* Keep a slope oscillator's phase running over n samples without rendering them (the AUX path of an
 * engine while the AUX knob is at 0): the next sample starts from the plain slope, without a blep. */
static void slope_skip(struct macro_slope *o, uint32_t inc, int n)
{
    o->phase += inc * (uint32_t)n;
    o->high = o->phase < 0x80000000u;
    o->next = o->high ? (int32_t)(o->phase >> 15) : 131072 - (int32_t)(o->phase >> 15);
}

static void wsh_render(struct macro_wsh *w, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                       int want_out, int want_aux)
{
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k15(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]);
    int32_t f0 = (int32_t)(inc >> 16), pw, slope, amount, sa_att, wf_att, inner, o1, i;
    int16_t index[32];
    struct ramp shape, gain, overtone;

    pw = 32768 + ((morph * 29491) >> 16);                       /* morph * 0.45 + 0.5 */
    if (pw < 2 * f0)
        pw = 2 * f0;
    if (pw > 65536 - 2 * f0)
        pw = 65536 - 2 * f0;
    slope_render(&w->slope, inc, pw, out, n);
    if (want_aux)
        slope_render(&w->tri, inc, 32768, aux, n);
    else
        slope_skip(&w->tri, inc, n);

    slope = 768 + ((iabs(morph - 32768) * 5) >> 8);             /* 3 + |morph - 0.5| * 5, Q8 */
    amount = iabs(harm - 32768);                                /* |harmonics - 0.5| * 2, Q15 */
    sa_att = tame(f0, slope, 16);
    inner = 98304 + 5 * ((amount * sa_att) >> 15);             /* 3 + amount * att * 5, Q15 */
    wf_att = tame(f0, (slope * inner) >> 15, 12);

    ramp_init(&shape, &w->prev_shape, 16384 + ((((harm - 32768) >> 1) * sa_att) >> 15), n);
    ramp_init(&gain, &w->prev_gain, 983 + ((((timb * wf_att) >> 15) * 15073) >> 15), n);
    o1 = (timb * (65536 - timb)) >> 15;                         /* t (2 - t) */
    ramp_init(&overtone, &w->prev_overtone, (o1 * (65536 - o1)) >> 15, n);

    /* the waveshaper: the slope through two shape tables, crossfaded, times the folder's gain -> the
     * folder's index, which OUT and AUX share. The usual case, knobs not moving: the tables, the
     * crossfade and the gain are the block's. */
    if (shape.d == 0 && gain.d == 0) {
        int32_t s = (shape.v >> 8) * 4, sf, g = gain.v >> 8;
        const int16_t *s1, *s2;
        if (s > 131071)
            s = 131071;
        if (s < 0)
            s = 0;
        sf = s & 32767;
        s1 = ws_table[s >> 15];
        s2 = ws_table[(s >> 15) + 1];
        for (i = 0; i < n; i++) {
            int32_t idx = 127 * out[i] + (128 << 15), wi = (idx >> 15) & 255, wf = idx & 32767, x, y, ix;
            x = s1[wi] + (((s1[wi + 1] - s1[wi]) * wf) >> 15);
            y = s2[wi] + (((s2[wi + 1] - s2[wi]) * wf) >> 15);
            ix = (((x + (((y - x) * sf) >> 15)) * g) >> 15) + 16384;
            index[i] = (int16_t)(ix < 0 ? 0 : ix > 32767 ? 32767 : ix);
        }
    } else
    for (i = 0; i < n; i++) {
        int32_t s = ramp_next(&shape) * 4, si, sf, idx, wi, wf, x, y, mix, ix;
        const int16_t *s1, *s2;
        if (s > 131071)
            s = 131071;
        if (s < 0)
            s = 0;
        si = s >> 15;
        sf = s & 32767;
        s1 = ws_table[si];
        s2 = ws_table[si + 1];
        idx = 127 * out[i] + (128 << 15);
        wi = (idx >> 15) & 255;
        wf = idx & 32767;
        x = s1[wi] + (((s1[wi + 1] - s1[wi]) * wf) >> 15);
        y = s2[wi] + (((s2[wi + 1] - s2[wi]) * wf) >> 15);
        mix = x + (((y - x) * sf) >> 15);
        ix = ((mix * ramp_next(&gain)) >> 15) + 16384;
        index[i] = (int16_t)(ix < 0 ? 0 : ix > 32767 ? 32767 : ix);
    }
    if (want_out)
        for (i = 0; i < n; i++)
            out[i] = hermite512(MACRO_FOLD, index[i]);
    if (want_aux)
        for (i = 0; i < n; i++) {
            int32_t sine = sine15((aux[i] >> 2) + 16384), fold2 = -hermite512(MACRO_FOLD_2, index[i]);
            aux[i] = sine + (((fold2 - sine) * ramp_next(&overtone)) >> 15);
        }
}

/* ---- FM: plaits/dsp/engine/fm_engine.cc ----------------------------------------------------------- *
 * Plaits runs it 4x oversampled with an 8-tap decimator; so does a note here that starts with feedback
 * (fm_render4: Plaits' own arithmetic, it matches Plaits within rounding). A note without feedback runs
 * 2x (fm_render2) with the half-band [-1 0 9 16 9 0 -1] / 32 (shifts and adds only): the aliasing measured
 * within 0.5 dB of Plaits' at notes 84-96 and high indices, the treble a little brighter (-1.2 dB at 15 kHz
 * where Plaits' is -2.8), for half the work. With feedback, 2x is not the same: the carrier's highest
 * partials alias inside the feedback loop and change it (measured: up to +30 % level at full MORPH). */

static void fm_init(struct macro_fm *f)
{
    int i;
    f->carrier = f->modulator = f->sub = 0;
    f->prev = 0;
    for (i = 0; i < 6; i++)
        f->hc[i] = f->hs[i] = 0;
    f->car_fir = f->sub_fir = 0;
    f->prev_amount = f->prev_feedback = RAMP_NEW;
    f->os4 = 0;
}

/* At a note start: 4x for a note with feedback (MORPH off its middle), 2x without. Chosen per note, so a
 * MORPH turned during a note keeps the note's rate (no switch, no click); a p-lock lands on a trig. */
static void fm_trig(struct macro_fm *f, const uint8_t *p)
{
    int32_t fb = k16(p[MACRO_P_MORPH]) - 32768;
    f->os4 = fb > 1024 || fb < -1024;
}

/* the half-band decimator: x[] holds six samples of history and then 2n new ones; out[i] is centred on
 * x[2i + 4] */
static void halfband(const int32_t *x, int32_t *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        const int32_t *c = x + 2 * i + 4;
        int32_t s1 = c[-1] + c[1];
        out[i] = (c[0] * 16 + s1 * 9 - (c[-3] + c[3])) >> 5;
    }
}

static uint32_t fm_controls(struct macro_fm *f, const uint8_t *p, uint32_t inc, uint32_t c_inc, struct ramp *amount,
                            struct ramp *feedback, int n);

static void fm_render2(struct macro_fm *f, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                       int want_aux)
{
    uint32_t c_inc = inc >> 1, m_inc;                   /* 2x oversampled: half the step */
    uint32_t car = f->carrier, mod = f->modulator, sub = f->sub;
    int32_t prev = f->prev, xc[64 + 6], xs[64 + 6], *pc = xc + 6, *ps = xs + 6, i;
    struct ramp amount, feedback;
    m_inc = fm_controls(f, p, inc, c_inc, &amount, &feedback, n);
    for (i = 0; i < 6; i++) {
        xc[i] = f->hc[i];
        xs[i] = f->hs[i];
    }

    /* one oversampled step: the modulator (phase feedback PFB or self modulation MFB), the carrier, the
     * feedback's one-pole (0.05 a step at 4x = 0.0975 at 2x), the sub (Plaits' AUX) if wanted */
#define FM_STEP(PFB, MFB, SUB) do {                                                                \
        int32_t m_, c_;                                                                            \
        if (PFB)                                                                                   \
            mod += m_inc + (uint32_t)((int32_t)(m_inc >> 15) * ((prev * pfb) >> 15));              \
        else                                                                                       \
            mod += m_inc;                                                                          \
        car += c_inc;                                                                              \
        m_ = (MFB) ? sin32(mod + ((uint32_t)(mfb * prev) << 2)) : sin32(mod);                      \
        c_ = sin32(car + ((uint32_t)(amt * m_) << 3));                                             \
        prev += ((c_ - prev) * 3195) >> 15;                                                        \
        *pc++ = c_;                                                                                \
        if (SUB) {                                                                                 \
            sub += c_inc >> 1;                                                                     \
            *ps++ = sin32(sub + ((uint32_t)(amt * c_) << 1));        /* amount x carrier x 0.25 */ \
        }                                                                                          \
    } while (0)
#define FM_LOOP(PFB, MFB, SUB) do {                                                                \
        FM_STEP(PFB, MFB, SUB);                                                                    \
        FM_STEP(PFB, MFB, SUB);                                                                    \
    } while (0)
    for (i = 0; i < n; i++) {
        int32_t amt = ramp_next(&amount), fb = ramp_next(&feedback);
        int32_t pfb = fb < 0 ? (fb * fb) >> 16 : 0;                 /* phase feedback, 0.5 fb^2 */
        int32_t mfb = fb > 0 ? (fb * fb) >> 17 : 0;                 /* self modulation, 0.25 fb^2 */
        if (want_aux)                               /* the general step: the same sums, with zeros */
            FM_LOOP(1, 1, 1);
        else if (pfb || mfb)                        /* feedback turned up during a 2x note */
            FM_LOOP(1, 1, 0);
        else                                        /* the usual 2x note: no feedback, OUT only */
            FM_LOOP(0, 0, 0);
    }
#undef FM_LOOP
#undef FM_STEP
    halfband(xc, out, n);
    for (i = 0; i < 6; i++)
        f->hc[i] = xc[2 * n + i];
    if (want_aux) {
        halfband(xs, aux, n);
        for (i = 0; i < 6; i++)
            f->hs[i] = xs[2 * n + i];
    } else {
        sub += (c_inc >> 1) * 2 * (uint32_t)n;
    }
    f->carrier = car;
    f->modulator = mod;
    f->sub = sub;
    f->prev = prev;
}

/* the controls both rates share: the modulator's increment (at c_inc's rate), the amount and feedback ramps */
static uint32_t fm_controls(struct macro_fm *f, const uint8_t *p, uint32_t inc, uint32_t c_inc, struct ramp *amount,
                            struct ramp *feedback, int n)
{
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k15(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]);
    int32_t ratio, hf, t2, mn, ri = harm >> 9, rf = (harm << 7) & 0xffff;
    uint32_t m_inc;
    ratio = MACRO_FM_RATIO[ri] + (((MACRO_FM_RATIO[ri + 1] - MACRO_FM_RATIO[ri]) * rf) >> 16);
    m_inc = mul_inc(c_inc, exp2_q16((ratio * 21845) >> 10));
    if (m_inc > 0x80000000u)
        m_inc = 0x80000000u;
    mn = note_q8(inc) - 24 * 256 + ratio;
    hf = 32768 - (((mn - 72 * 256) * 13107) >> 12);
    hf = hf < 0 ? 0 : hf > 32768 ? 32768 : hf;
    hf = (hf * hf) >> 15;
    t2 = (timb * timb) >> 15;
    ramp_init(amount, &f->prev_amount, (t2 * hf) >> 15, n);
    ramp_init(feedback, &f->prev_feedback, morph - 32768, n);
    return m_inc;
}

static void fm_render4(struct macro_fm *f, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                       int want_aux)
{
    uint32_t c_inc = inc >> 2, m_inc;                   /* 4x oversampled: a quarter of the step */
    const int32_t f0 = MACRO_FIR4X[0], f1 = MACRO_FIR4X[1], f2 = MACRO_FIR4X[2], f3 = MACRO_FIR4X[3];
    uint32_t car = f->carrier, mod = f->modulator, sub = f->sub;
    int32_t prev = f->prev, chead = f->car_fir, shead = f->sub_fir, i;
    struct ramp amount, feedback;
    m_inc = fm_controls(f, p, inc, c_inc, &amount, &feedback, n);
#define FM_STEP(CA, CB, PFB, MFB, SUB) do {                                                        \
        int32_t m_, c_;                                                                            \
        if (PFB)                                                                                   \
            mod += m_inc + (uint32_t)((int32_t)(m_inc >> 15) * ((prev * pfb) >> 15));              \
        else                                                                                       \
            mod += m_inc;                                                                          \
        car += c_inc;                                                                              \
        m_ = (MFB) ? sin32(mod + ((uint32_t)(mfb * prev) << 2)) : sin32(mod);                      \
        c_ = sin32(car + ((uint32_t)(amt * m_) << 3));                                             \
        prev += ((c_ - prev) * 1638) >> 15;                         /* ONE_POLE 0.05 */            \
        chead += c_ * (CA);                                                                        \
        ctail += c_ * (CB);                                                                        \
        if (SUB) {                                                                                 \
            int32_t s_;                                                                            \
            sub += c_inc >> 1;                                                                     \
            s_ = sin32(sub + ((uint32_t)(amt * c_) << 1));                                         \
            shead += s_ * (CA);                                                                    \
            stail += s_ * (CB);                                                                    \
        }                                                                                          \
    } while (0)
#define FM_SAMPLE(PFB, MFB, SUB) do {                                                              \
        int32_t ctail = 0, stail = 0;                                                              \
        FM_STEP(f3, f0, PFB, MFB, SUB);                                                            \
        FM_STEP(f2, f1, PFB, MFB, SUB);                                                            \
        FM_STEP(f1, f2, PFB, MFB, SUB);                                                            \
        FM_STEP(f0, f3, PFB, MFB, SUB);                                                            \
        out[i] = chead >> 15;                                                                      \
        chead = ctail;                                                                             \
        if (SUB) {                                                                                 \
            aux[i] = shead >> 15;                                                                  \
            shead = stail;                                                                         \
        }                                                                                          \
        (void)stail;                                                                               \
    } while (0)
    for (i = 0; i < n; i++) {
        int32_t amt = ramp_next(&amount), fb = ramp_next(&feedback);
        int32_t pfb = fb < 0 ? (fb * fb) >> 16 : 0;
        int32_t mfb = fb > 0 ? (fb * fb) >> 17 : 0;
        if (want_aux)                               /* the general step: the same sums, with zeros */
            FM_SAMPLE(1, 1, 1);
        else if (pfb)
            FM_SAMPLE(1, 0, 0);
        else if (mfb)
            FM_SAMPLE(0, 1, 0);
        else                                        /* MORPH back near its middle during a 4x note */
            FM_SAMPLE(1, 1, 0);
    }
#undef FM_SAMPLE
#undef FM_STEP
    if (!want_aux)
        sub += (c_inc >> 1) * 4 * (uint32_t)n;
    f->carrier = car;
    f->modulator = mod;
    f->sub = sub;
    f->prev = prev;
    f->car_fir = chead;
    f->sub_fir = shead;
}

static void fm_render(struct macro_fm *f, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                      int want_aux)
{
    if (f->os4)
        fm_render4(f, p, inc, out, aux, n, want_aux);
    else
        fm_render2(f, p, inc, out, aux, n, want_aux);
}

/* ---- NOISE: plaits/dsp/engine/noise_engine.cc ------------------------------------------------------- *
 * Two clocked noises (plaits/dsp/noise/clocked_noise.h) at TIMBRE's clock, the second's clock HARMONICS
 * apart; OUT: the first through a filter at the note's pitch, LP (HARMONICS 0) to BP to HP (1), MORPH its
 * resonance; AUX: two band-passes, at the note and HARMONICS (-2..+2 octaves) from it. A note start is
 * Plaits' trigger: both clocks restart, TIMBRE spans its patched range (-24..128). The filters' controls
 * are the block's (Plaits glides them over the block). */

static void cnoise_render(struct macro_cnoise *c, uint32_t *rng, int sync, uint32_t finc, int32_t *out, int n)
{
    uint32_t phase = c->phase;
    int32_t sample = c->sample, next = c->next, raw_amount = 0, i;
    if (finc > 0x40000000u) {                              /* clocks over 1/4 the rate: some raw noise */
        raw_amount = (int32_t)((finc - 0x40000000u) >> 15);
        if (raw_amount > 32768)
            raw_amount = 32768;
    }
    if (sync)
        phase = 0;
    for (i = 0; i < n; i++) {
        int32_t this_s = next, raw = 0;
        uint32_t old = phase;
        next = 0;
        if (raw_amount)
            raw = rnd15(rng);
        phase += finc;
        if (phase < old || sync) {                          /* a new value: a band-limited step */
            int32_t t = sync ? 65536 : sub_sample(phase, finc), u = 65536 - t, disc;
            sync = 0;
            if (!raw_amount)
                raw = rnd15(rng);
            disc = raw - sample;
            this_s += (disc * (((t >> 1) * (t >> 1)) >> 16)) >> 15;
            next -= (disc * (((u >> 1) * (u >> 1)) >> 16)) >> 15;
            sample = raw;
        }
        next += sample;
        out[i] = raw_amount ? this_s + ((((raw - this_s) >> 1) * raw_amount) >> 14) : this_s;
    }
    c->phase = phase;
    c->sample = sample;
    c->next = next;
}

static void noise_init(struct macro_noise *z)
{
    int i;
    for (i = 0; i < 2; i++) {
        z->src[i].phase = 0;
        z->src[i].sample = z->src[i].next = 0;
    }
    z->mm.s1 = z->mm.s2 = z->bp.s1 = z->bp.s2 = 0;
    z->sync = 0;
}

static void noise_render(struct macro_noise *z, uint32_t *rng, const uint8_t *p, uint32_t inc, int32_t *out,
                         int32_t *aux, int n, int want_aux)
{
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]);
    int32_t l0 = log2_q16(inc), lc, lv, cb, cx, use_hp, gm, gsh, i;
    int32_t nz[32];
    struct coef r, gain;
    struct svf_c k0, k1;
    int sync = z->sync;
    z->sync = 0;

    /* the clock: note TIMBRE x 152 - 24 (1653513: log2 of note 69's increment, Q16) */
    lc = 1653513 + (timb * 152 - 93 * 65536) / 12;
    cnoise_render(&z->src[0], rng, sync, inc_of_log2(lc), nz, n);

    /* q = 0.5 x 2^(10 MORPH): r = 1/q; the input's gain 1/sqrt((0.5 + q) x 40 x f0), at most 16 */
    r = coef_fit(coef_of_log2(65536 - 10 * morph));
    lv = log2_q16(65536 + exp2_q16(10 * morph)) - 17 * 65536 + 348779 + l0 - 32 * 65536;   /* 348779: log2 40 */
    lv = -(lv >> 1);
    gain = coef_fit(coef_of_log2(lv > 4 * 65536 ? 4 * 65536 : lv));
    gm = gain.m;
    gsh = gain.s - 9;                                       /* Q15 noise -> Q24 */

    /* the LP-to-HP mode: HARMONICS 0 LP, 0.5 BP, 1 HP; Q8 */
    if (harm <= 32768) {
        cb = harm >> 7;
        cx = 256 - cb;
        use_hp = 0;
    } else {
        cb = 512 - (harm >> 7);
        cx = 256 - (harm >> 7);
        use_hp = 1;
    }

    svf_coefs(&k0, inc, r);
    if (use_hp)
        for (i = 0; i < n; i++) {
            int32_t in = (gm * nz[i]) >> gsh, bp, lp;
            SVF_STEP(z->mm, k0, in, bp, lp);
            out[i] = ((bp >> 10) * cb + (SVF_HP(k0, in, bp, lp) >> 10) * cx) >> 5;   /* Q17 */
            nz[i] = bp;
        }
    else
        for (i = 0; i < n; i++) {
            int32_t in = (gm * nz[i]) >> gsh, bp, lp;
            SVF_STEP(z->mm, k0, in, bp, lp);
            out[i] = ((bp >> 10) * cb + (lp >> 10) * cx) >> 5;
            nz[i] = bp;
        }
    svf_guard(&z->mm);
    if (!want_aux)
        return;
    cnoise_render(&z->src[1], rng, sync, inc_of_log2(lc + 4 * harm - 2 * 65536), aux, n);
    svf_coefs(&k1, inc_of_log2(l0 + 4 * harm - 2 * 65536), r);
    for (i = 0; i < n; i++) {
        int32_t in = (gm * aux[i]) >> gsh, bp, lp;
        SVF_STEP(z->bp, k1, in, bp, lp);
        aux[i] = (nz[i] + bp) >> 7;
        (void)lp;
    }
    svf_guard(&z->bp);
}

/* ---- PARTICLE: plaits/dsp/engine/particle_engine.cc, plaits/dsp/noise/particle.h ---------------------- *
 * Six particles; each draws an impulse a sample with the probability TIMBRE's density sets, its height
 * random (0..1), into its own resonant band-pass (stmlib's Svf, FREQUENCY_DIRTY) at a frequency HARMONICS
 * spreads at random around the note (+-4 octaves at most), drawn again at the first impulse of a block.
 * OUT: their sum through a low-pass at the note; AUX: the impulses themselves. A note start makes every
 * particle fire at once (Plaits' trigger). MORPH: above its middle the band-passes' resonance; below it
 * Plaits adds a reverb-like diffuser (16 KB of delay a voice), which this port leaves out: there the
 * band-passes keep the resonance at the middle.
 *
 * Here the impulses come from exponential waiting times (the same Bernoulli process, drawn once an
 * impulse instead of every sample), and a particle whose band-pass has rung out and has no impulse in
 * the block is skipped. */

static void particles_init(struct macro_particles *z)
{
    int i;
    for (i = 0; i < MACRO_PARTICLES; i++) {
        struct macro_particle *q = &z->p[i];
        q->e = 1 << 26;
        q->s1 = q->s2 = 0;
        q->a1 = q->a2 = q->a3 = q->c1m = q->c2m = 0;
        q->c1s = q->c2s = 31;
    }
    z->post.s1 = z->post.s2 = 0;
    z->sync = 0;
}

/* an exponential waiting time, Q26: -ln(u), u uniform in (0, 1] */
static int32_t exp_wait(uint32_t *rng)
{
    uint32_t u = rnd32(rng) | 1;
    return ((32 << 16) - log2_q16(u)) * 710;                  /* x ln 2 x 2^10 */
}

/* a particle's band-pass at f (log2, Q16, 2^-16..0.25), damping k = 1/q; its input's gain 2^lpre. The
 * filter is linear, so an impulse x adds to the step without it: bp and lp by a2 x and a3 x, s1 and s2 by
 * twice those (c1, c2: pre_gain a2, pre_gain a3). Pre_gain itself reaches the thousands at low
 * frequencies, c1 and c2 stay small. */
static struct coef coef_cap(struct coef c)             /* c x (Q15) >> (s - 9) stays a shift of 0..31 */
{
    if (c.s < 9) {
        c.m = 32767;
        c.s = 9;
    } else if (c.s > 40) {
        c.m = c.s - 40 > 15 ? 0 : c.m >> (c.s - 40);
        c.s = 40;
    }
    return c;
}

static void particle_coefs(struct macro_particle *q, int32_t lf, struct coef k, int32_t lpre)
{
    struct svf_c c;
    struct coef pre = coef_of_log2(lpre), c1, c2;
    svf_coefs_g(&c, coef_fit(tan_dirty(inc_of_log2(lf + 32 * 65536))), k);
    q->a1 = c.a1;
    q->a2 = c.a2;
    q->a3 = c.a3;
    c1 = coef_cap(coef_mul(pre, coef_norm((uint32_t)c.a2, 31)));             /* pre_gain g h */
    c2 = coef_cap(coef_mul(pre, coef_norm((uint32_t)c.a3, 31)));             /* pre_gain g^2 h */
    q->c1m = c1.m;
    q->c1s = c1.s;
    q->c2m = c2.m;
    q->c2s = c2.s;
}

static void particle_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux,
                            int n, int want_aux)
{
    struct macro_particles *z = &m->e.part;
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]);
    int32_t lf0 = log2_q16(inc) - 32 * 65536, ld, lq, lpre_base, spread, t2, pd, i, j;
    int32_t acc[32];
    struct coef k;
    struct svf_c kp;
    int sync = z->sync;
    z->sync = 0;

    /* density: NoteToFrequency(60 + 72 TIMBRE^2)^2 / 6 an impulse a sample; pd: Q26 */
    t2 = (int32_t)(((uint32_t)timb * (uint32_t)timb) >> 16);
    ld = 2 * (1653513 - 32 * 65536 + ((72 * t2 - 9 * 65536) / 12)) - 169408;  /* 169408: log2 6 */
    pd = (int32_t)inc_of_log2(ld + 26 * 65536);
    if (pd < 1)
        pd = 1;
    /* q = 0.5 + 2^(20 (MORPH - 0.5)) above the middle, 1.5 below */
    lq = log2_q16(32768 + (morph > 32768 ? exp2_q16(20 * (morph - 32768)) : 65536)) - 16 * 65536;
    k = coef_fit(coef_of_log2(-lq));
    /* pre_gain = 0.5 / sqrt(q f sqrt(density)): its log2 without f's part */
    lpre_base = -65536 - ((lq + (ld >> 1)) >> 1);
    spread = (int32_t)(((uint32_t)harm * (uint32_t)harm) >> 14);           /* 4 HARMONICS^2 octaves, Q16 */

    for (i = 0; i < n; i++)
        acc[i] = 0;
    if (want_aux)
        for (i = 0; i < n; i++)
            aux[i] = 0;
    for (j = 0; j < MACRO_PARTICLES; j++) {
        struct macro_particle *q = &z->p[j];
        struct macro_svf f;
        struct svf_c c;
        int32_t at, s;
        int fresh = 1;
        if (sync)
            q->e = 0;
        /* the first impulse this block, if any */
        at = q->e < pd * n ? q->e / pd : n;
        if (at >= n) {
            q->e -= pd * n;
            if (!q->s1 && !q->s2)
                continue;                                       /* silent: nothing to compute */
        }
        f.s1 = q->s1;
        f.s2 = q->s2;
        c.a1 = q->a1;
        c.a2 = q->a2;
        c.a3 = q->a3;
        for (i = 0; i < n; i++) {
            int32_t bp, lp;
            if (i == at) {                                      /* an impulse */
                s = (sync && fresh) ? 32768 : (int32_t)(rnd32(&m->rng) >> 17);
                if (fresh) {                                    /* the band-pass's frequency for this block */
                    int32_t u = (int32_t)(rnd32(&m->rng) >> 16) - 32768;      /* -1..1, Q15 */
                    int32_t lf = lf0 + (((spread >> 2) * u) >> 13);
                    lf = lf > -2 * 65536 ? -2 * 65536 : lf < -16 * 65536 ? -16 * 65536 : lf;   /* f: 2^-16..0.25 */
                    particle_coefs(q, lf, k, lpre_base - (lf >> 1));
                    c.a1 = q->a1;
                    c.a2 = q->a2;
                    c.a3 = q->a3;
                    fresh = 0;
                }
                if (want_aux)
                    aux[i] += s;
                q->e = exp_wait(&m->rng);
                at = q->e < pd * (n - i - 1) ? i + 1 + q->e / pd : n;
                if (at >= n)
                    q->e -= pd * (n - i - 1);
                SVF_STEP(f, c, 0, bp, lp);
                lp = (q->c1m * s) >> (q->c1s - 9);              /* Q15 -> Q24 */
                bp += lp;
                f.s1 += 2 * lp;
                lp = (q->c2m * s) >> (q->c2s - 9);
                f.s2 += 2 * lp;
                acc[i] += bp;
                continue;
            }
            SVF_STEP(f, c, 0, bp, lp);
            acc[i] += bp;
            (void)lp;
        }
        svf_guard(&f);
        if (iabs(f.s1) < 512 && iabs(f.s2) < 512)
            f.s1 = f.s2 = 0;                                    /* rung out (-90 dB) */
        q->s1 = f.s1;
        q->s2 = f.s2;
    }
    if (want_aux)
        for (i = 0; i < n; i++)
            aux[i] = aux[i] > 65535 ? 65535 : aux[i];          /* (clipped later; keeps the gain in range) */

    /* the low-pass at the note, q 0.5 (FREQUENCY_DIRTY, f at most 0.49) */
    svf_coefs_g(&kp, tan_dirty(inc), coef_norm(2, 0));
    for (i = 0; i < n; i++) {
        int32_t bp, lp, x = acc[i];
        x = x > (64 << 24) ? 64 << 24 : x < -(64 << 24) ? -(64 << 24) : x;
        SVF_STEP(z->post, kp, x, bp, lp);
        out[i] = lp >> 6;                                       /* Q24 x 2 (Plaits' pre-gain) -> Q17 */
        (void)bp;
    }
    svf_guard(&z->post);
}

/* ---- the machine ---------------------------------------------------------------------------------- */

/* the gains Plaits' voice gives each engine's OUT and AUX (voice.cc, RegisterInstance), Q15. An engine
 * Plaits registers with a negative gain goes through its limiter (limit()) and then 0.8. */
static const int16_t gain_out[MACRO_ENGINES] = {22938, 19661, 26214, 26214};  /* WSH .7, FM .6, NOISE, PART lim */
static const int16_t gain_aux[MACRO_ENGINES] = {19661, 19661, 26214, 32767};  /* WSH .6, FM .6, NOISE lim, PART 1 */

const char *const macro_engine_name[MACRO_ENGINES] = {"WSHAPE", "2OP FM", "NOISE", "PARTCL"};

int macro_engine_of(int b)
{
    int e = b >> MACRO_ZONE_SHIFT;
    return e < MACRO_ENGINES ? e : MACRO_ENGINES - 1;
}

static void engine_init(struct macro_voice *m)
{
    m->lim_out = m->lim_aux = 1 << 16;                     /* the limiters start at a peak of 0.5 */
    switch (m->engine) {
    case MACRO_WSH:   wsh_init(&m->e.wsh); break;
    case MACRO_FM:    fm_init(&m->e.fm); break;
    case MACRO_NOISE: noise_init(&m->e.noise); break;
    case MACRO_PARTICLE: particles_init(&m->e.part); break;
    default: break;
    }
}

void macro_init(struct macro_voice *m)
{
    m->engine = 0;
    m->latch = 1;
    m->pad[0] = m->pad[1] = 0;
    m->rng = 0x2545f491u;
    engine_init(m);
}

void macro_trig(struct macro_voice *m)
{
    m->latch = 1;
}

static void macro_render_e(struct macro_voice *m, const uint8_t *p, uint32_t inc, int16_t *out, int n);

void macro_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct emac_save e;
    emac_enter(&e);
    macro_render_e(m, p, inc, out, n);
    emac_leave(&e);
}

static void macro_render_e(struct macro_voice *m, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int32_t o[32], a[32], go, ga, mix;
    int i;
    if (m->latch) {                                 /* a note start: the engine, and its per-note choices */
        int e = macro_engine_of(p[MACRO_P_ENGINE]);
        m->latch = 0;
        if (e != m->engine) {
            m->engine = (uint8_t)e;
            engine_init(m);
        }
        if (m->engine == MACRO_FM)
            fm_trig(&m->e.fm, p);
        if (m->engine == MACRO_NOISE)
            m->e.noise.sync = 1;
        if (m->engine == MACRO_PARTICLE)
            m->e.part.sync = 1;
    }
    if (inc > INC_MAX)
        inc = INC_MAX;
    if (inc < INC_MIN)
        inc = INC_MIN;
    mix = k15(p[MACRO_P_AUX]);
    switch (m->engine) {
    case MACRO_WSH: wsh_render(&m->e.wsh, p, inc, o, a, n, mix < 32767, mix > 0); break;
    case MACRO_FM:  fm_render(&m->e.fm, p, inc, o, a, n, mix > 0); break;
    case MACRO_NOISE:
        noise_render(&m->e.noise, &m->rng, p, inc, o, a, n, mix > 0);
        if (mix < 32767)
            limit(&m->lim_out, o, n);
        if (mix > 0)
            limit(&m->lim_aux, a, n);
        break;
    case MACRO_PARTICLE:
        particle_render(m, p, inc, o, a, n, mix > 0);
        if (mix < 32767)
            limit(&m->lim_out, o, n);
        break;
    default:
        for (i = 0; i < n; i++)
            out[i] = 0;
        return;
    }
    go = gain_out[m->engine];
    ga = gain_aux[m->engine];
    if (mix == 0) {                                 /* OUT only (the default) */
        for (i = 0; i < n; i++) {
            int32_t x = (o[i] * go) >> 15;
            out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        return;
    }
    if (mix >= 32767) {                             /* AUX only */
        for (i = 0; i < n; i++) {
            int32_t y = (a[i] * ga) >> 15;
            out[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = (o[i] * go) >> 15, y = (a[i] * ga) >> 15;
        x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        y = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
        x += ((y - x) * mix) >> 15;
        out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}
