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
    r->v = *prev << 8;
    r->d = ((target - *prev) << 8) / n;
    *prev = target;
}
static inline int32_t ramp_next(struct ramp *r) { r->v += r->d; return r->v >> 8; }

/* 2^x, x Q16 (any sign), as Q16; a quartic on the fraction, within 5e-6 (0.01 cent) */
static uint32_t exp2_q16(int32_t x)
{
    int32_t e = x >> 16;
    uint32_t t = (uint32_t)x & 0xffff, y;
    y = 65536 + ((t * (45416 + ((t * (15831 + ((t * (3392 + ((t * 897) >> 16))) >> 16))) >> 16))) >> 16);
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
        out[i] = ((c[0] << 4) + (s1 << 3) + s1 - (c[-3] + c[3])) >> 5;
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

/* ---- the machine ---------------------------------------------------------------------------------- */

/* the gains Plaits' voice gives each engine's OUT and AUX (voice.cc, RegisterInstance), Q15 */
static const int16_t gain_out[MACRO_ENGINES] = {22938, 19661};    /* WSH 0.7, FM 0.6 */
static const int16_t gain_aux[MACRO_ENGINES] = {19661, 19661};    /* WSH 0.6, FM 0.6 */

const char *const macro_engine_name[MACRO_ENGINES] = {"WSHAPE", "2OP FM"};

int macro_engine_of(int b)
{
    int e = (b * MACRO_ENGINES) >> 7;
    return e < MACRO_ENGINES ? e : MACRO_ENGINES - 1;
}

static void engine_init(struct macro_voice *m)
{
    switch (m->engine) {
    case MACRO_WSH: wsh_init(&m->e.wsh); break;
    case MACRO_FM:  fm_init(&m->e.fm); break;
    default: break;
    }
}

void macro_init(struct macro_voice *m)
{
    m->engine = 0;
    m->latch = 1;
    engine_init(m);
}

void macro_trig(struct macro_voice *m)
{
    m->latch = 1;
}

void macro_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int16_t *out, int n)
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
    }
    if (inc > INC_MAX)
        inc = INC_MAX;
    if (inc < INC_MIN)
        inc = INC_MIN;
    mix = k15(p[MACRO_P_AUX]);
    switch (m->engine) {
    case MACRO_WSH: wsh_render(&m->e.wsh, p, inc, o, a, n, mix < 32767, mix > 0); break;
    case MACRO_FM:  fm_render(&m->e.fm, p, inc, o, a, n, mix > 0); break;
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
        x += ((y - x) * mix) >> 15;
        out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}
