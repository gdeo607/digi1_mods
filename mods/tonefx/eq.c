/* SPDX-License-Identifier: GPL-2.0-or-later
 * digieq: a master low-shelf / high-shelf EQ (one knob each).
 * Digitone mk1 / Keys, OS 1.43.
 *
 * A one-pole split into low and high bands, each with its own 0..127 gain
 * (centre 64 = flat). The two gains are interpolated across the 32 frames of
 * each block, so a knob move (or a bypass) is a smooth ramp, not a step: no
 * zipper, no click. Bypass ramps both gains to zero, which is passthrough.
 * The low knob bends the low shelf, the high knob the high shelf.
 * Subscriptions only: it combines with every mod.
 */
#include "corea.h"
#include "emac.h"

#ifndef DIGIEQ_CUT
#define DIGIEQ_CUT 4            /* one-pole: lo += (x-lo)>>CUT (~1 kHz) */
#endif

/* runtime parameters (exported; digictl edits them).
 * 0..127, centre 64 = flat (stock convention: one step per notch). The DSP
 * scales each to a Q14 gain delta: (v - 64) * 128. */
int digieq_on = 1;
int digieq_lo = 64;
int digieq_hi = 64;

static int eq_lo_l, eq_lo_r;
static int eq_low_cur, eq_high_cur;     /* values at the last block's start */
static volatile unsigned eq_off;

void digieq_render_in(void)
{
    eq_off = dn_out_off();
}

void digieq_render_out(void)
{
    int lt = digieq_on ? (digieq_lo - 64) * 128 : 0;   /* 0..127 -> Q14 */
    int ht = digieq_on ? (digieq_hi - 64) * 128 : 0;
    int lstep, hstep, i, lc, hc, lo_l, lo_r;
    int *p;
    if (lt == eq_low_cur && ht == eq_high_cur && lt == 0 && ht == 0)
        return;                                 /* fully bypassed and settled */
    p = (int *)(DN_OUT_BASE + eq_off);
    lstep = (lt - eq_low_cur) >> 5;             /* reach the target over the block */
    hstep = (ht - eq_high_cur) >> 5;
    lc = eq_low_cur;
    hc = eq_high_cur;
    lo_l = eq_lo_l;
    lo_r = eq_lo_r;
    bx_emac_enter();                            /* band gains on the eMAC */
    for (i = 0; i < DN_OUT_FRAMES; i++) {
        int l = p[0], r = p[1], hl, hr;
        lo_l += (l - lo_l) >> DIGIEQ_CUT;
        lo_r += (r - lo_r) >> DIGIEQ_CUT;
        hl = l - lo_l;
        hr = r - lo_r;
        /* Full-precision gain (bx_mul14, not dn_q14d): the truncated form
         * quantized the bands in steps of the delta, which is the low-level
         * noise the EQ had. */
        p[0] = l + bx_mul14(lo_l, lc) + bx_mul14(hl, hc);
        p[1] = r + bx_mul14(lo_r, lc) + bx_mul14(hr, hc);
        p += 2;
        lc += lstep;
        hc += hstep;
    }
    bx_emac_leave();
    eq_low_cur = lt;
    eq_high_cur = ht;
    eq_lo_l = lo_l;
    eq_lo_r = lo_r;
}
