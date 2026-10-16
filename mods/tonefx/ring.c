/* SPDX-License-Identifier: GPL-2.0-or-later
 * digiring: a master ring modulator / tremolo insert.
 * Digitone mk1 / Keys, OS 1.43.
 *
 * Multiplies the output by a sine carrier. Depth and carrier frequency are
 * interpolated across each block's 32 frames, so edits and bypass ramp
 * smoothly (no zipper on depth, no pitch step on frequency). Bypass ramps the
 * depth to zero, which is passthrough. Combines with every mod.
 */
#include "corea.h"
#include "emac.h"
#include "sin256.h"

/* runtime parameters (exported; digictl edits them).
 * Stock convention: 0..127, one step per notch. The DSP scales them. */
int digiring_on = 1;
int digiring_depth = 51;        /* 0..127 wet amount (51 -> Q15 ~0.40) */
int digiring_freq = 6;          /* 0..127 carrier rate (6 -> ~91 Hz) */

/* 2^32 / 48000, so inc = freq * DN_RING_K advances an 8-bit-index phase */
#define DN_RING_K 89478

static volatile unsigned di_off;
static unsigned di_phase;
static int di_depth_cur, di_freq_cur;

void digiring_render_in(void)
{
    di_off = dn_out_off();
}

void digiring_render_out(void)
{
    int dt = digiring_on ? digiring_depth * 258 : 0;    /* 0..127 -> Q15 */
    int ft = 1 + digiring_freq * 15;                    /* 0..127 -> Hz */
    int i, dc, inc, dstep, fstep;
    unsigned ph = di_phase;
    int *p;
    if (dt == di_depth_cur && ft == di_freq_cur && dt == 0)
        return;
    dstep = (dt - di_depth_cur) >> 5;
    fstep = (ft - di_freq_cur) >> 5;
    dc = di_depth_cur;
    inc = di_freq_cur * DN_RING_K;
    p = (int *)(DN_OUT_BASE + di_off);
    bx_emac_enter();                         /* carrier + gains on the eMAC */
    for (i = 0; i < DN_OUT_FRAMES; i++) {
        unsigned hi = ph >> 24;                  /* 0..255 */
        int fr = (int)((ph >> 16) & 0xFFu);      /* 8-bit fraction */
        int s0 = sin256[hi & 0xFFu];
        int s1 = sin256[(hi + 1) & 0xFFu];
        int s = s0 + bx_mul8(s1 - s0, fr);       /* interpolated carrier */
        int l = p[0], r = p[1];
        /* Full-precision multiply: the truncated form quantized the wet signal
         * in steps of the Q15 carrier (~-36 dB), the ring's noise. */
        p[0] = l + bx_mul15(bx_mul15(l, s) - l, dc);
        p[1] = r + bx_mul15(bx_mul15(r, s) - r, dc);
        p += 2;
        ph += (unsigned)inc;
        dc += dstep;
        inc += fstep * DN_RING_K;
    }
    bx_emac_leave();
    di_depth_cur = dt;
    di_freq_cur = ft;
    di_phase = ph;
}
