/* SPDX-License-Identifier: GPL-2.0-or-later
 * emac.h -- ToneFX-BufferFX eMAC helpers (ColdFire MCF5441x eMAC).
 *
 * The MCF5441x has an eMAC (four accumulators, MAC/MSAC, movclr). GCC does NOT
 * emit EMAC instructions, so these primitives are inline asm. The core saves
 * and restores the whole EMAC around the audio render, so a handler may use it,
 * but must leave MACSR as the core found it: call bx_macsr_save() before and
 * bx_macsr_restore() after a block of MACs, with bx_macsr_frac() to select the
 * mode. ACC0 is left zeroed (movclr clears it).
 *
 * Fractional mode (MACSR = 0x20: signed, F/I set, R/T clear => truncating) the
 * eMAC evaluates
 *     movclr.l( mac.l Y,X )  ==  (Y*X) >> 31
 * exactly: the product is <<1 (binary-point alignment), kept to its upper 40
 * bits, and the store reads ACC[39:8], which is (Y*X) << 1 >> 32 = (Y*X) >> 31.
 * So with the multiplicand X = c << 16,
 *     (x * (c<<16)) >> 31  ==  (x*c) >> 15
 * which is exactly dn_mul15, and X = c << 19 gives (x*c) >> 12 (bx_mul12).
 * Verified bit-exact against the C forms over the full input domain, incl.
 * negatives and truncation boundaries (patched-Unicorn CFV4E EMAC model).
 */
#ifndef TONEFX_EMAC_H
#define TONEFX_EMAC_H

#define BX_MACSR_FRAC 0x20u     /* signed, fractional (F/I), truncating (R/T=0) */

/* BX_NO_EMAC forces the scalar C forms even on the ColdFire: a build knob for
 * the §3 hardware A/B. The C forms are the exact (x*c)>>k, so the audio is
 * bit-identical; only the EMAC mode switch and MAC latency go away. Build the
 * suite with -DBX_NO_EMAC=1 to test whether the eMAC itself (single muls /
 * MACSR stalls) is the idle cost. */
#if defined(__mcoldfire__) && !defined(BX_NO_EMAC)

/* Move to/from MACSR: once per block, not per sample. */
static inline unsigned bx_macsr_save(void)
{
    unsigned v;
    __asm__ volatile("move.l %%macsr, %0" : "=d"(v));
    return v;
}

static inline void bx_macsr_restore(unsigned v)
{
    __asm__ volatile("move.l %0, %%macsr" :: "d"(v) : "cc", "memory");
}

static inline void bx_macsr_frac(void)
{
    /* select fractional mode and zero all four accumulators (the x4 helper
     * assumes they start at 0; the single helpers movclr before their mac) */
    __asm__ volatile(
        "move.l %0, %%macsr\n\t"
        "movclr.l %%acc0, %0\n\t"
        "movclr.l %%acc1, %0\n\t"
        "movclr.l %%acc2, %0\n\t"
        "movclr.l %%acc3, %0\n\t"
        :: "d"(BX_MACSR_FRAC) : "cc", "memory");
}

/* Per-block EMAC scope. A standalone module saves/sets/restores MACSR around its
 * own MACs (the discipline below). The fused suite defines BX_EMAC_HOISTED and
 * lets the master subscription own the switch: digictl_macsr_begin (ev_render_out
 * order 49) saves + selects fractional mode once, digictl_macsr_end (order 74)
 * restores once, and every handler in between calls these as no-ops. The stock
 * render (which has the EMAC set up for its own use) runs before the
 * ev_render_out list, so it never sees the fractional mode. */
#if defined(BX_EMAC_HOISTED)
static inline void bx_emac_enter(void) { }
static inline void bx_emac_leave(void) { }
#else
static unsigned bx_emac_saved;
static inline void bx_emac_enter(void) { bx_emac_saved = bx_macsr_save(); bx_macsr_frac(); }
static inline void bx_emac_leave(void) { bx_macsr_restore(bx_emac_saved); }
#endif

/* The single-MAC helpers assume ACC0 is 0: bx_macsr_frac zeroes ACC0-3 at the
 * block start and every helper's closing movclr leaves ACC0 at 0, so the leading
 * "clear ACC0" is redundant -- two instructions per multiply, not three. */

/* (x*c) >> 15, c Q15, any int32 x. One mac.l instead of two muls + shifts. */
static inline int bx_mul15(int x, int c)
{
    int r;
    int c16 = (int)((unsigned)c << 16);
    __asm__ volatile(
        "mac.l %1, %2, %%acc0\n\t"
        "movclr.l %%acc0, %0\n\t"
        : "=&d"(r) : "d"(x), "d"(c16));
    return r;
}

/* (x*c) >> 12, c Q12. */
static inline int bx_mul12(int x, int c)
{
    int r;
    int c19 = (int)((unsigned)c << 19);
    __asm__ volatile(
        "mac.l %1, %2, %%acc0\n\t"
        "movclr.l %%acc0, %0\n\t"
        : "=&d"(r) : "d"(x), "d"(c19));
    return r;
}

/* (x*c) >> 14, c Q14 (|c| <= 16383). */
static inline int bx_mul14(int x, int c)
{
    int r;
    int c17 = (int)((unsigned)c << 17);
    __asm__ volatile(
        "mac.l %1, %2, %%acc0\n\t"
        "movclr.l %%acc0, %0\n\t"
        : "=&d"(r) : "d"(x), "d"(c17));
    return r;
}

/* (x*c) >> 8, c Q8 (|c| <= 255). */
static inline int bx_mul8(int x, int c)
{
    int r;
    int c23 = (int)((unsigned)c << 23);
    __asm__ volatile(
        "mac.l %1, %2, %%acc0\n\t"
        "movclr.l %%acc0, %0\n\t"
        : "=&d"(r) : "d"(x), "d"(c23));
    return r;
}

/* (x*c) >> 14 for c up to ~32767 (WIDE's d*256), x within +/-2^28: the shift
 * budget goes on x instead of c (c<<15 would be the Q14 form's limit). */
static inline int bx_mul14w(int x, int c)
{
    int r;
    int x2 = x << 2;
    int c15 = (int)((unsigned)c << 15);
    __asm__ volatile(
        "mac.l %1, %2, %%acc0\n\t"
        "movclr.l %%acc0, %0\n\t"
        : "=&d"(r) : "d"(x2), "d"(c15));
    return r;
}

/* (x*g) >> 8 for a Q8 gain up to ~2047 (fold/crush drive). g = high byte + a
 * sub-256 fraction; the fraction rides bx_mul8, so it is exact with one MAC. */
static inline int bx_q8(int x, int g)
{
    return x * (g >> 8) + bx_mul8(x, g & 255);
}

/* Two (x*c)>>15 with a common c, on ACC0/ACC1, so the two MACs issue
 * back-to-back instead of the serial movclr->mac->movclr chain. Assumes ACC0/1
 * are 0 (cleared by bx_macsr_frac); leaves them 0. o[0],o[1] = results.
 * (The 68k has only D0-D7, so a 4-wide form needs more registers than exist.) */
static inline void bx_mul15x2(int x0, int x1, int c, int o[2])
{
    int r0, r1;
    int k = (int)((unsigned)c << 16);
    __asm__ volatile(
        "mac.l %2, %4, %%acc0\n\t"
        "mac.l %3, %4, %%acc1\n\t"
        "movclr.l %%acc0, %0\n\t"
        "movclr.l %%acc1, %1\n\t"
        : "=&d"(r0), "=&d"(r1) : "d"(x0), "d"(x1), "d"(k) : "cc");
    o[0] = r0; o[1] = r1;
}

/* eMAC one-pole / DC-blocker (the bx_lp / bx_dc in buffx.h, on the MAC). */
static inline int bx_lp_e(int s, int x, int c)  { return s + bx_mul15(x - s, c); }

static inline int bx_dc_e(int *st, int x)
{
    int y = x - st[0] + bx_mul15(st[1], 32604);
    st[0] = x;
    st[1] = y;
    return y;
}

#else /* host, or a ColdFire build with BX_NO_EMAC: scalar fallbacks (identical
       * (x*c)>>k, so the audio matches and the tests still build) */

static inline unsigned bx_macsr_save(void)     { return 0u; }
static inline void bx_macsr_restore(unsigned v) { (void)v; }
static inline void bx_macsr_frac(void)          { }
static inline void bx_emac_enter(void)          { }
static inline void bx_emac_leave(void)           { }
static inline int bx_mul15(int x, int c) { return (x >> 15) * c + (((x & 0x7fff) * c) >> 15); }
static inline int bx_mul12(int x, int c) { return (x >> 12) * c + (((x & 0xfff) * c) >> 12); }
static inline int bx_mul14(int x, int c) { return (x >> 14) * c + (((x & 0x3fff) * c) >> 14); }
static inline int bx_mul8(int x, int c)  { return (x >> 8)  * c + (((x & 0xff)   * c) >> 8);  }
static inline int bx_mul14w(int x, int c) { return (x >> 14) * c + (((x & 0x3fff) * c) >> 14); }
static inline int bx_q8(int x, int g)     { return (x >> 8) * g + (((x & 0xff) * g) >> 8); }
static inline void bx_mul15x2(int x0, int x1, int c, int o[2])
{
    o[0] = bx_mul15(x0, c); o[1] = bx_mul15(x1, c);
}
static inline int bx_lp_e(int s, int x, int c) { return s + bx_mul15(x - s, c); }
static inline int bx_dc_e(int *st, int x)
{
    int y = x - st[0] + bx_mul15(st[1], 32604);
    st[0] = x;
    st[1] = y;
    return y;
}

#endif
#endif
