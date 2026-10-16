/* SPDX-License-Identifier: GPL-2.0-or-later
 * buffx.h -- shared buffer-FX primitives for the WRMZ / Grainz / Tailz / Cloudz /
 * Loopz family. Integer only, no libc, no 64-bit multiply (see corea.h).
 *
 * These are the same idioms the suite already uses (digiwave's interpolated
 * clamped read and block-rate LFO, digieq's one-pole, digifold's saturator), kept
 * in one header so the buffer modules share engines instead of copying them.
 *
 * Conventions: the master bus is int32, 24-bit range (a sample is within
 * +/-(1<<23)). A buffer line stores s16; convert in with `>> 8` and out with
 * `<< 8`. A ring line is a power of two, plus one guard sample mirroring index 0
 * so a fractional read at idx+1 never needs a second mask.
 */
#ifndef TONEFX_BUFFX_H
#define TONEFX_BUFFX_H

#include "corea.h"
#include "emac.h"
#include "sin256.h"

/* Interpolated read of a 24-bit ring: `idx` is the integer sample index
 * (already masked, in 0..N-1), `frac` the Q12 fraction, and `line` has N + 1
 * entries (the +1 a guard mirroring index 0, so a read at idx+1 never needs a
 * second mask). Keeping idx and frac apart avoids overflowing a Q16.16 position
 * for a long ring. The interpolation is bx_mul12 on the eMAC (a 24-bit delta
 * times a Q12 fraction is a 36-bit product). */
static inline int bx_read_at(const int *line, unsigned idx, int frac)
{
    int a = line[idx];
    return a + bx_mul12(line[idx + 1] - a, frac);
}

/* Wet-mix ceiling (Q15, 0.75): the mix sliders map 0..127 to 0..~1.0, but the
 * value is capped here so 100% never fully removes the dry. The dry stays in
 * the blend, so a hot wet (feedback delay / flanger, wide-detune grains) is not
 * exposed on its own -- the "distortion at max mix" tipping point. */
#define BX_WET_MAX 24575
static inline int bx_wet(int v)
{
    return v > BX_WET_MAX ? BX_WET_MAX : v;
}

/* A ramp step that always reaches its target: (t-c)>>5 stalls at small gaps. */
static inline int bx_step(int cur, int tgt)
{
    int s = (tgt - cur) >> 5;
    if (s == 0 && cur != tgt)
        s = (tgt > cur) ? 1 : -1;
    return s;
}

/* One-pole low-pass, coefficient c (Q15); s is the state, returns the new state. */
static inline int bx_lp(int s, int x, int c)
{
    return s + dn_mul15(x - s, c);
}

/* DC blocker: y = x - x1 + R*y1 (R ~ 0.995 => 32604 Q15). State in a 2-int pair
 * {xp, yp}; returns the blocked value. */
static inline int bx_dc(int *st, int x)
{
    int y = x - st[0] + dn_mul15(st[1], 32604);
    st[0] = x;
    st[1] = y;
    return y;
}

/* s16 clamp. */
static inline int bx_sat16(int v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}

/* Soft clip to +/-T: linear below the knee (3T/4), half slope from there to a
 * hard stop at T. One knee, so a hot feedback sum compresses instead of
 * hard-clipping (the s16 delay line / FLAN feedback distortion at max MIX).
 * A few shifts/compares, no divide. |x| = INT_MIN is not a concern here. */
static inline int bx_soft(int x, int T)
{
    int s = x < 0 ? -1 : 1;
    int a = x < 0 ? -x : x;
    int k = T - (T >> 2);                   /* knee at 3T/4 */
    if (a <= k)
        return x;
    a = k + ((a - k) >> 1);
    if (a > T)
        a = T;
    return s * a;
}

/* A drive (Q8 gain, 256 = 1.0) into an s16 clip: the digifold/digicrush idiom. */
static inline int bx_drive16(int v, int g)
{
    return bx_sat16((v * g) >> 8);
}

/* One sine sample (Q15) at a Q32 phase (digiwave's wv_sin). */
static inline int bx_sin(unsigned ph)
{
    unsigned hi = ph >> 24;
    int fr = (int)((ph >> 16) & 0xffu);
    int s0 = sin256[hi & 0xffu];
    int s1 = sin256[(hi + 1) & 0xffu];
    return s0 + (((s1 - s0) * fr) >> 8);
}

/* Q32 phase advance for a whole block: hz * (2^32/48000) * frames. */
#define BX_K 89478u
static inline unsigned bx_inc(unsigned hz, unsigned frames)
{
    return hz * BX_K * frames;
}

/* Same, for a sub-Hz rate given in milli-Hz (0.1 Hz = 100). uint32 safe to
 * 5 kHz: 5000 * 89478 = 4.5e8, /1000, *frames <= ~1.4e7. */
static inline unsigned bx_inc_mhz(unsigned mhz, unsigned frames)
{
    return (mhz * BX_K / 1000u) * frames;
}

#endif
