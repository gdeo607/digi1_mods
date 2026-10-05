/* Digi Mono: the synth engine (see mono.h, DESIGN.md).
 *
 * Numbers: a phase is a 32-bit fraction of a cycle; an oscillator's output is Q15 (+-32768 = +-1); a level
 * is Q15 (32768 = 1). Aliasing is kept down with polyBLEP: each jump of a saw, square or pulse is smoothed
 * over one sample either side by a two-sample polynomial step. Every product fits in 32 bits.
 */
#include "mono.h"
#include "mono_tables.h"

#define CHO_MASK   (MONO_CHO_LEN - 1)
#define CHO_BASE   (336 << 8)               /* chorus centre delay, 7 ms, Q8 samples  */
#define CHO_DEPTH  120                      /* chorus swing at CHRW 127, samples      */
#define CHO_RATE   53687u                   /* chorus LFO, 0.6 Hz                     */

static uint32_t rnd(struct mono_voice *v)
{
    uint32_t r = v->rng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    v->rng = r;
    return r;
}

/* inc * (1 + f/65536), f < 65536 */
static uint32_t scale_up(uint32_t inc, uint32_t f)
{
    return inc + (inc >> 16) * f + (((inc & 0xffff) * f) >> 16);
}

/* inc * (1 - f/65536), f < 65536 */
static uint32_t scale_down(uint32_t inc, uint32_t f)
{
    return inc - (inc >> 16) * f - (((inc & 0xffff) * f) >> 16);
}

static uint32_t pitch_inc_raw(int32_t pitch)
{
    int32_t semi, oct;
    uint32_t inc;
    if (pitch < 0)
        pitch = 0;
    semi = pitch >> 7;                              /* whole semitones above note 0 */
    oct = semi / 12;
    inc = MONO_SEMI_INC[semi - oct * 12];           /* notes 120..131 */
    inc = scale_up(inc, MONO_FINE[pitch & 127]);
    oct = 10 - oct;
    if (oct > 0)
        inc >>= oct < 31 ? oct : 31;
    else if (oct < 0)
        inc = 0xffffffffu;                          /* above note 131: no use to anyone */
    return inc;
}

uint32_t mono_pitch_inc(int32_t pitch)
{
    uint32_t inc = pitch_inc_raw(pitch);
    return inc > MONO_INC_MAX ? MONO_INC_MAX : inc;
}

/* inc moved by s semitones, -36..+36: inc * 2^(semi/12) * 2^(oct - 3), with s + 36 = 12 oct + semi */
static uint32_t interval(uint32_t inc, int32_t s)
{
    int32_t u = s + 36, oct = (u * 43) >> 9;                /* u / 12, exact for 0..72 */
    inc = scale_up(inc, MONO_SEMI_UP[u - oct * 12]) >> 3;
    if (inc > (MONO_INC_MAX >> oct))
        return MONO_INC_MAX;
    return inc << oct;
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

static inline int32_t saw(uint32_t t, uint32_t dt)
{
    return (int32_t)t - 32768 - blep(t, dt);
}

/* For a sum of saws run as one ramp (render_ens): the phase q stepping inc over n samples. d[j] gets
 * -2^28 at each sample just after a wrap (the ramp, in phase >> 4, drops by a cycle there; not at j = 0,
 * which the ramp's start already has), f[j] the blep of the samples on each side of a wrap (16-bit). */
static void saw_wraps(int32_t *d, int32_t *f, uint32_t q, uint32_t inc, int n)
{
    uint32_t dt = inc >> 16;
    int j = 0, last = -1;
    if (!inc)
        return;
    for (;;) {
        uint32_t steps;
        if (q < inc && j != last) {                     /* just after a wrap */
            if (j)
                d[j] -= 1 << 28;
            f[j] -= blep(q >> 16, dt);
            last = j;
        }
        steps = ~q / inc;
        if (steps >= (uint32_t)(n - j))
            break;
        j += (int)steps;
        q += steps * inc;
        if (j != last) {                                /* just before it */
            f[j] -= blep(q >> 16, dt);
            last = j;
        }
        j++;
        q += inc;
        if (j >= n)
            break;
    }
}

static inline int16_t sat16(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t)x;
}

static inline int32_t lin(int32_t v)                /* 0..127 -> 0..32766, straight */
{
    return v * 258;
}

static inline int32_t tri(uint32_t ph)              /* a triangle, 0..32767 */
{
    uint32_t u = ph >> 16;
    return u < 32768 ? (int32_t)u : (int32_t)(65535 - u);
}

/* Shrink levels whose sum is over 1 so that it is 1. g[0..n-1] Q15. */
static void normalise(int32_t *g, int n)
{
    int32_t i, sum = 0, k;
    for (i = 0; i < n; i++)
        sum += g[i];
    if (sum <= 32768)
        return;
    k = (1 << 30) / sum;
    for (i = 0; i < n; i++)
        g[i] = (g[i] * k) >> 15;
}

/* duty from a PW knob: 64 = 50 %, 0 / 127 = 1.2 % / 98.4 % */
static int32_t duty(int32_t pw)
{
    return 32768 + (pw - 64) * 500;
}

void mono_init(struct mono_voice *v)
{
    uint8_t *b = (uint8_t *)v;
    uint32_t i;
    for (i = 0; i < sizeof *v; i++)
        b[i] = 0;
    v->rng = 0x6d2b79f5u;
    macro_init(&v->macro);
}

void mono_trig(struct mono_voice *v, int machine)
{
    int i;
    if (v->rng == 0)
        v->rng = 0x6d2b79f5u;
    v->ph[0] = 0;                                   /* every oscillator here reads 0 at phase 0 */
    for (i = 1; i < 4; i++)
        v->ph[i] = rnd(v);                          /* unison / ensemble: free, like analog ones */
    if (machine == MONO_PSIN)
        v->ph[1] = v->ph[2] = 0;                    /* three sines at one note sum to one: MONO SIN */
    v->sub = 0;
    v->age = 0;
    v->c_lo = v->c_bp = 0;
    v->env = 1u << 30;
    if (machine == MONO_MACRO)
        macro_trig(&v->macro);
}

static void render_sin(struct mono_voice *v, uint32_t inc, int16_t *out, int n)
{
    uint32_t p = v->ph[0];
    while (n--) {
        uint32_t i = p >> 23, f = (p >> 7) & 0xffff;
        int32_t a = MONO_SINE[i], b = MONO_SINE[i + 1];
        *out++ = (int16_t)(a + (((b - a) * (int32_t)f) >> 16));
        p += inc;
    }
    v->ph[0] = p;
}

static void render_nois(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int32_t st = p[0], red = p[1], gt = MONO_GAIN[p[2]];
    uint32_t shi = st ? pitch_inc_raw((135 * 128) - st * 99 * 128 / 127) : 0;
    uint32_t tinc = inc << 1;                       /* tuned: two new values a cycle */
    int32_t a = MONO_GAIN[127 - red] + 256;         /* the red filter, Q15 */
    int32_t mk = 32768 + red * 768;                 /* and its make-up gain, Q15 (x1 .. x4) */
    int32_t hold = v->hold, thold = v->thold, y = v->red;
    uint32_t sh = v->sh, tsh = v->tsh;
    while (n--) {
        int32_t x;
        if (shi == 0) {
            hold = (int32_t)rnd(v) >> 16;
        } else {
            uint32_t s = sh + shi;
            if (s < sh)
                hold = (int32_t)rnd(v) >> 16;
            sh = s;
        }
        if (gt) {
            uint32_t s = tsh + tinc;
            if (s < tsh)
                thold = (int32_t)rnd(v) >> 16;
            tsh = s;
            x = hold + (((thold - hold) * gt) >> 15);
        } else {
            x = hold;
        }
        if (red) {
            y += (((x - y) >> 1) * a) >> 14;
            x = (y * (mk >> 4)) >> 11;
        }
        *out++ = sat16(x);
    }
    v->hold = hold;
    v->thold = thold;
    v->red = y;
    v->sh = sh;
    v->tsh = tsh;
}

/* SAW and PULS as sums of ramps and steps, a chunk of up to CHUNK samples at a time. Every oscillator
 * there is a saw (a ramp that drops a cycle at each wrap), a square or a pulse, and a pulse is two saws a
 * duty apart, of opposite signs (their ramps cancel). So the chunk's output is one ramp, the sum of the
 * oscillators' ramps (level q, slope s: Q30, a Q15 level times a 16-bit value), plus steps where an
 * oscillator jumps; a step's polyBLEP touches only the sample on each side of it. The steps and the
 * blep residuals go into h, as changes from the sample before, so the sample loop is one add a sample
 * whatever the number of oscillators; finding the jumps costs a division or three a wrap, not a test
 * a sample. The same samples as adding up the oscillators one by one, within the rounding (+-1). */
#define CHUNK 32

struct ramp {
    int32_t q, s;                   /* the level at the chunk's first sample (Q30) and its step         */
    int32_t h[CHUNK + 2];           /* changes, from the sample before, at each sample                  */
};

/* g * (x / 65536) for |g| < 2^15, x < 2^32: a phase or a step as a level, Q30 */
static inline int32_t gmul(int32_t g, uint32_t x)
{
    return g * (int32_t)(x >> 16) + ((g * (int32_t)(x & 0xffff)) >> 16);
}

/* A jump of j at each wrap of the phase q (stepping inc, < 2^31, over n samples), with a blep residual
 * of a times the polyBLEP (a = -g: a saw's fall; +g: a rise of 2) on the sample before and the one after
 * it. A wrap just before the chunk (q < inc) has its jump in the chunk's level already. The wraps. */
static int steps(struct ramp *r, uint32_t q, uint32_t inc, int32_t j, int32_t a, int n)
{
    uint32_t dt = inc >> 16;
    int32_t *h = r->h, e, w = 0;
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
        if (inc < (1u << 26) && m * inc <= ~q)  /* no wrap left in the chunk (m <= 32: no overflow) */
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

/* a saw of level g (Q15, either sign) at phase q, stepping inc */
static int ramp_saw(struct ramp *r, uint32_t q, uint32_t inc, int32_t g, int n)
{
    r->q += gmul(g, q) - g * 32768;
    r->s += gmul(g, inc);
    return steps(r, q, inc, -g * 65536, -g, n);
}

/* a square of level g, +1 for the first half cycle */
static void ramp_square(struct ramp *r, uint32_t q, uint32_t inc, int32_t g, int n)
{
    r->q += g * (q < 0x80000000u ? 32767 : -32768);
    steps(r, q, inc, g * 65535, g, n);
    steps(r, q + 0x80000000u, inc, -g * 65535, -g, n);
}

/* the subs, one and two octaves down from the main oscillator at phase p0 after w wraps: a square of
 * level g1 / g2, faded to a falling saw by x (Q15). The saw falls so that its fundamental is in phase
 * with the square's (a rising one would cancel most of it half-way). */
static void ramp_subs(struct ramp *r, uint32_t p0, uint32_t w, uint32_t inc, int32_t g1, int32_t g2, int32_t x, int n)
{
    if (g1) {
        uint32_t q = (w << 31) | (p0 >> 1);
        ramp_square(r, q, inc >> 1, (g1 * (32768 - x)) >> 15, n);
        if (x)
            ramp_saw(r, q, inc >> 1, -((g1 * x) >> 15), n);
    }
    if (g2) {
        uint32_t q = (w << 30) | (p0 >> 2);
        ramp_square(r, q, inc >> 2, (g2 * (32768 - x)) >> 15, n);
        if (x)
            ramp_saw(r, q, inc >> 2, -((g2 * x) >> 15), n);
    }
}

static void ramp_start(struct ramp *r, int n)
{
    int i;
    r->q = 16384;                               /* rounds the sum */
    r->s = 0;
    for (i = 0; i < n + 2; i++)
        r->h[i] = 0;
}

static void ramp_out(const struct ramp *r, int16_t *out, int n)
{
    int32_t q = r->q - r->s, s = r->s;
    const int32_t *h = r->h;
    while (n--) {
        q += s + *h++;
        *out++ = sat16(q >> 15);
    }
}

static void render_saw(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct ramp r;
    int32_t g[6], nu, k, x = lin(p[4]);
    uint32_t iu[4], f = MONO_DETUNE[p[1]];
    nu = p[2] < 43 ? 1 : p[2] < 86 ? 2 : 3;         /* UNIX: how many unison saws */
    iu[0] = inc;
    iu[1] = scale_up(inc, f);
    iu[2] = scale_down(inc, f);
    iu[3] = scale_up(inc, f >> 1);
    g[0] = 32767;
    g[1] = g[2] = g[3] = MONO_GAIN[p[0]];
    for (k = nu + 1; k < 4; k++)
        g[k] = 0;
    g[4] = MONO_GAIN[p[5]];
    g[5] = MONO_GAIN[p[6]];
    normalise(g, 6);
    while (n > 0) {
        int c = n < CHUNK ? n : CHUNK;
        uint32_t p0 = v->ph[0], w = v->sub;
        ramp_start(&r, c);
        v->sub = (uint8_t)(w + ramp_saw(&r, p0, inc, g[0], c));
        v->ph[0] = p0 + (uint32_t)c * inc;
        for (k = 1; k < 4; k++)
            if (g[k]) {
                ramp_saw(&r, v->ph[k], iu[k], g[k], c);
                v->ph[k] += (uint32_t)c * iu[k];
            }
        if (g[4] | g[5])
            ramp_subs(&r, p0, w, inc, g[4], g[5], x, c);
        ramp_out(&r, out, c);
        out += c;
        n -= c;
    }
}

/* a pulse of level g and duty o / 65536: a saw of g / 2 less the saw a duty later */
static int ramp_pulse(struct ramp *r, uint32_t q, uint32_t inc, uint32_t o, int32_t g, int n)
{
    int32_t h = (g + 1) >> 1;
    ramp_saw(r, q + (o << 16), inc, -h, n);
    return ramp_saw(r, q, inc, h, n);
}

static void render_puls(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct ramp r;
    int32_t g[5], o, k;
    uint32_t iu[3], f = MONO_DETUNE[p[1]];
    /* the pulse width for this block: PW, swung by the PWM LFO (triangle, PWRS) by PWAD */
    o = duty(p[4]) + ((((tri(v->lfo) << 1) - 32767) * ((MONO_GAIN[p[5]] * 30000) >> 15)) >> 15);
    if (o < 1024)
        o = 1024;
    if (o > 64511)
        o = 64511;
    v->lfo += MONO_RATE[p[6]] * (uint32_t)n;
    iu[1] = scale_up(inc, f);
    iu[2] = scale_down(inc, f);
    g[0] = 32767;
    g[1] = g[2] = MONO_GAIN[p[0]];
    g[3] = MONO_GAIN[p[2]];
    g[4] = MONO_GAIN[p[3]];
    normalise(g, 5);
    while (n > 0) {
        int c = n < CHUNK ? n : CHUNK;
        uint32_t p0 = v->ph[0], w = v->sub;
        ramp_start(&r, c);
        v->sub = (uint8_t)(w + ramp_pulse(&r, p0, inc, (uint32_t)o, g[0], c));
        v->ph[0] = p0 + (uint32_t)c * inc;
        if (g[1])
            for (k = 1; k < 3; k++) {
                ramp_pulse(&r, v->ph[k], iu[k], (uint32_t)o, g[k], c);
                v->ph[k] += (uint32_t)c * iu[k];
            }
        if (g[3] | g[4])
            ramp_subs(&r, p0, w, inc, g[3], g[4], 0, c);
        ramp_out(&r, out, c);
        out += c;
        n -= c;
    }
}

/* ENS's sample loop over c samples: the summed ramps (and with WAVE, the ramps a duty later), the
 * level, the chorus. has_k and has_c are constants at each call, so each copy has only its own work. */
static inline __attribute__((always_inline)) void ens_loop(struct mono_voice *v, int16_t *out, int c,
        uint16_t *wrp, int32_t *d0p, int32_t dd, uint32_t r1, uint32_t s1, const int32_t *d1, const int32_t *f1,
        uint32_t r2, uint32_t s2, const int32_t *d2, const int32_t *f2, int32_t k, int32_t nw, int32_t gc,
        int32_t nc, const int has_k, const int has_c)
{
    uint16_t wr = *wrp;
    int32_t d0 = *d0p, j;
    for (j = 0; j < c; j++) {
        int32_t x, dry;
        r1 += s1 + (uint32_t)d1[j];
        x = (int32_t)(r1 >> 12) - 4 * 32768 + f1[j];
        if (has_k) {
            r2 += s2 + (uint32_t)d2[j];
            x -= ((((int32_t)(r2 >> 12) - 4 * 32768) + f2[j]) * (k >> 2)) >> 13;   /* 4 saws: 2^17 */
            dry = ((x >> 2) * (nw >> 1)) >> 14;
        } else {
            dry = x >> 2;                           /* nw = 2^15 without WAVE */
        }
        v->dl[wr] = sat16(dry);
        if (has_c) {
            int32_t di = d0 >> 8, fr = d0 & 255;
            int32_t sa = v->dl[(wr - di) & CHO_MASK], sb = v->dl[(wr - di - 1) & CHO_MASK];
            int32_t wet = sa + (((sb - sa) * fr) >> 8);
            dry = ((dry + ((wet * gc) >> 15)) * (nc >> 1)) >> 14;
        }
        out[j] = sat16(dry);
        wr = (wr + 1) & CHO_MASK;
        d0 += dd;
    }
    *wrp = wr;
    *d0p = d0;
}

/* one ENS oscillator, saw - k * (saw a duty later), added into acc */
static void render_ens(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    uint32_t io[4], o = (uint32_t)duty(p[4]);
    int32_t k = lin(p[3]), gc = MONO_GAIN[p[5]], i;
    int32_t nw = (1 << 30) / (32768 + k);           /* keeps saw - k * saw' within +-1 */
    int32_t nc = (1 << 30) / (32768 + gc);          /* and dry + chorus */
    int32_t sw = (lin(p[6]) * CHO_DEPTH) >> 7;      /* chorus swing, Q8 samples */
    int32_t d0, d1, dd;
    uint16_t wr = v->wr;
    io[0] = inc > MONO_INC_MAX ? MONO_INC_MAX : inc;
    for (i = 1; i < 4; i++) {
        int32_t s = (int32_t)p[i - 1] - 63;        /* PCH: 63 = the same pitch */
        io[i] = interval(inc, s < -36 ? -36 : s > 36 ? 36 : s);
    }
    /* A fixed ensemble spread, +4, -4 and +7 cents: oscillators at one pitch beat slowly instead of
     * locking into a comb whose timbre would depend on the phases they started at. */
    io[1] = scale_up(io[1], 152);
    io[2] = scale_down(io[2], 152);
    io[3] = scale_up(io[3], 265);
    /* the chorus delay at the start and the end of the block; the samples between glide */
    d0 = CHO_BASE + ((((tri(v->lfo) << 1) - 32767) * sw) >> 15);
    v->lfo += CHO_RATE * (uint32_t)n;
    d1 = CHO_BASE + ((((tri(v->lfo) << 1) - 32767) * sw) >> 15);
    dd = n == 32 ? (d1 - d0) / 32 : (d1 - d0) / n;  /* the usual block: a shift */
    /* The four saws (and, with WAVE, the four a duty later) are each summed as one ramp: the sum of
     * the phases (>> 4, so four fit in 30 bits) steps by the sum of the increments a sample and drops
     * by a cycle where one wraps (d), with the blep of the samples beside a wrap (f) added in. Within
     * 3 of the saws added one by one (16-bit). */
    while (n > 0) {
        int c = n < CHUNK ? n : CHUNK, j;
        int32_t d1[CHUNK], f1[CHUNK], d2[CHUNK], f2[CHUNK];
        uint32_t r1 = 0, r2 = 0, s1 = 0, s2 = 0;
        for (j = 0; j < c; j++)
            d1[j] = f1[j] = 0;
        if (k)
            for (j = 0; j < c; j++)
                d2[j] = f2[j] = 0;
        for (i = 0; i < 4; i++) {
            uint32_t q = v->ph[i];
            r1 += q >> 4;
            s1 += io[i] >> 4;
            saw_wraps(d1, f1, q, io[i], c);
            if (k) {
                r2 += (q + (o << 16)) >> 4;
                s2 += io[i] >> 4;
                saw_wraps(d2, f2, q + (o << 16), io[i], c);
            }
            v->ph[i] = q + (uint32_t)c * io[i];
        }
        r1 -= s1;                                   /* the loop steps first */
        r2 -= s2;
        /* one copy of the sample loop for each of WAVE on / off and chorus on / off */
        if (k) {
            if (gc)
                ens_loop(v, out, c, &wr, &d0, dd, r1, s1, d1, f1, r2, s2, d2, f2, k, nw, gc, nc, 1, 1);
            else
                ens_loop(v, out, c, &wr, &d0, dd, r1, s1, d1, f1, r2, s2, d2, f2, k, nw, gc, nc, 1, 0);
        } else {
            if (gc)
                ens_loop(v, out, c, &wr, &d0, dd, r1, s1, d1, f1, r2, s2, d2, f2, k, nw, gc, nc, 0, 1);
            else
                ens_loop(v, out, c, &wr, &d0, dd, r1, s1, d1, f1, r2, s2, d2, f2, k, nw, gc, nc, 0, 0);
        }
        out += c;
        n -= c;
    }
    v->wr = wr;
}

/* ---- POLY SIN: three sines at set notes, with a pitch envelope ---------------------------------- *
 * NOT1..NOT3 put the three sines in semitones from the note, 63 = the note itself, -36..+36 (ENS's
 * PCH). At NOT1..3 = 63 the three are in phase and the sum is MONO SIN's sine (within 1 of 32767).
 * EDEP and ESPD are a pitch envelope over all three: EDEP the distance, in semitones, 64 = none,
 * -64..+63; ESPD how fast and which way, 64 = off: above 64 the notes start EDEP away and glide back to
 * their own (a decay), below 64 they start at their own and glide EDEP away (a rise). Further from 64 is
 * faster: a time constant of 2 s at 63 / 65 down to 1 ms at 0 (mono_tables.h, PENV). The envelope is
 * taken a block (32 samples) at a time and the oscillators' steps glide in between. */

/* a * b >> 30 for a, b <= 2^30, in 32-bit (low by at most 2) */
static uint32_t mul_q30(uint32_t a, uint32_t b)
{
    uint32_t ah = a >> 15, al = a & 0x7fff, bh = b >> 15, bl = b & 0x7fff;
    return ah * bh + ((ah * bl + al * bh) >> 15);
}

/* inc moved by e / 128 semitones, |e| < 16 octaves; 0 below the bottom, MONO_INC_MAX at the top */
static uint32_t shift_inc(uint32_t inc, int32_t e)
{
    uint32_t u, o, r;
    int32_t sh;
    if (e <= -16 * 1536 || e >= 16 * 1536)
        return e < 0 ? 0 : MONO_INC_MAX;
    u = (uint32_t)(e + 16 * 1536);
    o = (u * 43691u) >> 26;                         /* u / 1536, exact for u < 2^16 */
    r = u - o * 1536;
    inc = scale_up(scale_up(inc, MONO_SEMI_UP[r >> 7]), MONO_FINE[r & 127]);
    sh = (int32_t)o - 16;
    if (sh < 0)
        return -sh < 32 ? inc >> -sh : 0;
    if (sh > 0) {
        if (sh > 30 || inc > (MONO_INC_MAX >> sh))
            return MONO_INC_MAX;
        inc <<= sh;
    }
    return inc > MONO_INC_MAX ? MONO_INC_MAX : inc;
}

/* the sine table read at phase q, linear between entries */
static inline int32_t sini(uint32_t q)
{
    uint32_t x = q >> 23;
    int32_t a = MONO_SINE[x], b = MONO_SINE[x + 1];
    return a + (((b - a) * (int32_t)((q >> 7) & 0xffff)) >> 16);
}

static void render_psin(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int32_t note[3], dep = ((int32_t)p[3] - 64) * 128, spd = (int32_t)p[4] - 64, i;
    uint32_t m = (uint32_t)(spd < 0 ? -spd : spd), e0 = v->env;
    for (i = 0; i < 3; i++) {
        int32_t s = (int32_t)p[i] - 63;
        note[i] = (s < -36 ? -36 : s > 36 ? 36 : s) * 128;
    }
    while (n > 0) {
        int c = n < CHUNK ? n : CHUNK, j;
        uint32_t e1 = e0, cur[3], ph[3];
        int32_t step[3], off0 = 0, off1 = 0;
        if (spd) {
            uint32_t k = MONO_PENV32[m];
            if (c != 32)
                for (k = 1u << 30, j = 0; j < c; j++)
                    k = mul_q30(k, MONO_PENV1[m]);
            e1 = mul_q30(e0, k);
            /* the offset, 1/128 semitone: EDEP x the envelope (decay) or x (1 - it) (rise) */
            off0 = (dep * (int32_t)((spd > 0 ? e0 : (1u << 30) - e0) >> 15)) >> 15;
            off1 = (dep * (int32_t)((spd > 0 ? e1 : (1u << 30) - e1) >> 15)) >> 15;
        }
        for (i = 0; i < 3; i++) {
            uint32_t a = shift_inc(inc, note[i] + off0);
            cur[i] = a;
            step[i] = spd ? ((int32_t)shift_inc(inc, note[i] + off1) - (int32_t)a) / c : 0;
            ph[i] = v->ph[i];
        }
        {
            /* the three in locals (the CPU's registers); without the envelope the steps stay */
            uint32_t q0 = ph[0], q1 = ph[1], q2 = ph[2], c0 = cur[0], c1 = cur[1], c2 = cur[2];
            if (spd) {
                const uint32_t t0 = (uint32_t)step[0], t1 = (uint32_t)step[1], t2 = (uint32_t)step[2];
                for (j = 0; j < c; j++) {
                    int32_t sum = sini(q0) + sini(q1) + sini(q2);
                    q0 += c0; q1 += c1; q2 += c2;
                    c0 += t0; c1 += t1; c2 += t2;
                    out[j] = (int16_t)((sum * 21845) >> 16);  /* / 3 */
                }
            } else {
                for (j = 0; j < c; j++) {
                    int32_t sum = sini(q0) + sini(q1) + sini(q2);
                    q0 += c0; q1 += c1; q2 += c2;
                    out[j] = (int16_t)((sum * 21845) >> 16);
                }
            }
            ph[0] = q0; ph[1] = q1; ph[2] = q2;
        }
        for (i = 0; i < 3; i++)
            v->ph[i] = ph[i];
        e0 = e1;
        out += c;
        n -= c;
    }
    v->env = e0;
}

/* ---- VO: a formant voice ------------------------------------------------------------------------ *
 * A glottal source (a band-limited saw through a one-pole low-pass, with breath noise mixed in by VOIC)
 * through three parallel resonators at a vowel's first three formants. VOC1 and VOC2 pick vowels along
 * a continuum; V-SW glides from VOC1 to VOC2 after the note starts (0: VOC1 only). CONS picks a
 * consonant, a band of noise with its own decay (CLEN) and level (CVOL) at the note's start, the
 * vowel fading in under it. The vowel formants are published averages of measured male vowels
 * (Peterson and Barney, 1952); nothing here comes from the Monomachine. */

/* F1, F2, F3 in Hz, along a continuum: u  U  aw  ah  uh  ae  eh  ih  ee  er */
static const uint16_t VOWEL[10][3] = {
    {300, 870, 2240}, {440, 1020, 2240}, {570, 840, 2410}, {730, 1090, 2440}, {640, 1190, 2390},
    {660, 1720, 2410}, {530, 1840, 2480}, {390, 1990, 2550}, {270, 2290, 3010}, {490, 1350, 1690},
};
static const uint16_t VBW[3] = {80, 100, 140};             /* formant bandwidths, Hz */
static const int16_t VAMP[3] = {16384, 8192, 4096};        /* formant levels, Q14: 1, -6, -12 dB */
/* consonants: none, S, SH, F, H, T, K, P: centre (Hz), q (Q14: bandwidth / centre), longest (ms) */
static const uint16_t CONS[8][3] = {
    {0, 0, 0}, {6000, 4900, 400}, {2800, 8200, 400}, {7000, 16384, 400}, {1200, 16384, 400},
    {4000, 6500, 30}, {1800, 6500, 40}, {700, 9800, 30},
};

/* 2 sin(pi f / 48000) in Q14, f < 12 kHz: the state-variable filter's frequency coefficient */
static int32_t svf_f(int32_t hz)
{
    int32_t pos = (hz * 1398) >> 10;                        /* Q8 index into MONO_SINE: hz * 512 / 96000 */
    int32_t i = pos >> 8, fr = pos & 255;
    int32_t a = MONO_SINE[i], b = MONO_SINE[i + 1];
    return a + (((b - a) * fr) >> 8);                      /* sin in Q15 = 2 sin in Q14 */
}

/* The same at 24 kHz, where VO's vowel runs: hz * 512 / 48000 */
static int32_t svf_f24(int32_t hz)
{
    int32_t pos = (hz * 2796) >> 10;
    int32_t i = pos >> 8, fr = pos & 255;
    int32_t a = MONO_SINE[i], b = MONO_SINE[i + 1];
    return a + (((b - a) * fr) >> 8);
}

/* VO's vowel at 24 kHz in passes over up to 16 samples: the source, then each formant on its own (one
 * resonator's values fit the CPU's registers; all three at once did not). The same arithmetic as
 * VO_VOWEL, sample for sample. */
static void vo_source(uint32_t *php, uint32_t inc2, uint32_t dt2, int32_t *glpp, int32_t breath, uint32_t *rp,
                      int32_t *sb, int n)
{
    uint32_t ph = *php, r = *rp;
    int32_t glp = *glpp, j;
    for (j = 0; j < n; j++) {
        int32_t s_ = saw(ph >> 16, dt2);
        ph += inc2;
        glp += ((s_ - glp) * 7) >> 4;
        s_ = glp;
        if (breath) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            s_ += (((((int32_t)r >> 16) - s_) >> 1) * breath) >> 14;
        }
        sb[j] = s_;
    }
    *php = ph; *glpp = glp; *rp = r;
}

/* one resonator over sb: its band-pass, >> sh, into acc (set when first, else added) */
static void vo_formant(const int32_t *sb, int32_t *acc, int32_t *lop, int32_t *bpp, int32_t f, int32_t q,
                       int sh, int first, int n)
{
    int32_t lo = *lop, bp = *bpp, j;
    for (j = 0; j < n; j++) {
        int32_t hp_;
        lo += (f * bp) >> 14;
        hp_ = (((sb[j] - bp) * q) >> 14) - lo;
        bp += (f * hp_) >> 14;
        acc[j] = first ? bp >> sh : acc[j] + (bp >> sh);
    }
    *lop = lo; *bpp = bp;
}

static void render_vo(struct mono_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int32_t fk[3], qk[3], gk[3], k, cf = 0, cq = 0, cg, lenc, age0, pos1, pos2, pos, m, breath;
    uint32_t dt = inc >> 16, ph = v->ph[0], morph;
    int ct = p[4] >> 4;                                     /* CONS: 8 zones */
    /* the vowel: VOC1 -> VOC2 by V-SW, per block */
    pos1 = (p[0] * 74309) >> 12;                            /* Q8 along the continuum 0..9: p x 9 x 256 / 127 */
    pos2 = (p[1] * 74309) >> 12;
    if (p[2] == 0) {
        m = 0;
    } else {
        uint32_t mi = MONO_RATE[127 - p[2]] * 10;           /* 5 ms .. 2 s from VOC1 to VOC2 */
        uint32_t a = v->age > 0x7fffff ? 0x7fffff : v->age;
        morph = (a >= 0xffffffffu / (mi ? mi : 1)) ? 0xffffffffu : a * mi;
        m = (int32_t)(morph >> 17);                         /* Q15 */
    }
    pos = pos1 + (((pos2 - pos1) * m) >> 15);
    for (k = 0; k < 3; k++) {
        int32_t i = pos >> 8, fr = pos & 255, hz;
        if (i >= 9) {
            i = 8;
            fr = 256;
        }
        hz = VOWEL[i][k] + (((VOWEL[i + 1][k] - VOWEL[i][k]) * fr) >> 8);
        fk[k] = svf_f24(hz);                                /* the vowel runs at 24 kHz */
        qk[k] = (VBW[k] << 14) / hz;                        /* 1 / Q, Q14 */
        gk[k] = VAMP[k];
    }
    /* the consonant */
    lenc = 96 + ((p[5] * p[5] * 1188) >> 10);               /* CLEN: 2 ms .. 400 ms */
    if (ct && lenc > CONS[ct][2] * 48)
        lenc = CONS[ct][2] * 48;
    if (ct) {
        /* S and F (6, 7 kHz) run at 48 kHz; the others, at 4 kHz or under, at 24 kHz with the vowel */
        cf = CONS[ct][0] > 4000 ? svf_f(CONS[ct][0]) : svf_f24(CONS[ct][0]);
        cq = CONS[ct][1];
    }
    cg = MONO_GAIN[p[6]];
    breath = MONO_GAIN[p[3]];
    age0 = (int32_t)(v->age > 0x7fffffff ? 0x7fffffff : v->age);
    {
        /* The vowel (source, breath, three formants) runs at 24 kHz, every other output sample: everything
         * it makes is under 4 kHz. Between, the output is the line from the last 24 kHz sample to the new
         * one (ph[1] holds the last; bit 0 of pad, which sample of the pair is next: VO uses neither
         * otherwise). The consonant, up to 6 kHz, runs at 48 kHz while it lasts. The loop keeps the state
         * in locals; the consonant's fade-in and decay are ramps with a step worked out once a block; the
         * formant levels (VAMP: 1, 1/2, 1/4) are shifts. */
        int32_t lo0 = v->f_lo[0], lo1 = v->f_lo[1], lo2 = v->f_lo[2];
        int32_t bp0 = v->f_bp[0], bp1 = v->f_bp[1], bp2 = v->f_bp[2];
        int32_t glp = v->glp;
        const int32_t f0 = fk[0], f1 = fk[1], f2 = fk[2], q0 = qk[0], q1 = qk[1], q2 = qk[2];
        int32_t half = lenc >> 1, vstep = 0, einv = 0, yh, last;
        uint32_t r = v->rng, inc2 = inc > 0x3fffffffu ? 0x7fffffffu : inc << 1, dt2 = inc2 >> 16;
        int par;
        (void)gk;
        (void)dt;
        if (v->age == 0) {                                  /* a new note: the pair starts afresh */
            v->ph[1] = 0;
            v->pad = 0;
        }
        last = (int32_t)v->ph[1];
        par = v->pad & 1;
        yh = last;
        if (ct) {
            vstep = (32767 << 8) / (half + 1);              /* venv = age0 x 32767 / (half + 1), Q8 */
            einv = (1 << 23) / lenc;                        /* e = (lenc - age0) << 15 / lenc, Q8 */
        }
/* one output sample of the vowel into y: a new 24 kHz sample on the first of a pair, else the held one */
#define VO_VOWEL(y) do {                                                                \
            if (!par) {                                                                 \
                int32_t s_, hp_;                                                        \
                s_ = saw(ph >> 16, dt2);                                                \
                ph += inc2;                                                             \
                glp += ((s_ - glp) * 7) >> 4;               /* about 2 kHz, one pole */ \
                s_ = glp;                                                               \
                if (breath) {                                                           \
                    r ^= r << 13; r ^= r >> 17; r ^= r << 5;                            \
                    s_ += (((((int32_t)r >> 16) - s_) >> 1) * breath) >> 14;            \
                }                                                                       \
                lo0 += (f0 * bp0) >> 14;                                                \
                hp_ = (((s_ - bp0) * q0) >> 14) - lo0;                                 \
                bp0 += (f0 * hp_) >> 14;                                                \
                lo1 += (f1 * bp1) >> 14;                                                \
                hp_ = (((s_ - bp1) * q1) >> 14) - lo1;                                 \
                bp1 += (f1 * hp_) >> 14;                                                \
                lo2 += (f2 * bp2) >> 14;                                                \
                hp_ = (((s_ - bp2) * q2) >> 14) - lo2;                                 \
                bp2 += (f2 * hp_) >> 14;                                                \
                yh = bp0 + (bp1 >> 1) + (bp2 >> 2);                                     \
                y = (last + yh) >> 1;                       /* halfway from the last */ \
                last = yh;                                                              \
            } else {                                                                    \
                y = yh;                                                                 \
            }                                                                           \
            par ^= 1;                                                                   \
        } while (0)
        if (ct && age0 < lenc) {
            /* while the consonant lasts: the vowel fades in over its first half under a noise band that
             * decays. In chunks: the vowel into yb, then the consonant over it (two light loops rather
             * than one with more values than the CPU has registers). The noise band runs at 48 kHz for S
             * and F, else at 24 kHz with the vowel (halfway values between). Both ramps are running sums. */
            int32_t clo = v->c_lo, cbp = v->c_bp, clast = 0, cnew = 0;
            int32_t va = age0 * vstep, ea = (lenc - age0) * einv;
            /* the band's level, (ea >> 8) x CVOL in Q14, as a ramp: one step a sample */
            int32_t ga = (ea >> 8) * cg, gstep = ((einv >> 4) * cg) >> 4;
            const int full = CONS[ct][0] > 4000;
            int cn = lenc - age0 < n ? lenc - age0 : n;
            while (cn > 0) {
                int c = cn < 32 ? cn : 32, j, tick = !par;
                int32_t yb[32];
                if (!par && !(c & 1)) {                     /* whole pairs: in passes, as below */
                    int32_t sb[16], y24[16];
                    vo_source(&ph, inc2, dt2, &glp, breath, &r, sb, c >> 1);
                    vo_formant(sb, y24, &lo0, &bp0, f0, q0, 0, 1, c >> 1);
                    vo_formant(sb, y24, &lo1, &bp1, f1, q1, 1, 0, c >> 1);
                    vo_formant(sb, y24, &lo2, &bp2, f2, q2, 2, 0, c >> 1);
                    for (j = 0; j < c; j += 2) {
                        yh = y24[j >> 1];
                        yb[j] = (last + yh) >> 1;
                        yb[j + 1] = yh;
                        last = yh;
                    }
                } else {
                    for (j = 0; j < c; j++)
                        VO_VOWEL(yb[j]);
                }
                for (j = 0; j < c; j++, tick ^= 1) {
                    int32_t x, cy, venv = age0 < half ? va >> 8 : 32767;
                    if (full || tick) {
                        int32_t nz, hp;
                        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                        nz = (int32_t)r >> 17;
                        clo += (cf * cbp) >> 14;
                        hp = (((nz - cbp) * cq) >> 14) - clo;
                        cbp += (cf * hp) >> 14;
                        cnew = (cbp * (ga >> 14)) >> 15;    /* x2: the band's level is its q's */
                        cy = full ? cnew : (clast + cnew) >> 1;
                        clast = cnew;
                    } else {
                        cy = cnew;
                    }
                    x = ((yb[j] * (venv >> 1)) >> 13) + cy;   /* the vowel x2 make-up with the fade */
                    *out++ = sat16(x);
                    va += vstep;
                    ga -= gstep;
                    age0++;
                }
                cn -= c;
                n -= c;
            }
            v->c_lo = clo;
            v->c_bp = cbp;
        }
        if (n > 0) {                                        /* the vowel alone, a 24 kHz sample a pair */
            int32_t rest = n, y;
            if (par) {                                      /* the second of a pair first */
                VO_VOWEL(y);
                *out++ = sat16((y * 16383) >> 13);
                n--;
            }
            while (n >= 2) {                                /* in passes, up to 16 pairs at a time */
                int32_t sb[16], yb[16], j, c = n >> 1 > 16 ? 16 : n >> 1;
                vo_source(&ph, inc2, dt2, &glp, breath, &r, sb, c);
                vo_formant(sb, yb, &lo0, &bp0, f0, q0, 0, 1, c);
                vo_formant(sb, yb, &lo1, &bp1, f1, q1, 1, 0, c);
                vo_formant(sb, yb, &lo2, &bp2, f2, q2, 2, 0, c);
                for (j = 0; j < c; j++) {
                    yh = yb[j];
                    y = (last + yh) >> 1;
                    out[0] = sat16((y * 16383) >> 13);
                    out[1] = sat16((yh * 16383) >> 13);
                    last = yh;
                    out += 2;
                }
                n -= 2 * c;
            }
            while (n >= 2) {
                VO_VOWEL(y);                                /* a new sample: halfway to it */
                out[0] = sat16((y * 16383) >> 13);
                out[1] = sat16((yh * 16383) >> 13);         /* then it */
                par ^= 1;
                out += 2;
                n -= 2;
            }
            if (n) {
                VO_VOWEL(y);
                *out++ = sat16((y * 16383) >> 13);
            }
            age0 = age0 > 0x7fffffff - rest ? 0x7fffffff : age0 + rest;
        }
#undef VO_VOWEL
        v->f_lo[0] = lo0; v->f_lo[1] = lo1; v->f_lo[2] = lo2;
        v->f_bp[0] = bp0; v->f_bp[1] = bp1; v->f_bp[2] = bp2;
        v->glp = glp; v->rng = r;
        v->ph[1] = (uint32_t)last;
        v->pad = (uint8_t)par;
    }
    v->ph[0] = ph;
    v->age = (uint32_t)age0;
}

void mono_render(struct mono_voice *v, int machine, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    if (inc > MONO_INC_MAX)
        inc = MONO_INC_MAX;
    switch (machine) {
    case MONO_SIN:  render_sin(v, inc, out, n); break;
    case MONO_NOIS: render_nois(v, p, inc, out, n); break;
    case MONO_SAW:  render_saw(v, p, inc, out, n); break;
    case MONO_PULS: render_puls(v, p, inc, out, n); break;
    case MONO_ENS:  render_ens(v, p, inc, out, n); break;
    case MONO_VO:   render_vo(v, p, inc, out, n); break;
    case MONO_PSIN: render_psin(v, p, inc, out, n); break;
    case MONO_MACRO:
        while (n > 0) {                     /* the MACRO engines work in blocks of up to 32 */
            int k = n < 32 ? n : 32;
            macro_render(&v->macro, p, inc, out, k);
            out += k;
            n -= k;
        }
        break;
    default:
        while (n--)
            *out++ = 0;
    }
}
