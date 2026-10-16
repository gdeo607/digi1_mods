/* SPDX-License-Identifier: GPL-2.0-or-later
 * digiwrmz: the buffer-FX DELAY engine. A Core A master insert appended after the
 * Tone+FX chain (order 63). Digitone mk1 / Keys, OS 1.43.
 *
 * ONE dedicated delay line (mono, 24-bit) serves two modes, like digiwave's
 * modes on its own line:
 *   mode 0  WRMZ    a modulated delay (time, feedback, tone, wow/flutter)
 *   mode 1  GRAINZ  a grain-ish cloud: three drifting, interpolated taps off the
 *                   SAME line (no history ring, no head pool, no per-grain state)
 *
 * The wet is mono and mixed into both output channels (dry stays stereo). Params
 * live in one set per mode; the DELAYS page edits the active one. True bypass
 * when off. Integer, no libc, no 64-bit mul.
 */
#include "wrmz.h"
#include "buffx.h"
#include "emac.h"

int digiwrmz_on = 0;
int digiwrmz_time = 40;         /* 0..127 delay time */
int digiwrmz_fb = 32;           /* 0..127 feedback */
int digiwrmz_tone = 64;         /* 0..127 damping */
int digiwrmz_mod = 0;           /* 0..127 modulation depth */
int digiwrmz_rate = 40;         /* 0..127 modulation rate */
int digiwrmz_mix = 64;          /* 0..127 dry/wet */
int digiwrmz_mode = 0;          /* 0 WRMZ, 1 GRAINZ */

int digigrainz_on = 0;          /* GRAINZ mode params (same line) */
int digigrainz_dens = 64;       /* drift rate */
int digigrainz_size = 64;       /* tap spacing / spread */
int digigrainz_pitch = 64;      /* tap read rate */
int digigrainz_spray = 32;      /* drift depth */
int digigrainz_fb = 40;         /* feedback */
int digigrainz_mix = 90;        /* dry/wet */

#define WZ_N     (1u << 14)             /* 0.34 s @48k, mono + a guard */
#define WZ_MASK  (WZ_N - 1u)
#define WZ_MAXD  (WZ_N - 8u)

static volatile unsigned wz_off;

/* Line sample format. Default: 24-bit `int` (full bus resolution + feedback
 * headroom). With BX_S16_LINE the line is `short` -- half the DDR traffic, at
 * 16-bit tail quantization -- the §1 "24-bit delay line" bisect knob. Either
 * way the write saturates at the 24-bit bus, so the (soft-clipped) feedback
 * never folds; interpolation runs in the line's scale and converts once. */
#if defined(BX_S16_LINE)
typedef short wz_samp;
static inline int wz_rd(const wz_samp *b, unsigned idx) { return (int)b[idx] << 8; }
static inline int wz_rdi(const wz_samp *b, unsigned idx, int frac)
{
    int a = b[idx];
    return (a + bx_mul15(b[idx + 1] - a, frac)) << 8;
}
static inline wz_samp wz_mk(int v) { return (wz_samp)bx_sat16(v >> 8); }
#else
typedef int wz_samp;
static inline int wz_rd(const wz_samp *b, unsigned idx) { return b[idx]; }
static inline int wz_rdi(const wz_samp *b, unsigned idx, int frac) { return bx_read_at(b, idx, frac); }
static inline wz_samp wz_mk(int v) { return bx_soft(v, 0x7fffff); }
#endif

static wz_samp wz_buf[WZ_N + 1] __attribute__((aligned(16)));
static unsigned wz_w;
static unsigned wz_phase;
static int wz_lp;
static int wz_dc[2];
static int wz_mix;
static int wz_lastmode = -1;

void digiwrmz_render_in(void)
{
    wz_off = dn_out_off();
}

void digiwrmz_render_out(void)
{
    int *p;
    int i, mode, on, mix_t, mstep, fb, lpc;
    unsigned inc, ph, mhz;
    int l0, l1, l2, ls0, ls1, ls2;

    mode = digiwrmz_mode ? 1 : 0;
    on = mode ? digigrainz_on : digiwrmz_on;
    mix_t = on ? bx_wet((mode ? digigrainz_mix : digiwrmz_mix) * 258) : 0;
    p = (int *)(DN_OUT_BASE + wz_off);

    if (!on && wz_mix == 0)
        return;                                 /* true bypass: no work off */

    if (mode != wz_lastmode) {                  /* mode change: drop stale state */
        wz_lp = 0;
        wz_dc[0] = wz_dc[1] = 0;
        wz_lastmode = mode;
    }

    /* WRMZ RAT / GRAINZ DEN: 0.1..3.0 Hz (milli-Hz) -- slower than before so
     * wow / drift stays a slow wobble, not a fast warble. */
    mhz = 100u + (unsigned)((mode ? digigrainz_dens : digiwrmz_rate) * 2900) / 127u;
    inc = bx_inc_mhz(mhz, DN_OUT_FRAMES);
    ph = wz_phase;
    fb = ((mode ? digigrainz_fb : digiwrmz_fb) * 26214) / 127;   /* cap 0.80 */
    lpc = 21000 + (digiwrmz_tone - 64) * 180;
    if (lpc < 800)
        lpc = 800;
    else if (lpc > 32700)
        lpc = 32700;

    mstep = bx_step(wz_mix, mix_t);
    l0 = bx_sin(ph);
    ls0 = (bx_sin(ph + inc) - l0) >> 5;
    l1 = bx_sin(ph + 0x55555555u);
    l2 = bx_sin(ph + 0xAAAAAAABu);
    ls1 = (bx_sin(ph + 0x55555555u + inc) - l1) >> 5;
    ls2 = (bx_sin(ph + 0xAAAAAAABu + inc) - l2) >> 5;

    bx_emac_enter();                        /* eMAC for the per-frame gains */

    /* The mode is block-constant, so the loop is specialised (no per-frame
     * branch, and the base/span divisions are hoisted out of the frame loop). */
    if (mode == 0) {                            /* WRMZ: modulated delay */
        /* reserve the modulation span (<=900) so base+span stays inside the
         * shorter line and never wraps past it */
        int base = 48 + (digiwrmz_time * ((int)WZ_MAXD - 900 - 48)) / 127;
        int span = (digiwrmz_mod * 900) / 127;
        for (i = 0; i < DN_OUT_FRAMES; i++) {
            int l = p[0], r = p[1];
            int mono = (l + r) >> 1;                /* 24-bit mono downmix */
            int dq = base * 4096 + ((span * l0) >> 3);
            int fr, wet;
            unsigned idx;
            if (dq < (2 * 4096))
                dq = 2 * 4096;
            idx = (wz_w - (unsigned)(dq >> 12)) & WZ_MASK;
            fr = dq & 0xfff;
            wet = span ? wz_rdi(wz_buf, idx, fr) : wz_rd(wz_buf, idx);

            p[0] = l + bx_mul15(wet - l, wz_mix);
            p[1] = r + bx_mul15(wet - r, wz_mix);
            wz_lp = bx_lp_e(wz_lp, wet, lpc);
            {
                int f = mono + bx_mul15(wz_lp, fb);
                f = bx_dc_e(wz_dc, f);
                wz_buf[wz_w] = wz_mk(f);
            }
            if (wz_w == 0)
                wz_buf[WZ_N] = wz_buf[0];
            wz_w = (wz_w + 1) & WZ_MASK;
            l0 += ls0;
            wz_mix += mstep;
            p += 2;
        }
    } else {                                    /* GRAINZ: three drifting taps */
        int base = 192 + (digigrainz_size * ((int)WZ_MAXD / 2 - 192)) / 127;
        int sp = 160 + (digigrainz_size * 1600) / 127;
        int span = 40 + (digigrainz_spray * 2200) / 127;
        for (i = 0; i < DN_OUT_FRAMES; i++) {
            int l = p[0], r = p[1];
            int mono = (l + r) >> 1;
            int d0 = base + ((span * l0) >> 15);
            int d1 = base + sp + ((span * l1) >> 15);
            int d2 = base + 2 * sp + ((span * l2) >> 15);
            int wet;
            if (d0 < 2) d0 = 2;
            if (d1 < 2) d1 = 2;
            if (d2 < 2) d2 = 2;
            /* /3 via the eMAC (sum of three 24-bit taps), Q15 1/3 = 10923 */
            wet = bx_mul15(wz_rd(wz_buf, (wz_w - (unsigned)d0) & WZ_MASK) +
                           wz_rd(wz_buf, (wz_w - (unsigned)d1) & WZ_MASK) +
                           wz_rd(wz_buf, (wz_w - (unsigned)d2) & WZ_MASK), 10923);

            p[0] = l + bx_mul15(wet - l, wz_mix);
            p[1] = r + bx_mul15(wet - r, wz_mix);
            wz_lp = bx_lp_e(wz_lp, wet, lpc);
            {
                int f = mono + bx_mul15(wz_lp, fb);
                f = bx_dc_e(wz_dc, f);
                wz_buf[wz_w] = wz_mk(f);
            }
            if (wz_w == 0)
                wz_buf[WZ_N] = wz_buf[0];
            wz_w = (wz_w + 1) & WZ_MASK;
            l0 += ls0;
            l1 += ls1;
            l2 += ls2;
            wz_mix += mstep;
            p += 2;
        }
    }

    bx_emac_leave();
    wz_phase = ph + inc;
}
