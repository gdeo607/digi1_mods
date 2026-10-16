/* SPDX-License-Identifier: GPL-2.0-or-later
 * digicrush: master Crunch / Redux insert (bit-reduce, sample-rate hold, drive).
 * Digitone mk1 / Keys, OS 1.43.
 *
 * Three stateless/near-stateless lo-fi stages on the output half:
 *   BITS    quantise to N bits (a mask), with optional TPDF dither (tune)
 *   SR      sample-and-hold every `ratio` samples (a decimator)
 *   CRUNCH  a Q8 gain into a hard clip (a cheap saturation)
 *
 * Everything is computed once per block; the inner loop is straight-line
 * integer code (no 64-bit multiply, no library call). ev_render_in remembers
 * the output half, ev_render_out processes it in place. Subscriptions only:
 * it combines with every mod.
 */
#include "corea.h"
#include "emac.h"

/* runtime parameters (exported; digictl edits them). 0..127, one step/notch. */
int digicrush_on = 0;
int digicrush_mode = 0;         /* 0..127 -> BITS / SR / CRUNCH */
int digicrush_amount = 0;       /* depth of the selected stage */
int digicrush_tune = 0;         /* dither mix (BITS) / extra ratio (SR) */

#define DC_MODE_BITS 0
#define DC_MODE_SR   1
#define DC_MODE_DRV  2

static volatile unsigned dc_off;

static int dc_mode;             /* 0..2 */
static int dc_mask;             /* BITS: bit mask */
static int dc_dither;           /* BITS: 0/1 */
static int dc_ratio;            /* SR: hold length 1..64 */
static unsigned dc_gain;        /* DRV: Q8 gain, 256 = 1.0x */

/* state */
static int dc_hold_l, dc_hold_r;
static int dc_cnt;
static int dc_amt_cur;              /* ramped amount: OFF fades to 0, then skips */
static unsigned dc_lfsr = 0x2545f491u;

void digicrush_render_in(void)
{
    dc_off = dn_out_off();
}

/* x*g/2^8, full precision (g is Q8, x any int32) -- on the eMAC (bx_q8) */
static inline int dc_q8(int x, unsigned g)
{
    return bx_q8(x, (int)g);
}

static inline int dc_clip(int v)
{
    if (v > 0x7fffff) return 0x7fffff;          /* 24-bit left-justified */
    if (v < -0x800000) return -0x800000;
    return v;
}

void digicrush_render_out(void)
{
    int *p, i, amt, amt_t;
    unsigned lfsr;

    amt_t = digicrush_on ? digicrush_amount : 0;
    if (amt_t == 0 && dc_amt_cur == 0)
        return;                                  /* true bypass: settled, off */
    dc_amt_cur += (amt_t - dc_amt_cur) >> 5;     /* click-free fade */
    if (dc_amt_cur == 0)
        return;
    amt = dc_amt_cur;

    dc_mode = (digicrush_mode * 3) >> 7;
    if (dc_mode > DC_MODE_DRV)
        dc_mode = DC_MODE_DRV;

    if (dc_mode == DC_MODE_BITS) {
        int bits = 24 - (amt * 20 >> 7);                    /* 24..4 (24 = clean) */
        if (bits > 24) bits = 24;
        if (bits < 4) bits = 4;
        dc_mask = (int)(0xffffffffu << (32 - bits));
        dc_dither = digicrush_tune > 0;
    } else if (dc_mode == DC_MODE_SR) {
        dc_ratio = 1 + (amt * 63 >> 7) + (digicrush_tune * 63 >> 7);
        if (dc_ratio > 64) dc_ratio = 64;
    } else {
        dc_gain = 256u + (unsigned)(amt * 6);               /* 1x..~4x Q8 */
    }

    p = (int *)(DN_OUT_BASE + dc_off);
    lfsr = dc_lfsr;

    if (dc_mode == DC_MODE_BITS) {
        int mask = dc_mask;
        if (dc_dither) {                       /* dither is block-constant: split
                                                * the loop so the branch is not
                                                * tested 32x per block */
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int l = p[0], r = p[1];
                lfsr = lfsr * 1664525u + 1013904223u;
                l += (int)(lfsr >> 26) - (int)(lfsr >> 27);
                lfsr = lfsr * 1664525u + 1013904223u;
                r += (int)(lfsr >> 26) - (int)(lfsr >> 27);
                p[0] = l & mask;
                p[1] = r & mask;
                p += 2;
            }
        } else {
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                p[0] &= mask;
                p[1] &= mask;
                p += 2;
            }
        }
        dc_lfsr = lfsr;
    } else if (dc_mode == DC_MODE_SR) {
        int cnt = dc_cnt, hl = dc_hold_l, hr = dc_hold_r, ratio = dc_ratio;
        for (i = 0; i < DN_OUT_FRAMES; i++) {
            if (cnt <= 0) { hl = p[0]; hr = p[1]; cnt = ratio; }
            cnt--;
            p[0] = hl;
            p[1] = hr;
            p += 2;
        }
        dc_cnt = cnt;
        dc_hold_l = hl;
        dc_hold_r = hr;
    } else {
        unsigned g = dc_gain;
        bx_emac_enter();                        /* the Q8 drive rides the eMAC */
        for (i = 0; i < DN_OUT_FRAMES; i++) {
            p[0] = dc_clip(dc_q8(p[0], g));
            p[1] = dc_clip(dc_q8(p[1], g));
            p += 2;
        }
        bx_emac_leave();
    }
}
