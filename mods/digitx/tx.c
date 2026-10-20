/* Digi TX: the drum kit machine (tx.h). Everything per block (32 samples, 0.67 ms) is worked out at the block's
 * start: each part's pitch, and its level at the block's end; inside the block the levels ramp, the pitches
 * hold. Integer only: Q15 samples, Q16 levels, phases as 32-bit fractions of a cycle.
 *
 * The drums, after hugz202's measurements of the TX-8 and TX-9 machines (AE-analog-rytm-mods,
 * mods/0009-tx-machines/design.md, MIT licence); t30 is the time to -30 dB. The pitches are the originals' at
 * C4 and TUNE 0, which move them (no pitch knob: TUNE is it); the decays are their longest, which the
 * track's AMP envelope shortens (no decay knob: AMP's is it). C SWT scales every pitch sweep's time constant
 * (x0.25..x4, as measured at 64); D VAR makes each hit a little different (pitch +-0.5 semitone, level
 * +-25 %, snap +-50 % at 127); F DRV drives the whole drum into a soft clipper (0: clean, as the originals).
 *   BD9   56 Hz, starting 1 + 5.4 TONE/127 times higher, the sweep falling with an 11.5 ms time constant
 *         (175 > 122 > 83 > 64 > 58 > 56 Hz at TONE 64); held 15..65 ms (CHAR; 40 at 64), then a t30 of
 *         344 ms in all; a click (SNAP)
 *   BD8   49.7 Hz; TONE adds a pitch sweep (up to 3x, 8 ms; 0: none, as the 808); t30 1170 ms, shorter as
 *         the pitch rises (x1.53 an octave down, /2.67 an octave up, as 634 / 414 / 155 ms measured); the
 *         sigh (CHAR): a linear fall of up to 8 % over 165 ms; the attack step (SNAP): SNAP x 0.067 ms at
 *         2.2x the pitch
 *   SD9   two bodies, 187.5 Hz and that times 1 + 0.75 CHAR/127; t30 725 ms, the second 0.6 of it; a small
 *         bend (5 ms); a noise high-passed at 1.2 kHz and low-passed at 9 kHz, level SNAP, t30 60..400 ms (TONE)
 *   SD8   two bodies, 180 Hz and that times 1.5 + CHAR/127, balanced by TONE; t30 400 ms; a noise through a
 *         2-pole high-pass at 2.42 kHz and a 1-pole low-pass at 3 kHz (hugz202's match)
 *   LT9 MT9 HT9  91.5 / 112 / 134.5 Hz; a bend from 1 + 0.6 TONE/127 falling with 9.5 ms; a partial at 2/3
 *         of the pitch, -5 dB at CHAR 64, decaying 1.1x as long; t30 1500 ms, the same at every pitch
 *   LT8 MT8 HT8  91 / 141 / 192 Hz; t30 2500 ms, going as f^-0.93; the droop (CHAR): up to 12 % higher at
 *         the start, falling with 6 ms; TONE a bend as the toms 9's
 * SNAP is the noise or click. The bodies are sines from a refined parabola (y (0.775 + 0.225 |y|), within
 * 0.1 % of a sine's shape).
 */
#include "tx.h"
#include "tx_tables.h"

const char *const tx_drum_name[TX_DRUMS] = {
    "BD9", "BD8", "SD9", "SD8", "LT9", "MT9", "HT9", "LT8", "MT8", "HT8",
};

int tx_drum_of(int v)
{
    int d = (v & 127) >> TX_ZONE_SHIFT;
    return d >= TX_DRUMS ? TX_DRUMS - 1 : d;
}

/* 2^x, x and the result in Q16; x from -20 to 14 */
int32_t tx_exp2_q16(int32_t x)
{
    int32_t i = x >> 16, f = x & 0xffff, k = f >> 10, r = f & 1023;
    uint32_t m = tx_exp2_frac[k] + (((tx_exp2_frac[k + 1] - tx_exp2_frac[k]) * (uint32_t)r) >> 10);
    if (i >= 0)
        return i >= 15 ? 0x7fffffff : (int32_t)(m << i);
    return i <= -31 ? 0 : (int32_t)(m >> -i);
}

/* log2 x, x and the result Q16 (x > 0) */
int32_t tx_log2_q16(uint32_t x)
{
    int32_t e = 0, lo = 0, hi = 64;
    if (!x)
        return -(32 << 16);
    while (x >= 131072u) {
        x >>= 1;
        e++;
    }
    while (x < 65536u) {
        x <<= 1;
        e--;
    }
    while (hi - lo > 1) {                           /* tx_exp2_frac[lo] <= x < tx_exp2_frac[hi] */
        int32_t m = (lo + hi) >> 1;
        if (tx_exp2_frac[m] <= x)
            lo = m;
        else
            hi = m;
    }
    return (e << 16) + (lo << 10) + (int32_t)(((x - tx_exp2_frac[lo]) << 10) / (tx_exp2_frac[hi] - tx_exp2_frac[lo]));
}

/* a pitch envelope's factor a block for a time constant tau (Q4 ms): e^(-0.667 / tau), Q15 */
static int32_t sweep_g(int32_t tau_q4)
{
    if (tau_q4 < 8)
        tau_q4 = 8;
    return tx_exp2_q16(-(1008528 / tau_q4)) >> 1;
}

/* a knob (0..127) over tmin..tmin 2^L, exponentially (L Q16): ms */
static int32_t map_ms(int v, int32_t tmin, int32_t l)
{
    return (tmin * tx_exp2_q16(v * l / 127)) >> 16;
}

/* a level's factor a block for a t30 in ms: 2^(-4.98 / (1.5 t30)), Q15 */
static int32_t decay_g(int32_t t30)
{
    if (t30 < 3)
        t30 = 3;
    return tx_exp2_q16(-(217705 / t30)) >> 1;
}

static uint32_t hz_inc(int32_t f_q4)             /* Q4 Hz -> phase step at 48 kHz */
{
    if (f_q4 < 16)
        f_q4 = 16;
    if (f_q4 > 16 * 8000)
        f_q4 = 16 * 8000;
    return (uint32_t)f_q4 * 5592u + (((uint32_t)f_q4 * 26u) >> 6);
}

static inline int32_t body(uint32_t ph)          /* a sine, Q15 */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    if (y > 32767)
        y = 32767;
    ay = y < 0 ? -y : y;
    return (y * (25395 + ((7373 * ay) >> 15))) >> 15;
}

static inline int32_t drive(int32_t y, int32_t d)  /* y + d (y - y^3): more level, softly limited; d Q15 */
{
    int32_t y3 = (((y * y) >> 15) * y) >> 15;
    return y + ((d * (y - y3)) >> 15);
}

void tx_init(struct tx_voice *v)
{
    int32_t *w = (int32_t *)v, *end = (int32_t *)(v + 1);
    while (w < end)
        *w++ = 0;
    v->rnd = 22222;
}

void tx_trig(struct tx_voice *v, const uint8_t *p)
{
    uint32_t rnd = v->rnd;
    int d = tx_drum_of(p[TX_P_TYPE]);
    tx_init(v);
    v->rnd = rnd;
    v->drum = d;
    v->live = 1;
    v->sweep = 65536;
    v->bend = 65536;
    v->a1 = 65535;
    switch (d) {
    case TX_BD9:
        v->ac = p[TX_P_SNAP] * 400;                 /* the click */
        break;
    case TX_BD8:
        v->step_left = (p[TX_P_SNAP] * 205) >> 6;   /* SNAP x 0.067 ms, in samples */
        break;
    case TX_SD9:
        v->a1 = 39321;                              /* 0.6, 0.38 */
        v->a2 = 24903;
        v->an = p[TX_P_SNAP] * 413;                 /* up to 0.8 */
        break;
    case TX_SD8:
        v->a1 = (127 - p[TX_P_TONE]) * 413;
        v->a2 = p[TX_P_TONE] * 413;
        v->an = p[TX_P_SNAP] * 413;
        break;
    case TX_LT9: case TX_MT9: case TX_HT9: {
        int32_t a = p[TX_P_CHAR] * 372;             /* -5 dB under the body at 64 */
        v->a1 = 42598;                              /* 0.65: room for the partial */
        v->a2 = a > 47000 ? 47000 : a;
        v->an = p[TX_P_SNAP] * 206;
        break;
    }
    default:                                        /* toms 8 */
        v->an = p[TX_P_SNAP] * 206;
        break;
    }
    v->vp = 65536;
    if (p[TX_P_VAR]) {                              /* VAR: +-0.5 semitone, +-25 % level, +-50 % snap at 127 */
        int32_t var = p[TX_P_VAR], r;
        rnd = rnd * 1664525u + 1013904223u;
        r = (int32_t)rnd >> 16;                     /* -32768..32767 */
        v->vp = tx_exp2_q16((r * var * 21) >> 15);
        rnd = rnd * 1664525u + 1013904223u;
        r = (int32_t)rnd >> 16;
        r = 65536 + ((r * var) >> 9) * 2;           /* 1 +- 0.25 var/127, Q16 */
        v->a1 = (int32_t)(((uint32_t)v->a1 >> 4) * (uint32_t)r >> 12);
        v->a2 = (int32_t)(((uint32_t)v->a2 >> 4) * (uint32_t)r >> 12);
        if (v->a1 > 65535)
            v->a1 = 65535;
        if (v->a2 > 65535)
            v->a2 = 65535;
        rnd = rnd * 1664525u + 1013904223u;
        r = (int32_t)rnd >> 16;
        r = 65536 + ((r * var) >> 8) * 2;           /* 1 +- 0.5 var/127 */
        v->an = (int32_t)(((uint32_t)v->an >> 4) * (uint32_t)r >> 12);
        v->ac = (int32_t)(((uint32_t)v->ac >> 4) * (uint32_t)r >> 12);
        v->rnd = rnd;
    }
}

static const int32_t tom9_f0[3] = {976, 1192, 1435};         /* Q4 Hz: 61, 74.5, 89.7 */
static const int32_t tom8_f0[3] = {1456, 2256, 3072};        /* 91, 141, 192 */

void tx_render(struct tx_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    int32_t ratio, f1, f2 = 0, g1, g2 = 0, gn = 0, gc = 0, a1e, a2e, ane, ace, d1, d2, dn, dc;
    int32_t a1 = v->a1, a2 = v->a2, an = v->an, ac = v->ac, dr = 0, i, b = v->blocks;
    int32_t tone = p[TX_P_TONE], chr = p[TX_P_CHAR], lr;
    int32_t swt = tx_exp2_q16((p[TX_P_SWT] - 64) * 2048);   /* x0.25..x4 the sweeps' time constants, Q16 */
    int32_t drv = p[TX_P_DRV];                   /* 0: clean */
    uint32_t i1, i2 = 0, ph1 = v->ph1, ph2 = v->ph2, rnd = v->rnd;
    int noise = 0, step = 0;
    if (!v->live) {
        for (i = 0; i < n; i++)
            out[i] = 0;
        return;
    }
    ratio = (int32_t)(inc / 5715u);              /* the note and TUNE, Q12 (C4 4096) */
    ratio = (ratio * (v->vp >> 4)) >> 12;        /* VAR */
    if (ratio < 256)
        ratio = 256;
    if (ratio > 32768)
        ratio = 32768;
    lr = tx_log2_q16((uint32_t)ratio << 4);      /* octaves from C4, Q16: the decays that follow the pitch */

    switch (v->drum) {
    case TX_BD9: {
        int32_t fend = 896;                                              /* 56 Hz, Q4 */
        int32_t rm1 = tone * 2768;                                       /* (R - 1), Q16 */
        int32_t hold = 15 + ((50 * chr) >> 7), t30 = 344, msec = (b * 2) / 3;
        f1 = fend + ((fend * (((rm1 >> 4) * (v->sweep >> 4)) >> 8)) >> 16);
        v->sweep = (v->sweep * sweep_g((184 * swt) >> 16)) >> 15;       /* 11.5 ms */
        g1 = msec < hold ? 32768 : decay_g(t30 - hold > 10 ? t30 - hold : 10);
        i2 = hz_inc(1500 * 16);                                          /* the click's tone */
        gc = decay_g(6);
        break;
    }
    case TX_BD8: {
        int32_t t30 = 1170, k, sigh;
        f1 = 795;                                                        /* 49.7 Hz */
        k = lr < 0 ? tx_exp2_q16(-((lr * 40) >> 6)) : tx_exp2_q16(-((lr * 93) >> 6));  /* x1.53 an octave down, /2.67 up */
        t30 = (t30 * k) >> 16;
        sigh = chr * 41;                                                 /* up to 8 %, Q16 */
        f1 += (f1 * (((tone * 1032) >> 4) * (v->sweep >> 4) >> 8)) >> 16; /* x (1 + 2 TONE/127) at the start */
        v->sweep = (v->sweep * sweep_g((128 * swt) >> 16)) >> 15;       /* 8 ms */
        if (b < 247)
            f1 -= (f1 * ((sigh * b) / 247)) >> 16;
        else
            f1 -= (f1 * sigh) >> 16;
        g1 = decay_g(t30);
        step = 1;
        break;
    }
    case TX_SD9: {
        int32_t t30 = 725;
        f1 = 3000;                                                       /* 187.5 Hz */
        f1 += (f1 * ((9830 * (v->sweep >> 4)) >> 12)) >> 16;             /* a bend of 0.15 */
        f2 = f1 + ((f1 * chr * 387) >> 16);                              /* x (1 + 0.75 CHAR/127) */
        v->sweep = (v->sweep * sweep_g((80 * swt) >> 16)) >> 15;        /* 5 ms */
        g1 = decay_g(t30);
        g2 = decay_g((t30 * 3) / 5);
        gn = decay_g(map_ms(tone, 60, 179380));
        noise = 1;
        break;
    }
    case TX_SD8: {
        int32_t t30 = 400;
        f1 = 2880;                                                       /* 180 Hz */
        f2 = f1 + (f1 >> 1) + ((f1 * chr * 516) >> 16);                 /* x (1.5 + CHAR/127) */
        g1 = g2 = decay_g(t30);
        gn = decay_g((t30 * 6) / 5);
        noise = 2;
        break;
    }
    case TX_LT9: case TX_MT9: case TX_HT9: {
        int32_t t30 = 1500;
        f1 = (tom9_f0[v->drum - TX_LT9] * 3) >> 1;                       /* 91.5 / 112 / 134.5 Hz */
        f1 += (f1 * (((tone * 310) >> 4) * (v->sweep >> 4) >> 8)) >> 16; /* the bend, 1 + 0.6 TONE/127 */
        f2 = (f1 * 2) / 3;
        v->sweep = (v->sweep * sweep_g((152 * swt) >> 16)) >> 15;       /* 9.5 ms */
        g1 = decay_g(t30);
        g2 = decay_g((t30 * 11) / 10);
        gn = decay_g(50);
        noise = 3;
        break;
    }
    default: {                                                            /* toms 8 */
        int32_t t30 = 2500;
        f1 = tom8_f0[v->drum - TX_LT8];
        t30 = (t30 * tx_exp2_q16(-((lr * 60) >> 6))) >> 16;              /* as f^-0.93 */
        f1 += (f1 * (((chr * 62) * (v->sweep >> 4)) >> 12)) >> 16;      /* the droop: 1 + 0.12 CHAR/127 */
        f1 += (f1 * (((tone * 310) >> 4) * (v->bend >> 4) >> 8)) >> 16;  /* the bend, 1 + 0.6 TONE/127 */
        v->sweep = (v->sweep * 29322) >> 15;
        v->bend = (v->bend * sweep_g((152 * swt) >> 16)) >> 15;         /* 9.5 ms */
        g1 = decay_g(t30);
        gn = decay_g(50);
        noise = 3;
        break;
    }
    }
    f1 = (f1 * ratio) >> 12;
    i1 = hz_inc(f1);
    if (f2)
        i2 = hz_inc((f2 * ratio) >> 12);

    /* the levels at the block's end, and their steps */
    a1e = (int32_t)(((uint32_t)a1 * (uint32_t)g1) >> 15);
    a2e = g2 ? (int32_t)(((uint32_t)a2 * (uint32_t)g2) >> 15) : 0;
    ane = gn ? (int32_t)(((uint32_t)an * (uint32_t)gn) >> 15) : 0;
    ace = gc ? (int32_t)(((uint32_t)ac * (uint32_t)gc) >> 15) : 0;
    d1 = (a1e - a1) / n;
    d2 = (a2e - a2) / n;
    dn = (ane - an) / n;
    dc = (ace - ac) / n;

    for (i = 0; i < n; i++) {
        int32_t s, y;
        uint32_t st = i1;
        if (step && v->step_left > 0) {
            st = i1 * 2 + (i1 >> 2) - (i1 >> 4);                         /* 2.19x */
            v->step_left--;
        }
        s = body(ph1);
        ph1 += st;
        if (dr)
            s = drive(s, dr);
        y = (s * (a1 >> 1)) >> 15;                                       /* Q15 x Q16 -> Q15 */
        if (a2) {
            y += (body(ph2) * (a2 >> 1)) >> 15;
            ph2 += i2;
        } else if (ac) {
            int32_t c = (body(ph2) * 11469 + ((int32_t)rnd >> 17) * 9830) >> 15;
            rnd = rnd * 1664525u + 1013904223u;
            y += (c * (ac >> 1)) >> 15;
            ph2 += i2;
        }
        if (noise && an) {
            int32_t x = (int32_t)rnd >> 17, z;                           /* +-16384 */
            rnd = rnd * 1664525u + 1013904223u;
            if (noise == 1) {                                            /* HP 1.2 kHz, LP 9 kHz */
                v->hp1 += ((x - v->hp1) * 4763) >> 15;
                z = x - v->hp1;
                v->lp1 += ((z - v->lp1) * 22680) >> 15;
                z = v->lp1;
            } else if (noise == 2) {                                     /* HP 2.42 kHz (2-pole), LP 3 kHz */
                z = (13093 * (x + v->hx2) - 26185 * v->hx1 + 25517 * v->hy1 - 10469 * v->hy2) >> 14;
                v->hx2 = v->hx1;
                v->hx1 = x;
                v->hy2 = v->hy1;
                v->hy1 = z;
                v->lp1 += ((z - v->lp1) * 10642) >> 15;
                z = v->lp1;
            } else {                                                     /* LP 4 kHz */
                v->lp1 += ((x - v->lp1) * 13357) >> 15;
                z = v->lp1;
            }
            y += (z * (an >> 1)) >> 14;
        }
        if (y > 65535)
            y = 65535;
        else if (y < -65535)
            y = -65535;
        if (drv) {                                                       /* x 1..4, then x - 4 x^3 / 27 */
            int32_t x3;
            y += (y * drv * 3) >> 7;
            if (y > 49151)
                y = 49151;
            else if (y < -49151)
                y = -49151;
            x3 = (((y * y) >> 15) * (y >> 1)) >> 14;                     /* |y| <= 1.5: no overflow */
            y -= (x3 * 4) / 27;
        } else
            y = (y * 29000) >> 15;                                       /* 0.885: headroom for the drive-less level */
        out[i] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
        a1 += d1;
        a2 += d2;
        an += dn;
        ac += dc;
    }
    v->a1 = a1e;
    v->a2 = a2e;
    v->an = ane;
    v->ac = ace;
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->rnd = rnd;
    v->blocks = b + 1;
    if (a1e < 16 && a2e < 16 && ane < 16 && ace < 16 && (!step || v->step_left <= 0))
        v->live = 0;
}
