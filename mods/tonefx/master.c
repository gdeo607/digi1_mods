/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tone+FX, DT1 port: the effect chain on the master mix, so the main outputs, the headphones
 * and the USB stream all carry it (see corea.h). Called by master_glue.s at 0x400721e6 with the block.
 *
 * The master words are 8 bits hotter than the 24-bit samples the effects were written for (the render
 * takes the outputs as the EMAC's sum >> 8), so the chain runs on block >> 8 and only what it changed is
 * put back, shifted up: with every effect off (or at its neutral) the master is left exactly as it was,
 * low bits included. The order is the original's ev_render_out order.
 */
#include "corea.h"
#include "emac.h"

int tfx_blk[64];

void digiring_render_out(void);
void digieq_render_out(void);
void digicrush_render_out(void);
void digifold_render_out(void);
void digiwave_render_out(void);
void digiwrmz_render_out(void);
void digictl_limit_out(void);
void digimeter_render_out(void);

void tonefx_master(int *m)
{
    int i;
    unsigned macsr;
    for (i = 0; i < 64; i++)
        tfx_blk[i] = m[i] >> 8;
    /* eMAC block scope: flip to fractional mode once for the whole chain (the
     * effects are built with BX_EMAC_HOISTED, so they do not switch it themselves)
     * and restore the render's MACSR before returning. The stock render keeps its
     * own EMAC elsewhere; only our stage calls run in this window. */
    macsr = bx_macsr_save();
    bx_macsr_frac();
    digiring_render_out();      /* 50 */
    digieq_render_out();        /* 55 */
    digicrush_render_out();     /* 57 */
    digifold_render_out();      /* 60 */
    digiwave_render_out();      /* 62 */
    digiwrmz_render_out();      /* 63 */
    digictl_limit_out();        /* 64 */
    digimeter_render_out();     /* 65 */
    bx_macsr_restore(macsr);
    for (i = 0; i < 64; i++) {
        int d = tfx_blk[i] - (m[i] >> 8);
        if (d)
            m[i] += d << 8;
    }
}
