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

/* code run once a block or less (coefficients, starts): built for size, the mod's RAM is short */
#define COLD __attribute__((noinline, optimize("Os")))

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
static inline int32_t fmac1t(int32_t a, int32_t x)                             /* a x / 2^31, truncated */
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
static inline int32_t fmac2(int32_t a, int32_t x, int32_t b, int32_t y)
{
    return (int32_t)(((((int64_t)a * x) >> 23) + (((int64_t)b * y) >> 23) + 128) >> 8);
}
static inline int32_t fmac1(int32_t a, int32_t x)
{
    return (int32_t)(((((int64_t)a * x) >> 23) + 128) >> 8);
}
static inline int32_t fmac1t(int32_t a, int32_t x)
{
    return (int32_t)((((int64_t)a * x) >> 23) >> 8);
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
static COLD int32_t tame(int32_t f0_q16, int32_t mult_q8, int order)
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
static COLD int32_t note_q8(uint32_t inc)
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
static COLD void svf_coefs_g(struct svf_c *c, struct coef g, struct coef r)
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
static COLD struct coef tan_accurate(uint32_t finc)
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
static COLD struct coef tan_dirty(uint32_t finc)
{
    uint32_t f16;
    if (finc > 0x40000000u)
        finc = 0x40000000u;
    f16 = finc >> 16;                                                   /* Q16, up to 16384 */
    return coef_mul(coef_norm(finc, 32), coef_norm(12868 + ((47452 * ((f16 * f16) >> 16)) >> 16), 12));
}

/* stmlib's Svf::set_f_q<FREQUENCY_ACCURATE>: the frequency as a phase increment, the damping r = 1/q */
static COLD void svf_coefs(struct svf_c *k, uint32_t finc, struct coef r)
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

static COLD void slope_init(struct macro_slope *o)
{
    o->phase = 0x80000000u;
    o->next = 0;
    o->high = 1;
}

/* pw: Q16, already kept in 2f..1-2f. out: Q15. */
static void slope_render(struct macro_slope *__restrict o, uint32_t inc, int32_t pw, int32_t *__restrict out, int n)
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

static COLD void wsh_init(struct macro_wsh *w)
{
    slope_init(&w->slope);
    slope_init(&w->tri);
    w->prev_shape = RAMP_NEW;
    w->prev_gain = RAMP_NEW;
    w->prev_overtone = RAMP_NEW;
}

/* Keep a slope oscillator's phase running over n samples without rendering them (the AUX path of an
 * engine while the AUX knob is at 0): the next sample starts from the plain slope, without a blep. */
static COLD void slope_skip(struct macro_slope *o, uint32_t inc, int n)
{
    o->phase += inc * (uint32_t)n;
    o->high = o->phase < 0x80000000u;
    o->next = o->high ? (int32_t)(o->phase >> 15) : 131072 - (int32_t)(o->phase >> 15);
}

static void wsh_render(struct macro_wsh *__restrict w, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux, int n,
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
static COLD void fm_trig(struct macro_fm *f, const uint8_t *p)
{
    int32_t fb = k16(p[MACRO_P_MORPH]) - 32768;
    f->os4 = fb > 1024 || fb < -1024;
}

/* the half-band decimator: x[] holds six samples of history and then 2n new ones; out[i] is centred on
 * x[2i + 4] */
static void halfband(const int32_t *__restrict x, int32_t *__restrict out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        const int32_t *c = x + 2 * i + 4;
        int32_t s1 = c[-1] + c[1];
        out[i] = (c[0] * 16 + s1 * 9 - (c[-3] + c[3])) >> 5;
    }
}

static COLD uint32_t fm_controls(struct macro_fm *f, const uint8_t *p, uint32_t inc, uint32_t c_inc, struct ramp *amount,
                            struct ramp *feedback, int n);

/* fm_render2's usual case: no feedback, OUT only. A step at a time, so that its values fit the registers */
static void fm2_plain(uint32_t *carp, uint32_t *modp, int32_t *prevp, uint32_t c_inc, uint32_t m_inc,
                      struct ramp *amount, int32_t *__restrict pc, int n)
{
    uint32_t car = *carp, mod = *modp;
    int32_t prev = *prevp, v = amount->v, d = amount->d, amt = 0, j;
    for (j = 0; j < 2 * n; j++) {
        int32_t m_, c_;
        if (!(j & 1)) {
            v += d;
            amt = v >> 8;
        }
        mod += m_inc;
        car += c_inc;
        m_ = sin32(mod);
        c_ = sin32(car + ((uint32_t)(amt * m_) << 3));
        prev += ((c_ - prev) * 3195) >> 15;
        *pc++ = c_;
    }
    amount->v = v;
    *carp = car;
    *modp = mod;
    *prevp = prev;
}

static void fm_render2(struct macro_fm *__restrict f, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux, int n,
                       int want_aux)
{
    uint32_t c_inc = inc >> 1, m_inc;                   /* 2x oversampled: half the step */
    uint32_t car = f->carrier, mod = f->modulator, sub = f->sub;
    int32_t prev = f->prev, xc[64 + 6], xs[64 + 6], *pc = xc + 6, *ps = xs + 6, i, fast;
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
    /* MORPH still at its middle, OUT only (the usual 2x note): no feedback, no sub; else the general step,
     * every term (zeros where off; the sub then runs anyway) */
    fast = feedback.d == 0 && !want_aux && (feedback.v >> 8) <= 362 && (feedback.v >> 8) > -256;   /* fb^2 terms 0 */
    if (fast) {
        fm2_plain(&car, &mod, &prev, c_inc, m_inc, &amount, pc, n);
    } else {
        for (i = 0; i < n; i++) {
            int32_t amt = ramp_next(&amount), fb = ramp_next(&feedback);
            int32_t pfb = fb < 0 ? (fb * fb) >> 16 : 0;             /* phase feedback, 0.5 fb^2 */
            int32_t mfb = fb > 0 ? (fb * fb) >> 17 : 0;             /* self modulation, 0.25 fb^2 */
            FM_LOOP(1, 1, 1);
        }
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
    } else if (fast) {
        sub += (c_inc >> 1) * 2 * (uint32_t)n;
    }
    f->carrier = car;
    f->modulator = mod;
    f->sub = sub;
    f->prev = prev;
}

/* the controls both rates share: the modulator's increment (at c_inc's rate), the amount and feedback ramps */
static COLD uint32_t fm_controls(struct macro_fm *f, const uint8_t *p, uint32_t inc, uint32_t c_inc, struct ramp *amount,
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

static void fm_render4(struct macro_fm *__restrict f, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux, int n,
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
        else                                        /* OUT: phase feedback or self modulation (or neither) */
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

static void cnoise_render(struct macro_cnoise *__restrict c, uint32_t *__restrict rng, int sync, uint32_t finc, int32_t *__restrict out, int n)
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

static void noise_render(struct macro_noise *__restrict z, uint32_t *__restrict rng, const uint8_t *p, uint32_t inc, int32_t *__restrict out,
                         int32_t *__restrict aux, int n, int want_aux)
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
static COLD struct coef coef_cap(struct coef c)             /* c x (Q15) >> (s - 9) stays a shift of 0..31 */
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

static COLD void particle_coefs(struct macro_particle *q, int32_t lf, struct coef k, int32_t lpre)
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

static void particle_render(struct macro_voice *__restrict m, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux,
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
        i = 0;
        for (;;) {
            int32_t bp, lp, *a = acc + i, e = at < n ? at : n;
            for (; i < e; i++) {                                /* ringing, up to the next impulse */
                SVF_STEP(f, c, 0, bp, lp);
                *a++ += bp;
                (void)lp;
            }
            if (i >= n)
                break;
            /* an impulse */
            s = (sync && fresh) ? 32768 : (int32_t)(rnd32(&m->rng) >> 17);
            if (fresh) {                                        /* the band-pass's frequency for this block */
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
            lp = (q->c1m * s) >> (q->c1s - 9);                  /* Q15 -> Q24 */
            bp += lp;
            f.s1 += 2 * lp;
            lp = (q->c2m * s) >> (q->c2s - 9);
            f.s2 += 2 * lp;
            acc[i] += bp;
            i++;
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

/* ---- shared by the drums ------------------------------------------------------------------------------ */

#define ONE_POLE(X, IN, C) ((X) += fmac1t((IN) - (X), (C)))        /* C: Q31 (truncated: a smoother) */
#define Q31(x) ((int32_t)((x) * 2147483648.0 + 0.5))                 /* a constant 0..1 as Q31 (compile time) */
#define Q24(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))

/* y / (1 + |y|), Q24 in and out: tables (MACRO_RSAT_*), a division past |y| = 16 (rsat_far). Inline: it
 * runs a sample in the drums' loops. */
static COLD int32_t rsat_far(int32_t u)
{
    return 32768 - (int32_t)(0x7fffffffu / (uint32_t)(((1 << 24) + (u > 0x7e000000 ? 0x7e000000 : u)) >> 8));
}

static int32_t rsat24(int32_t y)
{
    int32_t u = iabs(y), r, i, f;
    if (u < (1 << 24)) {
        i = u >> 18;
        f = (u >> 2) & 0xffff;
        r = MACRO_RSAT_FINE[i] + (((MACRO_RSAT_FINE[i + 1] - MACRO_RSAT_FINE[i]) * f) >> 16);
    } else if (u < (16 << 24)) {
        u -= 1 << 24;
        i = u >> 20;
        f = (u >> 4) & 0xffff;
        r = MACRO_RSAT_COARSE[i] + (((MACRO_RSAT_COARSE[i + 1] - MACRO_RSAT_COARSE[i]) * f) >> 16);
    } else {
        r = rsat_far(u);
    }
    r <<= 9;
    return y < 0 ? -r : r;
}

/* stmlib's SoftClip, Q24 (inline: a sample in the drums' loops) */
static inline __attribute__((always_inline)) int32_t softclip24(int32_t x)
{
    int32_t u = iabs(x), r, i, f;
    if (u >= (3 << 24))
        r = 1 << 24;
    else {
        u = fmac1(u, Q31(1.0 / 3.0)) << 7;                  /* x / 3, Q31 */
        i = u >> 24;
        f = (u >> 8) & 0xffff;
        r = (MACRO_SOFTCLIP[i] + (((MACRO_SOFTCLIP[i + 1] - MACRO_SOFTCLIP[i]) * f) >> 16)) << 9;
    }
    return x < 0 ? -r : r;
}

/* A drum's sound has died away when every state that feeds its output is below -96 dB (Q24): from then on
 * until the next trig its output is silence and nothing of it is computed. */
#define TINY(x) ((uint32_t)((x) + 256) < 512u)
/* (a resonator rounding to a fixed point a few hundred LSBs off zero at a low frequency: -72 dB counts) */
#define TINYR(x) ((uint32_t)((x) + 4096) < 8192u)

/* AnalogBassDrum's Diode(): x, or 0.7 x 2x / (1 + |2x|) below 0 (Q24) */
static inline int32_t diode24(int32_t x)
{
    if (x >= 0)
        return x;
    return fmac1(rsat24(x < -(60 << 24) ? -(120 << 24) : 2 * x), Q31(0.7));
}

/* g = tan(pi f) for f in Q31 (<= 0.5), as Q27: FREQUENCY_DIRTY f (pi + 3.736e-1 pi^3 f^2) and FREQUENCY_FAST
 * f (pi + f^2 (3.260e-1 pi^3 + 1.823e-1 pi^5 f^2)) */
static int32_t tan_dirty27(int32_t f31)
{
    return fmac1(f31, 421657428 + fmac1(1554770775, fmac1(f31, f31)));
}

static int32_t tan_fast27(int32_t f31)
{
    int32_t f2 = fmac1(f31, f31);
    int32_t inner26 = 678339498 + (fmac1(f2, 1871914135) << 1);              /* 10.108 + 55.787 f^2, Q26 */
    return fmac1(f31, 421657428 + (fmac1(f2, inner26) << 1));
}

/* 1/x for x >= 1 (Qq, q 16..27) as Q31: a hardware division to 16 bits and a Newton step to 31 */
static int32_t recip_q(int32_t x, int q)
{
    uint32_t rr = (0x7fffffffu / (uint32_t)(x >> (q - 15))) << 15;
    int32_t r = rr > 0x7fffffffu ? 0x7fffffff : (int32_t)rr, e;
    e = (1 << q) - fmac1(x, r);
    e = fmac1(r, e * (1 << (31 - q)));
    return e > 0 && r > 0x7fffffff - e ? 0x7fffffff : r + e;
}
#define recip27(x) recip_q((x), 27)

/* An Svf's coefficients (struct svf_c, k2 left out) from g and g k (Q27): a1 = 1/D to 31 bits (the margin to instability, 2 g k / D, is 4e-5 at the longest
 * decays), a2 = g a1, a3 = g a2. */
static COLD void svf_coefs_gk(int32_t g27, int32_t gk27, int32_t *a1, int32_t *a2, int32_t *a3)
{
    int32_t r = recip_q((1 << 24) + (fmac1(g27, g27) << 1) + (gk27 >> 3), 24);   /* 1 / (1 + g^2 + g k), D < 128 */
    *a1 = r;
    *a2 = fmac1(r, g27) << 4;
    *a3 = fmac1(*a2, g27) << 4;
}

/* k = 1 / (1 + q f) (qf: q f, Q8), Q31 */
static COLD int32_t k_of_qf(int32_t qf8)
{
    int32_t den = 256 + (qf8 < 0 || qf8 > 0x3fffffff ? 0x3fffffff : qf8), z = clz32((uint32_t)den);
    uint32_t kk = 0x7fffffffu / ((uint32_t)(den << (z - 1)) >> 15);
    kk = z >= 8 ? kk << (z - 8) : kk >> (8 - z);
    return kk > 0x7fffffffu ? 0x7fffffff : (int32_t)kk;
}

/* A resonator: FREQUENCY_DIRTY at f (Q31, <= 0.4), q = 1 + q f */
static void reso_coefs(int32_t f31, int32_t qf8, int32_t *a1, int32_t *a2, int32_t *a3)
{
    int32_t g27 = tan_dirty27(f31);
    svf_coefs_gk(g27, fmac1(g27, k_of_qf(qf8)), a1, a2, a3);
}

/* stmlib's OnePole (FREQUENCY_FAST at f): G = g / (1 + g), Q31; lp = s + G (in - s), s = 2 lp - s */
static COLD int32_t onepole_G(int32_t f31)
{
    return 0x7fffffff - recip27((1 << 27) + tan_fast27(f31));
}
#define ONEPOLE_LP(S, G, IN, LP) do {                                                              \
        LP = (S) + fmac1((IN) - (S), (G));                                                         \
        (S) = LP + LP - (S);                                                                       \
    } while (0)

/* ---- BD: plaits/dsp/engine/bass_drum_engine.cc, drums/analog_bass_drum.h, drums/synthetic_bass_drum.h --- *
 * OUT: the analog-style drum (a pulse into a resonator that its own output and an attack pulse bend in
 * pitch, then the overdrive); AUX: the synthetic one (a distorted sine with a pitch envelope, a click and
 * noise). HARM: attack FM, self FM, drive; TIMB: tone; MORP: decay. Accent is Plaits' unpatched 0.8; the
 * drums are triggered (Plaits' patched trigger), never free-running. The resonator's pitch is updated
 * every sample during the attack's pitch sweep (the first 7 ms), then every 8 samples (Plaits: every
 * sample; measured: the decay's level within 0.3 dB); its other arithmetic is Plaits', in Q24. */

static void bd_init(struct macro_bd *d)
{
    int32_t *w = &d->trig, *end = (int32_t *)(d + 1);
    while (w < end)
        *w++ = 0;
}

#ifndef BD_EVERY
#define BD_EVERY 7                                    /* the resonator's pitch: every 8 samples after the attack */
#endif
static void bd_analog(struct macro_bd *__restrict d, int32_t harm, int32_t timb, int32_t morph, uint32_t inc, int32_t *__restrict out,
                      int n)
{
    int32_t f0 = (int32_t)(inc >> 1), lf0 = log2_q16(inc) - 32 * 65536, afm27, sfm31, q8, scale, tone_f, leak, i;
    struct svf_c c;
    /* HARMONICS: attack FM 1.7 min(4h, 1) (Q27), self FM 0.08 clamp(4h - 1, 0, 1) (Q31) */
    afm27 = harm >= 16384 ? Q31(0.85) >> 3 : fmac1(harm << 17, Q31(0.85)) >> 3;
    sfm31 = harm <= 16384 ? 0 : harm >= 32768 ? Q31(0.08) : fmac1((harm - 16384) << 17, Q31(0.08));
    /* q = 1500 x 2^(80 MORPH / 12), x 256 */
    q8 = (int32_t)inc_of_log2(691454 + (morph * 20) / 3 + 8 * 65536);           /* 691454: log2 1500 */
    /* scale = 0.001 / f0, at most 5, Q28 */
    scale = (int32_t)inc_of_log2(28 * 65536 - 653118 - lf0);                  /* 653118: -log2 0.001 */
    if (scale > (5 << 28) || scale <= 0)
        scale = 5 << 28;
    /* tone: min(4 f0 2^(9 TIMBRE), 1), Q31; the exciter's leak 0.08 (TIMBRE + 0.25), Q31 */
    {
        uint32_t t = inc_of_log2(lf0 + 33 * 65536 + 9 * timb);
        tone_f = t > 0x7fffffffu ? 0x7fffffff : (int32_t)t;
    }
    leak = (timb + 16384) * 2621;
    if (d->trig) {
        d->pulse_left = 48;                                 /* 1 ms */
        d->fm_left = 288;                                   /* 6 ms */
        d->lp_out = 0;
    }
    c.a1 = d->a1;
    c.a2 = d->a2;
    c.a3 = d->a3;
    {
        int32_t xin[32], xleak[32], fml[32], active, sweep = 0, fixed = 0;
        int32_t pl = d->pulse_left, fl = d->fm_left, pu = d->pulse, plp = d->pulse_lp, rt = d->retrig, fmlp = d->fm_lp;
        int32_t s1 = d->res.s1, s2 = d->res.s2, lpo = d->lp_out, tl = d->tone_lp, bp, lp;
        /* the trigger pulse, the FM pulse and the retrigger pulse (while they last) */
        active = pl || fl || !TINY(pu) || !TINY(plp) || rt || fmlp > 16;
        if (active && !pl && !fl && TINY(pu) && TINY(plp) && fmlp <= 16) {
            /* only the retrigger pulse's slow tail: the input alone */
            pu = plp = fmlp = 0;
            for (i = 0; i < n; i++) {
                rt = (uint32_t)(rt + 4096) < 8192u ? 0 : fmac1(rt, Q31(1.0 - 1.0 / 2400.0));
                xin[i] = fmac1(-fmac1(rt, Q31(0.2)), scale) << 3;
                xleak[i] = 0;
                fml[i] = 0;
            }
        } else if (active) {
            for (i = 0; i < n; i++) {
                int32_t pulse, fm_pulse = 0;
                if (pl) {
                    pl--;
                    pulse = pl ? Q24(8.6) : Q24(7.6);          /* 3 + 7 accent, accent 0.8 */
                    pu = pulse;
                } else {
                    pu = fmac1t(pu, Q31(1.0 - 1.0 / 9.6));
                    pulse = pu;
                }
                ONE_POLE(plp, pulse, Q31(1.0 / 4.8));
                pulse = diode24(pulse - plp + fmac1(pulse, Q31(0.044)));
                if (fl) {
                    fl--;
                    fm_pulse = 0x7fffff00 >> 7;                 /* 1, Q24 */
                    rt = fl ? 0 : -Q24(0.8);
                } else
                    rt = (uint32_t)(rt + 4096) < 8192u ? 0 : fmac1(rt, Q31(1.0 - 1.0 / 2400.0));
                ONE_POLE(fmlp, fm_pulse, Q31(1.0 / 4.8));
                fml[i] = fmlp;
                xin[i] = fmac1(pulse - fmac1(rt, Q31(0.2)), scale) << 3;
                xin[i] = xin[i] > Q24(100.0) ? Q24(100.0) : xin[i] < -Q24(100.0) ? -Q24(100.0) : xin[i];
                xleak[i] = fmac1(pulse, leak);
            }
            sweep = fmlp > Q24(0.004) || fl;
        } else
            pu = plp = rt = fmlp = 0;
        d->pulse_left = pl;
        d->fm_left = fl;
        d->pulse = pu;
        d->pulse_lp = plp;
        d->retrig = rt;
        d->fm_lp = fmlp;
        /* the resonator's pitch: from the attack FM and the self FM (its own output); without self FM and
         * after the attack, the block's */
        if (!sfm31 && !active) {
            int32_t f27 = f0 >> 4;
            f27 = f27 > (Q31(0.4) >> 4) ? Q31(0.4) >> 4 : f27 < 16 ? 16 : f27;
            reso_coefs(f27 << 4, fmac1(f27 << 4, q8), &c.a1, &c.a2, &c.a3);
            fixed = 1;
        }
        for (i = 0; i < n; i++) {
            if (!fixed && (!(i & BD_EVERY) || (sweep && !(i & 1) && fml[i] > Q24(0.004)))) {
                int32_t lo = lpo > Q24(12.0) ? Q24(12.0) : lpo < -Q24(12.0) ? -Q24(12.0) : lpo;
                int32_t punch = Q24(0.7) + diode24(10 * lo - (1 << 24));
                int32_t m27, f27;
                if (punch > (100 << 24))
                    punch = 100 << 24;
                m27 = (active ? fmac1(fml[i] << 7, afm27) : 0) + (fmac1(punch, sfm31) << 3);
                f27 = (f0 >> 4) + fmac1(f0, m27);
                f27 = f27 > (Q31(0.4) >> 4) ? Q31(0.4) >> 4 : f27 < 16 ? 16 : f27;
                reso_coefs(f27 << 4, fmac1(f27 << 4, q8), &c.a1, &c.a2, &c.a3);
            }
            {
                struct macro_svf f;
                f.s1 = s1;
                f.s2 = s2;
                SVF_STEP(f, c, active ? xin[i] : 0, bp, lp);
                s1 = f.s1;
                s2 = f.s2;
            }
            lpo = lp;
            ONE_POLE(tl, (active ? xleak[i] : 0) + bp, tone_f);
            out[i] = tl;
        }
        d->res.s1 = s1;
        d->res.s2 = s2;
        d->lp_out = lpo;
        d->tone_lp = tl;
    }
    svf_guard(&d->res);
    d->a1 = c.a1;
    d->a2 = c.a2;
    d->a3 = c.a3;
}

/* SyntheticBassDrum's DistortedSine: a triangle bent by t / (1 + |t|), towards a clean sine as
 * dirtiness falls; the phase jittered by the phase noise. Q24 */
static int32_t distorted_sine(uint32_t phase, int32_t pnoise, int32_t dirt31)
{
    int32_t t, tri, sine, clean;
    phase += (uint32_t)(fmac1(pnoise, dirt31) * 256);
    t = (int32_t)(phase < 0x80000000u ? phase : 0u - phase);     /* 0..0.5, Q32 */
    tri = (int32_t)((uint32_t)t >> 6) - (1 << 24);                /* 4 t - 1, Q24 */
    sine = 2 * rsat24(tri);
    clean = sin32(phase + 0xc0000000u) << 9;
    return sine + fmac1(clean - sine, 0x7fffffff - dirt31);
}

static void bd_synthetic(struct macro_bd *__restrict d, uint32_t *__restrict rng, int32_t harm, int32_t timb, int32_t morph, uint32_t inc,
                         int32_t *__restrict aux, int n)
{
    int32_t f0 = (int32_t)(inc >> 1), lf0 = log2_q16(inc) - 32 * 65536, m2, dirt31, fm_amt, fmd, fm_decay, body_decay;
    int32_t tone_f, tone15, i, df;
    uint32_t t;
    m2 = (int32_t)(((uint32_t)morph * (uint32_t)morph) >> 16);               /* decay^2, Q16 */
    /* dirtiness (0.4 - 0.25 MORPH^2) max(1 - 8 f0, 0), Q31 */
    df = 0x7fffffff - (f0 > (0x7fffffff >> 3) ? 0x7fffffff : f0 * 8);
    dirt31 = fmac1((Q31(0.4) >> 0) - (m2 * 8192), df);
    /* the FM envelope: amount min(2h, 1) x 3.5 (Q27), decay max(2h - 1, 0)^2 */
    fm_amt = harm >= 32768 ? Q31(0.4375) : fmac1(harm << 16, Q31(0.4375));   /* 3.5/8: Q28 of 3.5 x amount */
    fmd = harm <= 32768 ? 0 : (harm - 32768) * 2;                             /* Q16 */
    fmd = (int32_t)(((uint32_t)fmd * (uint32_t)fmd) >> 16);
    /* 1 - 1 / (384 (1 + 4 fmd^2)) */
    fm_decay = 0x7fffffff - (int32_t)(0x7fffffffu / (uint32_t)(384 + ((384 * 4 * fmd) >> 16)));
    /* 1 - 2^(-5 MORPH^2) / 960 */
    body_decay = 0x7fffffff - fmac1((int32_t)exp2_q16(-5 * m2) << 14, 4473924);   /* 4473924: 2^32 / 960 */
    t = inc_of_log2(lf0 + 33 * 65536 + 9 * timb);
    tone_f = t > 0x7fffffffu ? 0x7fffffff : (int32_t)t;
    tone15 = timb << 15;                                                      /* transient level, Q31 */
    if (d->trig) {
        d->fm = 0x7fffff00 >> 7;
        d->body = d->trans = Q24(0.86);
        d->body_pw = 48;
        d->fm_pw = 62;
    }
    for (i = 0; i < n; i++) {
        int32_t body, transient, mix, x, c_in, bp, lp;
        struct svf_c ck;
        ONE_POLE(d->pnoise, (int32_t)(rnd32(rng) >> 8) - Q24(0.5), Q31(0.002));
        if (d->fm_pw) {
            d->fm_pw--;
            d->phase = 0x40000000u;
        } else {
            uint32_t step;
            d->fm = fmac1t(d->fm, fm_decay);
            /* min(f0 (1 + 3.5 amount fm_lp), 0.5) */
            step = (uint32_t)(f0 >> 3) + (uint32_t)fmac1(f0, fmac1(d->fm_lp2 << 7, fm_amt));   /* Q28 */
            d->phase += step > 0x08000000u ? 0x80000000u : step << 4;
        }
        if (d->body_pw)
            d->body_pw--;
        else {
            d->body = fmac1t(d->body, body_decay);
            d->trans = fmac1t(d->trans, Q31(1.0 - 1.0 / 240.0));
        }
        ONE_POLE(d->body_lp, d->body, Q31(0.1));
        ONE_POLE(d->trans_lp, d->trans, Q31(0.1));
        ONE_POLE(d->fm_lp2, d->fm, Q31(0.1));
        body = distorted_sine(d->phase, d->pnoise, dirt31);
        /* the click: SLOPE (0.5 up, 0.1 down), a one-pole high-pass (0.04), a low-pass at 5 kHz, q 2 */
        c_in = d->body_pw ? 0 : 0x7fffff00 >> 7;
        x = c_in - d->click_lp;
        d->click_lp += fmac1(x, x > 0 ? Q31(0.5) : Q31(0.1));
        ONE_POLE(d->click_hp, d->click_lp, Q31(0.04));
        ck.a1 = 1671397352;
        ck.a2 = 567202663;
        ck.a3 = 192484965;
        SVF_STEP(d->click, ck, d->click_lp - d->click_hp, bp, lp);
        (void)bp;
        /* the noise: band-limited by two one-poles */
        ONE_POLE(d->noise_lp, (int32_t)(rnd32(rng) >> 8), Q31(0.05));
        ONE_POLE(d->noise_hp, d->noise_lp, Q31(0.005));
        transient = lp + d->noise_lp - d->noise_hp;
        /* TransistorVCA: s = (body - 0.6) gain; 3 s / (2 + |s|) + 0.3 gain */
        x = fmac1(body - Q24(0.6), d->body_lp << 7);
        mix = -(3 * rsat24(x >> 1) + fmac1(d->body_lp, Q31(0.3)));
        mix -= fmac1(fmac1(transient, d->trans_lp << 7), tone15);
        ONE_POLE(d->tone_lp2, mix, tone_f);
        aux[i] = d->tone_lp2;
    }
    svf_guard(&d->click);
}

/* Plaits' Overdrive (on OUT): drive 0.5 + 0.5 max(2h - 1, 0) max(1 - 16 f0, 0) */
static void bd_overdrive(int32_t harm, uint32_t inc, int32_t *__restrict x, int n)
{
    int32_t f0 = (int32_t)(inc >> 1), dv, d2, pa, pb, pre, sq, arg, sc, post, i;
    int32_t lim = f0 > (0x7fffffff >> 4) ? 0 : 0x7fffffff - f0 * 16;
    dv = harm <= 32768 ? 0 : fmac1((harm - 32768) << 9, lim);                 /* Q24 */
    dv = Q24(0.5) + (dv >> 1);
    d2 = fmac1(dv << 7, dv << 7);                                             /* Q31 */
    pa = dv >> 1;                                                             /* Q24 */
    pb = fmac1(fmac1(d2, d2), dv) * 24;                                       /* 24 dv^5, Q24 */
    pre = pa + fmac1(pb - pa, d2);
    sq = fmac1(dv << 7, (Q24(2.0) - dv) << 6) >> 6;                           /* dv (2 - dv), Q24 */
    arg = Q24(0.33) + fmac1(sq << 6, pre - Q24(0.33)) * 2;
    sc = softclip24(arg);
    post = (int32_t)(0x7fffffffu / (uint32_t)(sc >> 7)) << 10;                /* 1/sc, Q24 */
    for (i = 0; i < n; i++) {
        int32_t v = fmac1(x[i], pre << 2);                                    /* pre x, Q19 */
        v = softclip24((v > (7 << 18) ? 7 << 18 : v < -(7 << 18) ? -(7 << 18) : v) << 5);
        x[i] = fmac1(v, post << 5) << 2;
    }
}

static void bd_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                      int want_out, int want_aux)
{
    struct macro_bd *d = &m->e.bd;
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]), i;
    if (d->trig)
        d->idle[0] = d->idle[1] = 0;
    if (want_out) {
        if (d->idle[0])
            for (i = 0; i < n; i++)
                out[i] = 0;
        else {
            bd_analog(d, harm, timb, morph, inc, out, n);
            bd_overdrive(harm, inc, out, n);
            for (i = 0; i < n; i++)
                out[i] >>= 9;                               /* Q15 */
            d->idle[0] = !d->pulse_left && !d->fm_left && TINY(d->pulse) && TINY(d->pulse_lp) && TINY(d->retrig)
                         && TINYR(d->res.s1) && TINYR(d->res.s2) && TINYR(d->tone_lp);
        }
    }
    if (want_aux) {
        if (d->idle[1])
            for (i = 0; i < n; i++)
                aux[i] = 0;
        else {
            bd_synthetic(d, &m->rng, harm, timb, morph, inc, aux, n);
            for (i = 0; i < n; i++)
                aux[i] >>= 9;
            d->idle[1] = !d->body_pw && TINY(d->body) && TINY(d->body_lp) && TINY(d->trans_lp) && TINY(d->tone_lp2);
        }
    }
    d->trig = 0;
}

/* ---- SD: plaits/dsp/engine/snare_drum_engine.cc, drums/analog_snare_drum.h, drums/synthetic_snare_drum.h ---- *
 * OUT: the analog-style snare (a pulse into five resonant modes, 808-like up to TIMBRE 2/3 then more modes,
 * soft-clipped, plus band-passed noise); AUX: the synthetic one (two coupled distorted sines and filtered
 * noise with their own envelopes). HARM: snappy (noise against shell); TIMB: tone (OUT), FM (AUX); MORP:
 * decay. Accent 0.8, triggered. */

static void sd_init(struct macro_sd *d)
{
    int32_t *w = &d->trig, *end = (int32_t *)(d + 1);
    while (w < end)
        *w++ = 0;
    d->key_knobs = -1;
}

static const int32_t sd_ratio28[5] = {268435456, 536870912, 853624750, 1116691497, 1508608262};   /* 1, 2, 3.18, 4.16, 5.62 */

static void sd_analog(struct macro_sd *__restrict d, uint32_t *__restrict rng, int32_t harm, int32_t timb, int32_t morph, uint32_t inc,
                      int32_t *__restrict out, int n)
{
    int32_t f0 = (int32_t)(inc >> 1), dxt, snappy, leak31, gain28[5], one_minus_snappy, ned, i, j, nm;
    /* decay_xt = d (1 + d (d - 1)), Q16 */
    dxt = (int32_t)(((uint32_t)morph * (uint32_t)(65536 + (int32_t)(((uint32_t)morph * (uint32_t)morph) >> 16) - morph)) >> 16);
    if (d->key_inc != inc || d->key_knobs != morph) {                     /* the resonators' coefficients */
        int32_t q8 = (int32_t)inc_of_log2(718654 + (dxt * 7) + 8 * 65536);   /* 2000 x 2^(84 dxt / 12), x 256 */
        int32_t fn;
        d->key_inc = inc;
        d->key_knobs = morph;
        for (j = 0; j < 5; j++) {
            int32_t f = fmac1(f0, sd_ratio28[j]), g27;                     /* Q28 */
            f = f > (Q31(0.499) >> 3) ? Q31(0.499) : f << 3;
            g27 = tan_fast27(f);
            svf_coefs_gk(g27, fmac1(g27, k_of_qf(fmac1(f, j ? q8 >> 2 : q8))), &d->ma1[j], &d->ma2[j], &d->ma3[j]);
        }
        fn = f0 > Q31(0.499) / 16 ? Q31(0.499) : f0 * 16;
        {
            int32_t g27 = tan_fast27(fn);
            svf_coefs_gk(g27, fmac1(g27, k_of_qf(fmac1(fn, 384))), &d->na1, &d->na2, &d->na3);
        }
    }
    /* noise envelope: 1 - 0.0017 x 2^(-MORPH (50 + 10 HARM) / 12) */
    ned = 0x7fffffff - fmac1((int32_t)exp2_q16(-(int32_t)(((uint32_t)(morph >> 4) * (uint32_t)((50 * 65536 + 10 * harm) / 12)) >> 12)) << 14,
                             7301444);                                       /* 0.0017 x 2^32 */
    /* exciter leak: snappy (2 - snappy) 0.1 (the raw HARMONICS) */
    leak31 = fmac1(harm << 15, (131072 - harm) * 1638) * 2;
    /* snappy = clamp(1.1 HARM - 0.05, 0, 1), Q31 */
    {
        int32_t sn = harm + ((harm * 3277) >> 15) - 3277;                        /* Q16 */
        sn = sn < 0 ? 0 : sn > 65535 ? 65535 : sn;
        snappy = sn << 15;
    }
    one_minus_snappy = 0x7fffffff - snappy;
    /* the modes' gains, Q28 */
    if (timb < 43691) {                                                    /* 808-style: two modes */
        int32_t t = (int32_t)(((uint32_t)timb * 3) >> 1);                    /* tone x 1.5, Q16 */
        int32_t u = 65536 - t;
        gain28[0] = (3 << 27) + (int32_t)((((uint32_t)u >> 1) * ((uint32_t)u >> 1) >> 14) * 18432);   /* 1.5 + 4.5 (1 - t)^2 */
        gain28[1] = (t << 13) + (int32_t)(0.15 * 268435456.0);              /* 2 t + 0.15 */
        gain28[2] = gain28[3] = gain28[4] = 0;
        nm = 2;
    } else {
        int32_t t = (timb - 43691) * 3;                                     /* Q16 */
        t = t > 65535 ? 65535 : t;
        gain28[0] = (3 << 27) - (t << 11);                                  /* 1.5 - 0.5 t */
        gain28[1] = (int32_t)(2.15 * 268435456.0) - fmac1(t << 15, (int32_t)(0.7 * 268435456.0) );
        gain28[2] = t << 12;
        t = (int32_t)(((uint32_t)t * (uint32_t)t) >> 16);
        gain28[3] = t << 12;
        t = (int32_t)(((uint32_t)t * (uint32_t)t) >> 16);
        gain28[4] = t << 12;
        nm = 5;
    }
    if (d->trig) {
        d->pulse_left = 48;
        d->noise_env = Q24(2.0);
    }
    /* the excitation (while the trigger pulse and its tail last) */
    {
        int32_t pl = d->pulse_left, pu = d->pulse, plp = d->pulse_lp, active = pl || !TINY(pu) || !TINY(plp);
        int32_t x0[32], x1[32], shell[32];
        for (i = 0; i < n; i++)
            shell[i] = 0;
        if (active) {
            for (i = 0; i < n; i++) {
                int32_t pulse;
                if (pl) {
                    pl--;
                    pulse = pl ? Q24(8.6) : Q24(7.6);
                    pu = pulse;
                } else {
                    pu = fmac1t(pu, Q31(1.0 - 1.0 / 4.8));
                    pulse = pu;
                }
                ONE_POLE(plp, pulse, Q31(0.75));
                x0[i] = pulse - plp + fmac1(pulse, Q31(0.006));
                x1[i] = fmac1(pulse, Q31(0.026));
            }
            d->pulse_left = pl;
            d->pulse = pu;
            d->pulse_lp = plp;
        } else {
            d->pulse = d->pulse_lp = 0;
        }
        /* the shell: each mode over the block (its states and coefficients in registers) */
        for (j = 0; j < nm; j++) {
            struct macro_svf f = d->mode[j];
            struct svf_c c;
            int32_t g = gain28[j], bp, lp;
            const int32_t *ex = j ? x1 : x0;
            c.a1 = d->ma1[j];
            c.a2 = d->ma2[j];
            c.a3 = d->ma3[j];
            if (active)
                for (i = 0; i < n; i++) {
                    SVF_STEP(f, c, ex[i], bp, lp);
                    shell[i] += fmac1(bp + fmac1(ex[i], leak31), g);          /* Q21 */
                }
            else if (!TINY(f.s1) || !TINY(f.s2))
                for (i = 0; i < n; i++) {
                    SVF_STEP(f, c, 0, bp, lp);
                    shell[i] += fmac1(bp, g);
                }
            (void)lp;
            svf_guard(&f);
            d->mode[j] = f;
        }
        for (i = 0; i < n; i++) {
            int32_t s21 = shell[i];
            s21 = s21 > (7 << 20) ? 7 << 20 : s21 < -(7 << 20) ? -(7 << 20) : s21;
            out[i] = fmac1(softclip24(s21 << 3), one_minus_snappy);
        }
    }
    /* the noise: 2u - 1 kept above 0, the envelope, snappy x 2, a band-pass at 16 f0 (none without snappy) */
    if (snappy || !TINY(d->nf.s1) || !TINY(d->nf.s2)) {
        struct macro_svf f = d->nf;
        struct svf_c c;
        int32_t env = d->noise_env, bp, lp;
        uint32_t r = *rng;
        c.a1 = d->na1;
        c.a2 = d->na2;
        c.a3 = d->na3;
        for (i = 0; i < n; i++) {
            int32_t noise;
            r = r * 1664525u + 1013904223u;
            noise = (int32_t)(r >> 7) - (1 << 24);
            if (noise < 0)
                noise = 0;
            env = fmac1t(env, ned);
            noise = fmac1(fmac1(noise, env << 5) << 2, snappy) << 1;   /* x the envelope (Q29: up to 2) x snappy x 2 */
            SVF_STEP(f, c, noise, bp, lp);
            out[i] += bp;
        }
        (void)lp;
        *rng = r;
        d->noise_env = env;
        svf_guard(&f);
        d->nf = f;
    } else
        for (i = 0; i < n; i++)
            d->noise_env = fmac1t(d->noise_env, ned);
}

/* SyntheticSnareDrum's DistortedSine: t = 4 p - 1.3 (p mirrored past 0.5), 2 t / (1 + |t|); p Q28 */
static inline int32_t sd_dsine(int32_t p28)
{
    int32_t t = (p28 < (1 << 27) ? p28 : (1 << 28) - p28) >> 4;            /* Q24 */
    return 2 * rsat24(4 * t - Q24(1.3));
}

static void sd_synthetic(struct macro_sd *__restrict d, uint32_t *__restrict rng, int32_t harm, int32_t timb, int32_t morph, uint32_t inc,
                         int32_t *__restrict aux, int n)
{
    int32_t f0 = (int32_t)(inc >> 1), dxt, fm2, drum_decay, snare_decay, sn, drum_level, snare_level, rna, i;
    int32_t g_hp, g_dlp, f, fmin, fmax, a1, a2, a3, k31, f28, fm4;
    struct svf_c c;
    dxt = (int32_t)(((uint32_t)morph * (uint32_t)(65536 + (int32_t)(((uint32_t)morph * (uint32_t)morph) >> 16) - morph)) >> 16);
    fm2 = (int32_t)(((uint32_t)timb * (uint32_t)timb) >> 16);               /* fm_amount^2, Q16 */
    /* drum: 1 - 2^((-72 dxt - 12 fm^2 + 7 HARM) / 12) / 720; snare: 1 - 2^((-60 MORPH - 7 HARM) / 12) / 480 */
    drum_decay = 0x7fffffff - fmac1((int32_t)exp2_q16(-6 * dxt - fm2 + (7 * harm) / 12) << 13, 11930465); /* 2^33/720 */
    snare_decay = 0x7fffffff - fmac1((int32_t)exp2_q16(-5 * morph - (7 * harm) / 12) << 14, 8947849);     /* 2^32/480 */
    sn = harm + ((harm * 3277) >> 15) - 3277;
    sn = sn < 0 ? 0 : sn > 65535 ? 65535 : sn;                             /* snappy, Q16 */
    drum_level = sn >= 65535 ? 0 : (int32_t)exp2_q16((log2_q16((uint32_t)(65536 - sn)) - 16 * 65536) >> 1) << 15;
    snare_level = sn <= 0 ? 0 : (int32_t)exp2_q16((log2_q16((uint32_t)sn) - 16 * 65536) >> 1) << 15;
    if (drum_level < 0)
        drum_level = 0x7fffffff;
    if (snare_level < 0)
        snare_level = 0x7fffffff;
    /* the oscillators' reset noise: ((0.125 - f0) 8)^2 clamped, x fm^2 */
    rna = f0 >= Q31(0.125) ? 0 : (Q31(0.125) - f0) * 8;
    rna = fmac1(fmac1(rna, rna), fm2 << 15);                               /* Q31 */
    /* filters */
    fmin = f0 > Q31(0.05) ? Q31(0.5) : f0 * 10;
    fmax = f0 > Q31(0.5 / 35) ? Q31(0.5) : f0 * 35;
    g_hp = onepole_G(fmin);
    f = f0 > Q31(0.5 / 3) ? Q31(0.5) : f0 * 3;
    g_dlp = onepole_G(f);
    {
        int32_t g27 = tan_fast27(fmax);
        /* k = 1 / q, q = 0.5 + 2 snappy: g k = 2 g / (1 + 4 snappy) */
        k31 = recip27((1 << 27) + (sn << 13));
        svf_coefs_gk(g27, fmac1(g27, k31) << 1, &a1, &a2, &a3);
    }
    c.a1 = a1;
    c.a2 = a2;
    c.a3 = a3;
    fm4 = fm2 << 13;                                                       /* 4 fm^2, Q27 */
    if (d->trig) {
        d->snare_amp = d->drum_amp = Q24(0.86);
        d->fm = 0x7fffff00 >> 7;
        d->ph0 = d->ph1 = 0;
        d->hold = 1920 + ((1440 * morph) >> 16);
    }
    for (i = 0; i < n; i++) {
        int32_t rn, drum, noise, snare, lp, bp;
        if (d->drum_amp > Q24(0.03) || !((n - 1 - i) & 1))
            d->drum_amp = fmac1t(d->drum_amp, drum_decay);
        if (d->hold)
            d->hold--;
        else
            d->snare_amp = fmac1t(d->snare_amp, snare_decay);
        d->fm = fmac1t(d->fm, Q31(1.0 - 1.0 / 336.0));
        rn = (d->ph0 > (1 << 27) ? -1 : 1) + (d->ph1 > (1 << 27) ? -1 : 1);
        rn = fmac1(rn << 28, fmac1(rna, Q31(0.025)));                      /* Q28 */
        f28 = (f0 >> 3) + (fmac1(f0, fmac1(d->fm << 7, fm4)) << 1);        /* f0 (1 + 4 fm^2 fm), Q28 */
        d->ph0 += f28;
        d->ph1 += fmac1(f28, Q31(0.735)) * 2;
        if (rna > Q31(0.1)) {
            if (d->ph0 >= (1 << 28) + rn)
                d->ph0 = (1 << 28) - d->ph0;
            if (d->ph1 >= (1 << 28) + rn)
                d->ph1 = (1 << 28) - d->ph1;
        } else {
            if (d->ph0 >= (1 << 28))
                d->ph0 -= 1 << 28;
            if (d->ph1 >= (1 << 28))
                d->ph1 -= 1 << 28;
        }
        /* (past a step of 1 a sample, at the top notes with FM, Plaits' phases run away; kept here in -1..2) */
        d->ph0 = d->ph0 > (2 << 28) ? d->ph0 - (2 << 28) : d->ph0 < -(1 << 28) ? -(1 << 28) : d->ph0;
        d->ph1 = d->ph1 > (2 << 28) ? d->ph1 - (2 << 28) : d->ph1 < -(1 << 28) ? -(1 << 28) : d->ph1;
        drum = -Q24(0.1) + fmac1(sd_dsine(d->ph0), Q31(0.6)) + fmac1(sd_dsine(d->ph1), Q31(0.25));
        drum = fmac1(fmac1(drum, d->drum_amp << 7), drum_level);
        ONEPOLE_LP(d->drum_lp, g_dlp, drum, drum);
        noise = (int32_t)(rnd32(rng) >> 8);
        SVF_STEP(d->snare_lp, c, noise, bp, lp);
        (void)bp;
        ONEPOLE_LP(d->snare_hp, g_hp, lp, snare);
        snare = lp - snare;                                                 /* the high-pass */
        snare = fmac1(fmac1(snare + Q24(0.1), (d->snare_amp + d->fm) << 6) << 1, snare_level);
        aux[i] = snare + drum;
    }
    svf_guard(&d->snare_lp);
}

static void sd_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int32_t *out, int32_t *aux, int n,
                      int want_out, int want_aux)
{
    struct macro_sd *d = &m->e.sd;
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]), i;
    if (d->trig)
        d->idle[0] = d->idle[1] = 0;
    if (want_out) {
        if (d->idle[0])
            for (i = 0; i < n; i++)
                out[i] = 0;
        else {
            int j, q = 1;
            sd_analog(d, &m->rng, harm, timb, morph, inc, out, n);
            for (i = 0; i < n; i++)
                out[i] >>= 9;
            for (j = 0; j < 5; j++)
                q &= TINYR(d->mode[j].s1) && TINYR(d->mode[j].s2);
            d->idle[0] = q && !d->pulse_left && TINY(d->pulse) && TINY(d->pulse_lp) && TINY(d->noise_env)
                         && TINY(d->nf.s1) && TINY(d->nf.s2);
        }
    }
    if (want_aux) {
        if (d->idle[1])
            for (i = 0; i < n; i++)
                aux[i] = 0;
        else {
            sd_synthetic(d, &m->rng, harm, timb, morph, inc, aux, n);
            for (i = 0; i < n; i++)
                aux[i] >>= 9;
            d->idle[1] = !d->hold && TINY(d->drum_amp) && TINY(d->snare_amp) && TINY(d->fm) && TINY(d->drum_lp);
        }
    }
    d->trig = 0;
}

/* ---- HH: plaits/dsp/engine/hi_hat_engine.cc, drums/hi_hat.h ------------------------------------------- *
 * OUT: 808-style metallic noise (six square oscillators), a resonant band-pass, clocked noise mixed in by
 * HARMONICS, a "swing" VCA and a high-pass; AUX: three ring-modulated square x saw pairs, a band-pass, a
 * linear VCA with a two-stage envelope, a high-pass. TIMB: tone (the filters' frequency); MORP: decay. */

static void hh_init(struct macro_hh *d)
{
    int32_t *w = &d->trig, *end = (int32_t *)(d + 1), i;
    while (w < end)
        *w++ = 0;
    for (i = 0; i < 6; i++) {
        d->ph[i] = 0x80000000u;                             /* Oscillator::Init: phase 0.5, high */
        d->high[i] = 1;
    }
    d->key_timb = -1;
}

static const uint32_t hh_sq_ratio28[6] = {268435456, 350039835, 393526378, 479694160, 518617301, 680752316};
static const uint32_t hh_rm_inc[6] = {17895697, 673772995, 45634028, 722538769, 65319294, 939524096};

/* Plaits' Oscillator, SQUARE (pw 0.5) or SAW, one sample, Q15 (band-limited steps: polyBLEP) */
static inline int32_t blep_q15(int32_t t)                    /* 0.5 t^2, t Q16 */
{
    return ((t >> 1) * (t >> 1)) >> 16;
}

static void hh_render(struct macro_voice *__restrict m, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux, int n,
                      int want_out, int want_aux)
{
    struct macro_hh *d = &m->e.hh;
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]);
    int32_t f0 = (int32_t)(inc >> 1), noisiness, nf25, env_decay, cut_decay, i, j, h;
    uint32_t cutoff, nclk_inc;
    struct svf_c kbp, khp;
    noisiness = (int32_t)(((uint32_t)harm * (uint32_t)harm) >> 16) << 15;   /* HARMONICS^2, Q31 */
    /* 1 - 0.003 x 2^(-7 MORPH), 1 - 0.0025 x 2^(-3 MORPH) */
    env_decay = 0x7fffffff - fmac1((int32_t)exp2_q16(-7 * morph) << 14, 12884902);
    cut_decay = 0x7fffffff - fmac1((int32_t)exp2_q16(-3 * morph) << 14, 10737418);
    /* the filters at 150 Hz x 2^(6 TIMBRE), 16 kHz at most */
    if (d->key_timb != timb) {                             /* (computed again only when TIMBRE moves) */
        struct svf_c k;
        cutoff = inc_of_log2(1551766 + 6 * timb);
        if (cutoff > 0x55555555u)
            cutoff = 0x55555555u;
        svf_coefs(&k, cutoff, coef_fit(coef_norm((uint32_t)recip_q((3 << 24) + timb * 768, 24), 31)));   /* q 3 + 3 TIMBRE */
        d->kc[0][0] = k.a1; d->kc[0][1] = k.a2; d->kc[0][2] = k.a3; d->kc[0][3] = k.k2;
        svf_coefs(&k, cutoff, coef_norm(1, 0));                                                          /* q 1 */
        d->kc[1][0] = k.a1; d->kc[1][1] = k.a2; d->kc[1][2] = k.a3; d->kc[1][3] = k.k2;
        svf_coefs(&k, cutoff, coef_norm(2, 0));                                                          /* q 0.5 */
        d->kc[2][0] = k.a1; d->kc[2][1] = k.a2; d->kc[2][2] = k.a3; d->kc[2][3] = k.k2;
        d->key_timb = timb;
    }
    khp.a1 = d->kc[2][0]; khp.a2 = d->kc[2][1]; khp.a3 = d->kc[2][2]; khp.k2 = d->kc[2][3];
    /* the clocked noise: f0 (32 - 16 HARMONICS^2), at most 0.5 */
    {
        nf25 = fmac1(f0, (32 << 25) - ((noisiness >> 15) << 13));           /* Q25 */
        nclk_inc = nf25 >= (1 << 24) ? 0x80000000u : (uint32_t)nf25 << 7;
    }
    for (h = 0; h < 2; h++) {
        int32_t *x = h ? aux : out;
        if (!(h ? want_aux : want_out))
            continue;
        if (d->trig)
            d->idle[h] = 0;
        if (d->idle[h]) {
            for (i = 0; i < n; i++)
                x[i] = 0;
            continue;
        }
        if (d->trig)                                       /* (1.5 + 0.5 (1 - MORPH)) x 0.86 */
            d->env[h] = Q24(0.86 * 2.0) - (fmac1(morph << 15, Q31(0.43)) >> 7);
        if (h == 0) {                                      /* SquareNoise at 2 f0 */
            uint32_t sinc[6];
            for (j = 0; j < 6; j++) {
                uint32_t f = (uint32_t)fmac1(f0, (int32_t)hh_sq_ratio28[j]);       /* Q28 of f0 r */
                sinc[j] = f >= (Q31(0.499) >> 4) ? (uint32_t)Q31(0.499) << 1 : f << 5;  /* 2 f0 r, Q32 */
            }
            if ((sinc[0] | sinc[1] | sinc[2] | sinc[3] | sinc[4] | sinc[5]) < (1u << 26)) {
                /* Up to ~700 Hz the squares turn over at most twice a block each: the count of squares up
                 * changes only there, so it is built from those turns (a division each) rather than six
                 * phases a sample. The same samples as the loop below. */
                int32_t dc[32], c = 0;
                for (i = 0; i < n; i++)
                    dc[i] = 0;
                for (j = 0; j < 6; j++) {
                    uint32_t q = d->sq[j], si = sinc[j];
                    int m = 0;
                    c += (int32_t)(q >> 31);
                    if (si)
                        for (;;) {
                            uint32_t dist = (q & 0x80000000u) ? 0u - q : 0x80000000u - q;   /* to the next half */
                            uint32_t k;
                            if (dist > (uint32_t)(n - m) * si)
                                break;
                            k = (dist - 1) / si + 1;          /* steps until the top bit turns */
                            m += (int)k;
                            dc[m - 1] += (q & 0x80000000u) ? -1 : 1;
                            q += k * si;
                        }
                    d->sq[j] += (uint32_t)n * si;
                }
                for (i = 0; i < n; i++) {
                    c += dc[i];
                    x[i] = c * Q24(0.33) - Q24(1.0);
                }
            } else {
                uint32_t p0 = d->sq[0], p1 = d->sq[1], p2 = d->sq[2], p3 = d->sq[3], p4 = d->sq[4], p5 = d->sq[5];
                for (i = 0; i < n; i++) {
                    int32_t c;
                    p0 += sinc[0];
                    p1 += sinc[1];
                    p2 += sinc[2];
                    p3 += sinc[3];
                    p4 += sinc[4];
                    p5 += sinc[5];
                    c = (int32_t)((p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31));
                    x[i] = c * Q24(0.33) - Q24(1.0);
                }
                d->sq[0] = p0;
                d->sq[1] = p1;
                d->sq[2] = p2;
                d->sq[3] = p3;
                d->sq[4] = p4;
                d->sq[5] = p5;
            }
            kbp.a1 = d->kc[0][0]; kbp.a2 = d->kc[0][1]; kbp.a3 = d->kc[0][2]; kbp.k2 = d->kc[0][3];
        } else {                                           /* RingModNoise at 2 f0: ratio 2f0 / (0.01 + 2f0) */
            /* 1 - 0.01 / (0.01 + 2 f0) = 1 - 1 / (1 + 200 f0) */
            int32_t ratio = 0x7fffffff - recip_q((1 << 24) + (f0 >> 7) * 200, 24);
            uint32_t rinc[6];
            for (j = 0; j < 6; j++) {
                uint32_t f = (uint32_t)fmac1((int32_t)(hh_rm_inc[j] >> 1), ratio) << 1;
                if (f > 0x40000000u)
                    f = 0x40000000u;                       /* kMaxFrequency 0.25 */
                rinc[j] = f;
            }
            for (i = 0; i < n; i++)
                x[i] = 0;
            for (j = 0; j < 6; j += 2) {                   /* a pair over the block: square x saw */
                uint32_t pa = d->ph[j], pb = d->ph[j + 1], ia = rinc[j], ib = rinc[j + 1];
                int32_t na = d->next[j], nb = d->next[j + 1], hi = d->high[j];
                for (i = 0; i < n; i++) {
                    int32_t ta = na, tb = nb, above, t;
                    uint32_t old = pa;
                    na = 0;
                    pa += ia;
                    above = pa < old || pa >= 0x80000000u;
                    if (hi ^ above) {                      /* the square up */
                        t = sub_sample(pa - 0x80000000u, ia);
                        ta += blep_q15(t);
                        na -= blep_q15(65536 - t);
                        hi = above;
                    }
                    if (pa < old) {                        /* and down: a new cycle */
                        t = sub_sample(pa, ia);
                        ta -= blep_q15(t);
                        na += blep_q15(65536 - t);
                        hi = 0;
                    }
                    na += pa >= 0x80000000u ? 32768 : 0;
                    old = pb;
                    nb = 0;
                    pb += ib;
                    if (pb < old) {                        /* the saw's reset */
                        t = sub_sample(pb, ib);
                        tb -= blep_q15(t);
                        nb += blep_q15(65536 - t);
                    }
                    nb += (int32_t)(pb >> 17);
                    x[i] += ((2 * ta - 32768) * (2 * tb - 32768)) >> 6;    /* Q30 -> Q24 */
                }
                d->ph[j] = pa;
                d->ph[j + 1] = pb;
                d->next[j] = na;
                d->next[j + 1] = nb;
                d->high[j] = hi;
            }
            kbp.a1 = d->kc[1][0]; kbp.a2 = d->kc[1][1]; kbp.a3 = d->kc[1][2]; kbp.k2 = d->kc[1][3];
        }
        {
            struct macro_svf fb = d->bp[h], fh = d->hp[h];
            uint32_t clk = d->nclk[h], r = m->rng;
            int32_t smp = d->nsmp[h], env = d->env[h], bp, lp, s;
            for (i = 0; i < n; i++) {
                SVF_STEP(fb, kbp, x[i], bp, lp);
                if (noisiness) {                           /* the clocked noise, mixed in by HARMONICS^2 */
                    uint32_t old = clk;
                    clk += nclk_inc;
                    if (clk < old) {
                        r = r * 1664525u + 1013904223u;
                        smp = (int32_t)(r >> 8) - Q24(0.5);
                    }
                    s = bp + fmac1(smp - bp, noisiness);
                } else
                    s = bp;
                if (h == 0) {                              /* SwingVCA, one-stage envelope */
                    env = fmac1t(env, env_decay);
                    s = s > 0 ? (s > Q24(30.0) ? Q24(120.0) : s * 4) : fmac1(s, Q31(0.1));
                    s = fmac1(rsat24(s) + Q24(0.1), env << 6) << 1;
                } else {                                   /* LinearVCA, two stages */
                    env = fmac1t(env, env > Q24(0.5) ? env_decay : cut_decay);
                    s = fmac1(s, env << 6) << 1;
                }
                SVF_STEP(fh, khp, s, bp, lp);
                x[i] = SVF_HP(khp, s, bp, lp) >> 9;        /* Q15 */
            }
            d->bp[h] = fb;
            d->hp[h] = fh;
            d->nclk[h] = clk;
            d->nsmp[h] = smp;
            d->env[h] = env;
            m->rng = r;
        }
        svf_guard(&d->bp[h]);
        svf_guard(&d->hp[h]);
        d->idle[h] = TINY(d->env[h]) && TINY(d->hp[h].s1) && TINY(d->hp[h].s2);
    }
    d->trig = 0;
}

/* ---- GRAIN: plaits/dsp/engine/grain_engine.cc, oscillator/grainlet_oscillator.h, z_oscillator.h ------ *
 * OUT: two "grainlets" (a sine formant hard-synced to a shaped carrier), the second's formant HARMONICS
 * away (-2..+2 octaves), summed, DC-blocked; AUX: the Z oscillator (a formant with a sine discontinuity).
 * TIMB: formant frequency; MORP: carrier shape; HARM: formant ratio and carrier bleed (OUT), the
 * discontinuity's shape (AUX). Controls are the block's (Plaits glides them over the block). */

static void grain_init(struct macro_grain *z)
{
    int32_t *w = (int32_t *)z, *end = (int32_t *)(z + 1);
    while (w < end)
        *w++ = 0;
}

/* a sine of a Q32 phase, Q24 */
static inline int32_t sine24(uint32_t ph)
{
    return sin32(ph) << 9;
}

struct carrier_c { int32_t seg, m27, bp31, a22, b31; };

/* GrainletOscillator::Carrier for a block's shape: the warped phase's parameters */
static void carrier_coefs(struct carrier_c *c, int32_t shape16)
{
    int32_t s3 = shape16 * 3, fr = s3 & 0xffff, t = 65536 - fr, t3;
    c->m27 = c->bp31 = c->a22 = c->b31 = 0;
    c->seg = s3 >> 16;
    if (c->seg >= 2)
        t = fr;                                             /* (segment 2: t = 1 - t) */
    t = t > 65535 ? 65535 : t;
    t3 = (int32_t)(((((uint32_t)t * (uint32_t)t) >> 16) * (uint32_t)t) >> 16);   /* Q16 */
    if (c->seg == 0)
        c->m27 = (1 << 27) + t3 * 15 * 2048;                /* 1 + 15 t^3 */
    else if (c->seg == 1) {
        int32_t bp = Q31(0.001) + fmac1(t3 << 15, Q31(0.499));
        c->bp31 = bp;
        {
            uint32_t q = (0x7fffffffu / ((uint32_t)bp >> 11)) << 10;           /* 0.5 / bp, Q22 */
            c->a22 = q > 0x7fffffffu ? 0x7fffffff : (int32_t)q;
        }
        c->b31 = (int32_t)(0x40000000u / ((uint32_t)(0x80000000u - (uint32_t)bp) >> 16)) << 15;   /* 0.5 / (1 - bp) */
        if (c->b31 < 0)
            c->b31 = 0x7fffffff;
    } else
        c->m27 = (1 << 26) + t3 * 29 * 1024;                /* 0.5 + 14.5 t^3 */
}

/* the carrier, Q24: (Sine(warped phase) + 1) / 4; p: Q31, 0..1 inclusive */
static inline int32_t carrier24(const struct carrier_c *c, uint32_t p)
{
    uint32_t w;
    if (c->seg == 0) {
        int32_t v = fmac1((int32_t)(p >> 1), c->m27);       /* Q26 */
        w = (v >= (1 << 26) ? 0x80000000u : (uint32_t)v << 5) * 2 + 0xc0000000u;
    } else if (c->seg == 1) {
        if (p < (uint32_t)c->bp31)
            w = (uint32_t)fmac1((int32_t)p, c->a22) << 10;                        /* p 0.5 / bp, Q32 */
        else
            w = 0x80000000u + ((uint32_t)fmac1((int32_t)(p - (uint32_t)c->bp31), c->b31) << 1);
        w += 0xc0000000u;
    } else {
        int32_t v = fmac1((int32_t)(p >> 1), c->m27);       /* Q26 */
        w = v >= (1 << 25) ? 0xc0000000u : 0x40000000u + ((uint32_t)v << 6);
    }
    return (sine24(w) + (1 << 24)) >> 2;
}

static void grain_render(struct macro_grain *__restrict z, const uint8_t *p, uint32_t inc, int32_t *__restrict out, int32_t *__restrict aux, int n,
                         int want_out, int want_aux)
{
    int32_t harm = k16(p[MACRO_P_HARM]), timb = k16(p[MACRO_P_TIMB]), morph = k16(p[MACRO_P_MORPH]), i, j;
    int32_t lf0 = log2_q16(inc), G;
    uint32_t c_inc = inc > 0x20000000u ? 0x20000000u : inc;      /* the carrier: at most 0.125 */
    {
        uint32_t f = inc >> 1;                                     /* 0.3 f0: the DC blockers */
        G = 0x7fffffff - recip27((1 << 27) + tan_dirty27(fmac1((int32_t)f, Q31(0.3))));
    }
    if (want_out) {
        struct carrier_c cc;
        int32_t bleed, inv, shape, lim;
        uint32_t f_inc[2];
        /* f1 = NoteToFrequency(24 + 84 TIMBRE); the second x 2^((48 HARMONICS - 24) / 12) */
        f_inc[0] = inc_of_log2(1653513 + ((84 * timb - 45 * 65536) / 12));
        f_inc[1] = inc_of_log2(1653513 + ((84 * timb - 45 * 65536) / 12) + 4 * harm - 2 * 65536);
        for (j = 0; j < 2; j++)
            if (f_inc[j] > 0x40000000u)
                f_inc[j] = 0x40000000u;
        /* bleed: cb (2 - cb), cb = 1 - 2 HARMONICS below the middle; the grainlet's 1 / (1 + bleed) */
        bleed = harm < 32768 ? (32768 - harm) * 2 - (harm == 0) : 0;          /* cb, Q16 (65535 at most) */
        bleed = (int32_t)(((uint32_t)bleed * (uint32_t)(131072 - bleed)) >> 16) << 8;   /* Q24 */
        inv = recip_q((1 << 24) + bleed, 24);
        /* shape 0.33 + (MORPH - 0.33) max(1 - 24 f0, 0) */
        lim = inc >= 0x0aaaaaaau ? 0 : 65536 - (int32_t)((inc >> 12) * 24 >> 4);
        shape = 21627 + (((morph - 21627) * (lim >> 1)) >> 15);
        carrier_coefs(&cc, shape < 0 ? 0 : shape > 65535 ? 65535 : shape);
        for (i = 0; i < n; i++)
            out[i] = 0;
        for (j = 0; j < 2; j++) {
            uint32_t fi = f_inc[j], gc = z->gc[j], gf = z->gf[j];   /* (in locals: registers) */
            int32_t gn = z->gnext[j];
            for (i = 0; i < n; i++) {
                int32_t this_s = gn, next = 0, g;
                uint32_t old = gc;
                gc += c_inc;
                if (gc < old) {                                  /* the carrier restarts: the formant too */
                    int32_t rt = sub_sample(gc, c_inc), before, after, disc;
                    before = fmac1(carrier24(&cc, 0x80000000u) << 7,
                                   sine24(gf + mul_inc(fi, (uint32_t)(65536 - rt))) + bleed);
                    after = fmac1(carrier24(&cc, 0) << 7, bleed);
                    disc = fmac1(after - before, inv);
                    this_s += fmac1(disc, blep_q15(rt) << 16);
                    next -= fmac1(disc, blep_q15(65536 - rt) << 16);
                    gf = mul_inc(fi, (uint32_t)rt);
                } else
                    gf += fi;
                g = fmac1(carrier24(&cc, gc >> 1) << 7, sine24(gf) + bleed);
                next += fmac1(g, inv);
                gn = next;
                out[i] += this_s;
            }
            z->gc[j] = gc;
            z->gf[j] = gf;
            z->gnext[j] = gn;
        }
        for (i = 0; i < n; i++) {                               /* the DC blocker (a one-pole high-pass) */
            int32_t lp;
            ONEPOLE_LP(z->dc[0], G, out[i], lp);
            out[i] = (out[i] - lp) >> 9;
        }
    }
    if (want_aux) {
        /* the formant: NoteToFrequency(note + 96 TIMBRE), at most 0.25; the shape MORPH; the mode HARMONICS */
        uint32_t fi = inc_of_log2(lf0 + 8 * timb), ps;
        int32_t offset, s2 = 0, lowshape = morph < 32768, i2;
        if (fi > 0x40000000u)
            fi = 0x40000000u;
        if (harm < 21823) {                                     /* 0.333 */
            offset = 1 << 24;
            ps = 0x40000000u + (uint32_t)harm * 98304u;          /* 0.25 + 1.5 mode */
        } else {
            ps = 0xbfdf3b64u - (uint32_t)(harm - 21627) * 49152u;   /* 0.7495 - 0.75 (mode - 0.33) */
            offset = harm < 43647 ? -sine24(ps) : Q24(0.001);
        }
        if (lowshape)
            s2 = morph << 16;                                     /* 2 shape, Q31 */
#define ZFN(C, D, F, R) do {                                                                       \
            int32_t rd_ = (sine24(((uint32_t)(D)) + 0x40000000u) + (1 << 24)) >> 1;               \
            int32_t ct_;                                                                           \
            if (lowshape) {                                                                        \
                if ((C) >= 0x40000000u)                                                            \
                    rd_ = fmac1(rd_, s2);                                                          \
                ct_ = (1 << 24) + fmac1(sine24(((uint32_t)(C) << 1) + 0x40000000u) - (1 << 24), s2); \
            } else                                                                                 \
                ct_ = sine24(((uint32_t)(C) << 1) + ((uint32_t)morph << 15));                     \
            R = fmac1((fmac1(rd_ << 7, offset + sine24((F) + ps)) - offset), ct_ << 6) << 1;       \
        } while (0)
        for (i = 0; i < n; i++) {
            int32_t this_s = z->znext, next = 0, v;
            uint32_t zi = c_inc >> 1;                             /* f0, Q31 */
            z->zd += c_inc;                                       /* 2 f0 */
            z->zc += zi;
            if (z->zd >= 0x80000000u) {
                int32_t rt, before, after, disc;
                uint32_t cb, ca;
                z->zd -= 0x80000000u;
                rt = sub_sample(z->zd, c_inc);
                cb = z->zc >= 0x80000000u ? 0x80000000u : 0x40000000u;
                ca = z->zc >= 0x80000000u ? 0u : 0x40000000u;
                ZFN(cb, 0x80000000u, z->zf + mul_inc(fi, (uint32_t)(65536 - rt)), before);
                ZFN(ca, 0u, 0u, after);
                disc = after - before;
                this_s += fmac1(disc, blep_q15(rt) << 16);
                next -= fmac1(disc, blep_q15(65536 - rt) << 16);
                z->zf = mul_inc(fi, (uint32_t)rt);
                if (z->zc > 0x80000000u)
                    z->zc = z->zd >> 1;
            } else
                z->zf += fi;
            if (z->zc >= 0x80000000u)
                z->zc -= 0x80000000u;
            ZFN(z->zc, z->zd, z->zf, v);
            next += v;
            z->znext = next;
            aux[i] = this_s;
        }
#undef ZFN
        for (i2 = 0; i2 < n; i2++) {
            int32_t lp;
            ONEPOLE_LP(z->dc[1], G, aux[i2], lp);
            aux[i2] = (aux[i2] - lp) >> 9;
        }
    }
}

/* ---- the machine ---------------------------------------------------------------------------------- */

/* the gains Plaits' voice gives each engine's OUT and AUX (voice.cc, RegisterInstance), Q15. An engine
 * Plaits registers with a negative gain goes through its limiter (limit()) and then 0.8. */
static const int16_t gain_out[MACRO_ENGINES] = {22938, 19661, 26214, 26214, 26214, 26214, 26214, 22938};  /* WSH .7, FM .6, NOISE, PART lim; drums .8; GRAIN .7 */
static const int16_t gain_aux[MACRO_ENGINES] = {19661, 19661, 26214, 32767, 26214, 26214, 26214, 19661};  /* WSH .6, FM .6, NOISE lim, PART 1; drums .8; GRAIN .6 */

const char *const macro_engine_name[MACRO_ENGINES] = {"WSHAPE", "2OP FM", "NOISE", "PARTCL", "BDRUM", "SNARE", "HIHAT", "GRAIN"};

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
    case MACRO_BD:    bd_init(&m->e.bd); break;
    case MACRO_SD:    sd_init(&m->e.sd); break;
    case MACRO_HH:    hh_init(&m->e.hh); break;
    case MACRO_GRAIN: grain_init(&m->e.grain); break;
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

#define CL2(v) ((v) > 65535 ? 65535 : (v) < -65535 ? -65535 : (v))     /* +-2 in Q15: clipped below anyway */

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
        if (m->engine == MACRO_BD)
            m->e.bd.trig = 1;
        if (m->engine == MACRO_SD)
            m->e.sd.trig = 1;
        if (m->engine == MACRO_HH)
            m->e.hh.trig = 1;
    }
    if (inc > INC_MAX)
        inc = INC_MAX;
    if (inc < INC_MIN)
        inc = INC_MIN;
    /* F: OUT up to 55, AUX from 72, a crossfade between (only there are both outputs computed) */
    mix = p[MACRO_P_AUX] <= 55 ? 0 : p[MACRO_P_AUX] >= 72 ? 32767 : (p[MACRO_P_AUX] - 55) * 1927;
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
    case MACRO_BD:
        bd_render(m, p, inc, o, a, n, mix < 32767, mix > 0);
        break;
    case MACRO_SD:
        sd_render(m, p, inc, o, a, n, mix < 32767, mix > 0);
        break;
    case MACRO_HH:
        hh_render(m, p, inc, o, a, n, mix < 32767, mix > 0);
        break;
    case MACRO_GRAIN:
        grain_render(&m->e.grain, p, inc, o, a, n, mix < 32767, mix > 0);
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
    if (mix == 0 || mix >= 32767) {                 /* OUT only (the default) or AUX only */
        /* x g / 2^15 on the EMAC: no product overflows, so no clip before it (the gains are over 0.5:
         * whatever CL2 would clip lands past +-1 either way) */
        const int32_t *x = mix ? a : o;
        int32_t g = (mix ? ga : go) << 16;
        for (i = 0; i < n; i++) {
            int32_t y = fmac1t(x[i], g);
            out[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = (CL2(o[i]) * go) >> 15, y = (CL2(a[i]) * ga) >> 15;
        x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        y = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
        x += ((y - x) * mix) >> 15;
        out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}
