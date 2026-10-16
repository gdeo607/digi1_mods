/* SPDX-License-Identifier: GPL-2.0-or-later
 * digimeter: output level metering for the DIGI FX master page.
 * Digitone mk1 / Keys, OS 1.43.
 *
 * ev_render_in remembers which output half the block writes; ev_render_out
 * scans that half for L/R peak (log scale); ev_tick decays a peak-hold. The
 * two bar lengths are exported (`digimeter_l/r`, 0..60) for digictl to draw on
 * its page -- this mod does NOT draw over the screen itself. Combines with
 * every mod.
 */
#include "corea.h"

/* runtime parameters (exported; digictl toggles/reads them) */
int digimeter_on = 1;
int digimeter_live = 1;            /* reserved: 1 so the meter always runs */
int digimeter_l, digimeter_r;      /* 0..60 bars, read by digictl's page */

/* digictl exposes when the FOLD page (the only meter reader) is up; a weak
 * reference keeps this mod linkable on its own (undefined -> always run). */
extern int digictl_meter_active(void) __attribute__((weak));

static volatile unsigned dm_off;
static volatile int dm_peak_l, dm_peak_r;
static int dm_hold_l, dm_hold_r;

/* peak -> 0..60 on a log scale (floor(log2(peak)) over a 31-bit sample).
 * ISA-C ff1 gives the leading-zero count in one instruction instead of the
 * shift loop: floor(log2(x)) = 31 - ff1(x). */
static int dm_bar(int peak)
{
    int b;
    if (peak <= 1)
        return 0;
#if defined(__mcoldfire__)
    b = peak;
    __asm__ volatile("ff1.l %0" : "+d"(b));
    b = 31 - b;
#else
    b = 0;
    while (peak > 1) { peak >>= 1; b++; }
#endif
    b = b * 60 / 31;
    return b > 60 ? 60 : b;
}

void digimeter_render_in(void)
{
    dm_off = dn_out_off();
}

void digimeter_render_out(void)
{
    const int *p;
    int i, pl = 0, pr = 0;
    if (!digimeter_on) return;
    if (digictl_meter_active && !digictl_meter_active()) return;  /* not on FOLD */
    p = (const int *)(DN_OUT_BASE + dm_off);
    for (i = 0; i < DN_OUT_FRAMES; i++) {
        int l = p[0], r = p[1];
        p += 2;
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > pl) pl = l;
        if (r > pr) pr = r;
    }
    dm_peak_l = dm_bar(pl);
    dm_peak_r = dm_bar(pr);
}

void digimeter_tick(void *ctrl)
{
    int l = dm_peak_l, r = dm_peak_r;
    (void)ctrl;
    if (!digimeter_on) {
        dm_hold_l = dm_hold_r = 0;
        digimeter_l = digimeter_r = 0;
        return;
    }
    if (l > dm_hold_l) dm_hold_l = l; else if (dm_hold_l > 0) dm_hold_l--;
    if (r > dm_hold_r) dm_hold_r = r; else if (dm_hold_r > 0) dm_hold_r--;
    digimeter_l = dm_hold_l;
    digimeter_r = dm_hold_r;
}
