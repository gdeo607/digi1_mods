/* SPDX-License-Identifier: GPL-2.0-or-later
 * digictl: the master-page UI for the DIGI suite.
 * Digital Alchemy DSP's, for the original FM groovebox (and its Keys model), OS 1.43; ported to
 * the DT1 (OS 1.53 / 1.54) through corea.h.
 *
 * Six pages of our own, appended to the master page tree (view kinds 0..5),
 * rotated with LEFT / RIGHT (or the on-screen < > arrows):
 *
 *   RING  kind 0  E = depth, F = frequency; a centred ring animation, the
 *                 depth/frequency bars on the right and the values bottom-right
 *   FOLD  kind 1  A = on/off, D = fold type (CLEAN/MUD/DIST/TRSH, continuous),
 *                 E/F/G/H = amount; the spiral on the left, the big mode title
 *                 centred, the amount as a vertical slider next to the meter
 *   TILT  kind 2  A = on/off, E = low shelf, H = high shelf; a bent response
 *   CRUSH kind 3  A = on/off, D = mode (BITS/SR/CRUNCH), E = amount, F = tune
 *   PITCH kind 4  A = on/off, D = mode (WOB/SHFT), E = speed, F = depth
 *   STEREO kind 5 A = on/off, D = mode (FLAN/PAN/WIDE), E = speed, F = depth,
 *                 G = mix, H = feedback
 *   PERF  kind 6  the static performance page: no animation, one depth per FX
 *                 block on the encoders (A ring depth, B fold amount, C crush
 *                 amount, D wave pitch depth, E wave stereo depth, F EQ low,
 *                 G EQ high, H wave speed). Reached by pressing UP on any of the
 *                 six pages; UP again returns. The 16 trigs are momentary
 *                 performance pads, four blocks of four intensities (1 2 9 10
 *                 ring, 3 4 11 12 fold/crush, 5 6 13 14 pitch, 7 8 15 16 pan):
 *                 a held trig ramps its block's depth up (and turns it on),
 *                 holding at the intensity target, and restores the saved
 *                 pattern value on release. The store is never dirtied by the
 *                 pads, so the pattern value is the floor.
 *
 * The stock top status bar (rows 53..63) is left untouched; our drawing stays
 * below it. None of the stock pages are taken: the pages are reached by FUNC+LFO
 * or LEFT/RIGHT.
 *
 * The master view (vtable 0x40199BAC) keeps its page kinds in a vector at
 * view+124 (end +128, current index +144). We append kinds 0, 1 and 2 and patch
 * the view's draw (slot 4, 0x40199BBC).
 *
 * ---- DT1 port (OS 1.53 / 1.54) ----
 * The DT1's master pages (FUNC+LFO) are the same view class: vtable 0x401845dc, kinds at +124/+128,
 * index +144, stock kinds {11 COMP, 12 internal mixer, 13 external mixer}. Its kinds 1..10 are real
 * parameter pages (TRIG, SRC, ...) that the stock draw, keys and knobs would act on, and only kind 0
 * ("None") has no knobs, so the seven pages are seven kind-0 entries after the stock pages. From 3.2a4 they
 * are core 4.1's master pages (core_mpages): core puts them in the list, names them and calls dc_draw, so
 * Tone+FX combines with other master-page mods (Digi EQ); its audio is core's ev_render_master. Key ids are the DT1's (UP 14, LEFT 16, RIGHT 17,
 * trigs 24..39, FUNC held = flags bit 1); everything else - the effects, the pages, the PERFORM pads, the
 * store and the CC map - is the original's code. Where the settings live and the CC entry differ: see
 * those sections.
 */

/* ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ---- */
#ifdef OS154
#define F_400c178a 0x400c19b2
#define F_400c19a6 0x400c1bce
#define F_400c257c 0x400c27a4
#define F_400c9812 0x400c9a3a
#define F_400d4180 0x400d43a8
#define F_401845dc 0x401848dc
#define F_40200b0c 0x40200ebc
#define F_40200b20 0x40200ed0
#define F_40200c20 0x40200fd0
#define F_40200e20 0x402011d0
#define F_4191e018 0x4191f018
#define F_4197cf88 0x4197df88
#define F_4199dc44 0x4199ec44
#else
#define F_400c178a 0x400c178a
#define F_400c19a6 0x400c19a6
#define F_400c257c 0x400c257c
#define F_400c9812 0x400c9812
#define F_400d4180 0x400d4180
#define F_401845dc 0x401845dc
#define F_40200b0c 0x40200b0c
#define F_40200b20 0x40200b20
#define F_40200c20 0x40200c20
#define F_40200e20 0x40200e20
#define F_4191e018 0x4191e018
#define F_4197cf88 0x4197cf88
#define F_4199dc44 0x4199dc44
#endif
/* ---- end of the moved addresses ---- */


#include "corea.h"
#include "digitakt-mk1/core3.h"    /* core 4.1: struct core_mpage, core_mpage_shown, core_mview */
#include "sin256.h"

/* fillRect / frameRect: (bmp, x0, y0, x1, y1, colour), y = 0 the BOTTOM row; colour 0 clear, 1 set,
 * -1 invert (the DT1's; on the original they were in corea.h) */
#define DN_FILLRECT  ((void (*)(void *, int, int, int, int, int))F_400c19a6)
#define DN_FRAMERECT ((void (*)(void *, int, int, int, int, int))F_400c178a)
#define DN_TEXTF      ((void (*)(void *, const void *, int, int, int, const char *, ...))F_400c257c)
#define DN_FONT5      ((const void *)F_40200b0c)
#define DN_INVALIDATE ((void (*)(void *))F_400c9812)
/* operator new: the firmware returns the pointer in d0, where this toolchain (m68k-linux-gnu) reads a
 * returned pointer from a0, so it is declared as returning an integer */
#define DN_FXKIND     0
#define DN_FEKIND     1
#define DN_EQKIND     2
#define DN_CRKIND     3
#define DN_WVKIND     4            /* WAVE pitch page: WOB / SHFT */
#define DN_WSKIND     5            /* WAVE stereo page: FLAN / PAN / WIDE */
#define DN_PFKIND     6            /* PERF page: static depths + momentary trigs */
#define DN_NPAGES     7
#define DN_K_UP       14           /* the UP key id (DT1) */
#define DN_K_LEFT     16
#define DN_K_RIGHT    17
#define DN_TRIG1      24           /* trig key ids 24..39 = trigs 1..16 (DT1) */
#define DN_TRIG16     39
#define DN_KF_FUNC    0x02         /* key event flags: bit 1 = FUNC held (DT1) */
#ifndef DT_PER_STEP
#define DT_PER_STEP   16           /* DT1: encoder events carry 16 per notch (the original's 4) */
#endif
#define DC_PER_STEP   DT_PER_STEP
#define DG_TOP        53            /* rows 53..63 are the stock status bar */

/* the stock 5-px font tables (glyph widths, bitmap offsets, bitmap bytes) */
#define DN_FW  ((const unsigned char *)F_40200b20)
#define DN_FO  ((const unsigned short *)F_40200c20)
#define DN_FB  ((const unsigned char *)F_40200e20)

extern int digieq_on, digieq_lo, digieq_hi;
extern int digiring_on, digiring_depth, digiring_freq;
extern int digifold_on, digifold_amount, digifold_mode;
extern int digimeter_on, digimeter_l, digimeter_r;
extern int digicrush_on, digicrush_mode, digicrush_amount, digicrush_tune;
extern int digiwave_on, digiwave_pmode, digiwave_smode, digiwave_page;
extern int digiwave_speed, digiwave_depth, digiwave_mix, digiwave_fb;

/* Track-LFO bridge. A track LFO can target the track's FLTR FREQ; that live
 * value is in the per-voice record at 0x80003544 + (voice-1)*0x9E, FREQ at +0
 * (index<<8, fractional low byte). We read it every block and drive one of our
 * params from it, so a track LFO modulates us. */
int digimod_dest;                    /* 0 off, 1 ring dep, 2 ring frq, 3 fold,
                                        4 EQ low, 5 EQ high.
                                        Parked: no page readout. */
int digimod_voice = 1;               /* 1..8 */
/* DT1 port: the original's per-voice record has no counterpart mapped here yet, so the bridge
 * stays off on the DT1 (it is parked on the original too: nothing in the UI or the CC map sets
 * digimod_dest). The two params are still kept in the store, as on the original. */

/* ---- per-pattern storage -----------------------------------------------------
 * Our FX params have no stock field, so they are kept with the pattern. On the
 * original they live in a 64-byte block the four MIDI tracks leave unused in
 * the saved pattern struct ("DGX1" v7: magic, version, count, 24 x int16).
 *
 * DT1 port: each pattern has its own kit object (128 kits at 0x4191e018 +
 * pattern * 0x91e on OS 1.53, 4 KB further on 1.54; *0x4199dc44 is the current
 * pattern's), and kit + 0x530 .. 0x551 is 34 bytes the firmware neither uses nor
 * saves: zero in every kit (docs/TECHNICAL_NOTES.md, "What the +Drive
 * actually stores of a kit"). The same 24 params fit as one byte each (they are
 * all 0..127), so the block there is "DX" + version 7 + count 24 + 24 bytes =
 * 28 bytes. It follows a pattern switch, as on the original; it is RAM-only, so
 * the settings are kept while the unit is on but not over a power cycle or a
 * project load (a pattern with no block adopts the live values, as on the
 * original). The sync and the adopt guard are the original's. */
#define DT_UI_KIT     (*(unsigned char *volatile *)F_4199dc44)
#define DT_KIT0       ((unsigned)F_4191e018)
#define DT_KIT_STEP   0x91eu
#define DN_PAT_SLOTS  128u
#define DN_STORE_OFF  0x530u
#define DN_STORE_N    24
#define DN_STORE_VER  7
#define DN_STORE_LEN  (4 + DN_STORE_N)
#define DN_STORE_MAG(a) ((a)[0] == 0x44 && (a)[1] == 0x58 && \
                         (a)[2] == DN_STORE_VER && (a)[3] == DN_STORE_N)  /* "DX" v7 */

static int *const dn_fxparam[DN_STORE_N] = {
    &digiring_on, &digiring_depth, &digiring_freq,
    &digieq_on, &digieq_lo, &digieq_hi,
    &digifold_on, &digifold_amount, &digifold_mode,
    &digimeter_on, &digimod_dest, &digimod_voice,
    &digicrush_on, &digicrush_mode, &digicrush_amount, &digicrush_tune,
    &digiwave_on, &digiwave_pmode, &digiwave_smode, &digiwave_page,
    &digiwave_speed, &digiwave_depth, &digiwave_mix, &digiwave_fb,
};

static unsigned char *dn_store_blk;             /* block we last synced */
static unsigned char dn_store_copy[DN_STORE_LEN];
static int dn_store_dirty;                      /* a page edit is pending */

static unsigned char *dn_store_slot(void)
{
    unsigned p = (unsigned)DT_UI_KIT;
    if (p < DT_KIT0 || p >= DT_KIT0 + DN_PAT_SLOTS * DT_KIT_STEP ||
        (p - DT_KIT0) % DT_KIT_STEP)
        return 0;
    return (unsigned char *)(p + DN_STORE_OFF);
}

static void dn_store_read(const unsigned char *b)
{
    int i;
    for (i = 0; i < DN_STORE_N; i++)
        *dn_fxparam[i] = b[4 + i] & 0x7f;
}

static void dn_store_write(unsigned char *b)
{
    int i;
    b[0] = 0x44; b[1] = 0x58;                             /* "DX" */
    b[2] = DN_STORE_VER; b[3] = DN_STORE_N;
    for (i = 0; i < DN_STORE_N; i++)
        b[4 + i] = (unsigned char)(*dn_fxparam[i] & 0x7f);
}

/* Once a UI frame: follow a pattern switch, persist a page edit, and adopt an
 * external change (a pattern reload) the OS made behind us. */
static void dn_store_sync(void)
{
    unsigned char *b = dn_store_slot();
    int i;
    if (!b)
        return;
    if (b == dn_store_blk) {
        if (dn_store_dirty) {
            dn_store_write(b);
        } else {
            for (i = 0; i < DN_STORE_LEN; i++)
                if (b[i] != dn_store_copy[i])
                    break;
            if (i < DN_STORE_LEN) {          /* the block changed: only adopt
                                              * it if it is still ours. A stock
                                              * write here must NOT reach our
                                              * params -- reclaim the block. */
                if (DN_STORE_MAG(b))
                    dn_store_read(b);
                else
                    dn_store_write(b);
            }
        }
    } else {
        dn_store_blk = b;                    /* pattern switch / first sync */
        if (DN_STORE_MAG(b))
            dn_store_read(b);
        else
            dn_store_write(b);               /* new pattern: adopt live values */
    }
    dn_store_dirty = 0;
    for (i = 0; i < DN_STORE_LEN; i++)
        dn_store_copy[i] = b[i];
}

static int clampi(int v, int lo, int hi);

/* ---- MIDI CC control ---------------------------------------------------------
 * cc_glue.s hooks the incoming CC router (0x400ED94E) and calls this with the
 * CC number and value. Only CC numbers the stock DN table does not use are
 * ours; every other CC falls through to stock untouched. Values are 0..127;
 * on/off params switch at 64.
 *
 * DT1 port: cc_glue.s hooks the DT1's CC entry (0x400d4a90, once per
 * incoming CC message, before the channel is matched to a track), so a CC on
 * any channel drives the master FX, as on the original, while the unit's MIDI
 * CC receive setting is on. The numbers are the original's where the
 * DT1's own CC map leaves them free: 8, 11, 36, 37, 40, 41, 67, 68, 69
 * and 96 are free on the DT1 too (its CC tables, read from the running
 * firmware: tests/digiemu_tonefx.py), but 20..23 are the audio tracks' SRC
 * parameters and 24..31 the auto channel's FX, so CRUSH and WAVE moved up by
 * 22, to 42..53, which nothing on the DT1 uses. */
#define DN_CC_RING_ON    11
#define DN_CC_RING_DEPTH 8
#define DN_CC_RING_RATE  36
#define DN_CC_FOLD_ON    37
#define DN_CC_FOLD_AMT   40
#define DN_CC_FOLD_MODE  41
#define DN_CC_EQ_ON      67
#define DN_CC_EQ_LO      69
#define DN_CC_EQ_HI      96
#define DN_CC_METER_ON   68
#define DN_CC_CRU_ON    42
#define DN_CC_CRU_MODE  43
#define DN_CC_CRU_AMT   44
#define DN_CC_CRU_TUNE  45
#define DN_CC_WV_ON     46
#define DN_CC_WV_PMODE  47
#define DN_CC_WV_SMODE  52
#define DN_CC_WV_PAGE   53
#define DN_CC_WV_SPD    48
#define DN_CC_WV_DEP    49
#define DN_CC_WV_MIX    50
#define DN_CC_WV_FB     51

void digictl_cc_apply(int cc, int value)
{
    int v = clampi(value, 0, 127);
    switch (cc) {
    case DN_CC_RING_ON:    digiring_on = (v >= 64); break;
    case DN_CC_RING_DEPTH: digiring_depth = v; break;
    case DN_CC_RING_RATE:  digiring_freq = v; break;
    case DN_CC_FOLD_ON:    digifold_on = (v >= 64); break;
    case DN_CC_FOLD_AMT:   digifold_amount = v; break;
    case DN_CC_FOLD_MODE:  digifold_mode = v; break;
    case DN_CC_EQ_ON:      digieq_on = (v >= 64); break;
    case DN_CC_EQ_LO:      digieq_lo = v; break;
    case DN_CC_EQ_HI:      digieq_hi = v; break;
    case DN_CC_METER_ON:   digimeter_on = (v >= 64); break;
    case DN_CC_CRU_ON:     digicrush_on = (v >= 64); break;
    case DN_CC_CRU_MODE:   digicrush_mode = v; break;
    case DN_CC_CRU_AMT:    digicrush_amount = v; break;
    case DN_CC_CRU_TUNE:   digicrush_tune = v; break;
    case DN_CC_WV_ON:      digiwave_on = (v >= 64); break;
    case DN_CC_WV_PMODE:   digiwave_pmode = v; digiwave_page = 0; break;
    case DN_CC_WV_SMODE:   digiwave_smode = v; digiwave_page = 1; break;
    case DN_CC_WV_PAGE:    digiwave_page = (v >= 64); break;
    case DN_CC_WV_SPD:     digiwave_speed = v; break;
    case DN_CC_WV_DEP:     digiwave_depth = v; break;
    case DN_CC_WV_MIX:     digiwave_mix = v; break;
    case DN_CC_WV_FB:      digiwave_fb = v; break;
    default: return;                 /* not ours: stock handles/ignores it */
    }
    dn_store_dirty = 1;              /* persist with the pattern */
}

/* ---- PERFORM page: static depths + momentary trig pads -----------------------
 * The 16 trigs are performance pads in four blocks; each block has four pads of
 * increasing intensity. A held pad saves the block's depth (and on/off), turns
 * the block on, and ramps the depth up to the intensity target (base + 1/4..4/4
 * of the way to 127), where it holds. Releasing it ramps back to the saved
 * value and restores the on/off. This never touches the per-pattern store, so
 * the saved value is always the resting position.
 *
 * The ramp runs in ev_render_in (audio context, 1500 Hz), one cheap step per
 * block, and only while a pad or its release ramp is active -- with no pad held
 * it costs a single flag test. */
#define PF_N 4
static const unsigned char pf_trig_block[16] = {
    0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 1, 1, 2, 2, 3, 3
};
static const unsigned char pf_trig_inten[16] = {
    1, 2, 1, 2, 1, 2, 1, 2, 3, 4, 3, 4, 3, 4, 3, 4
};
/* block -> its params (index -1 = none) */
static const signed char pf_block_params[4][2] = {
    { 0, -1 },          /* ring: depth */
    { 1,  2 },          /* fold + crush: amount each */
    { 3, -1 },          /* pitch: wave depth (PITCH group) */
    { 3, -1 },          /* pan: wave depth (STEREO group) */
};
static int *const pf_param[PF_N] = {
    &digiring_depth, &digifold_amount, &digicrush_amount, &digiwave_depth
};
static int *const pf_onflag[PF_N] = {
    &digiring_on, &digifold_on, &digicrush_on, &digiwave_on
};

static volatile unsigned pf_holdmask[PF_N];     /* pads currently holding */
static volatile unsigned pf_taken;              /* trig presses we consumed */
static int pf_base[PF_N];                       /* saved value at first engage */
static int pf_cur[PF_N];                        /* ramped value */
static int pf_tgt[PF_N];                        /* target while holding */
static int pf_onbase[PF_N];                     /* saved on/off */
static signed char pf_active[PF_N];
static volatile int pf_running;                 /* any pad or release ramp live */

static void pf_trig_down(int t)
{
    int b = pf_trig_block[t], k = pf_trig_inten[t], i;
    pf_taken |= 1u << t;
    for (i = 0; i < 2; i++) {
        int p = pf_block_params[b][i], tgt;
        if (p < 0)
            break;
        if (!pf_holdmask[p]) {                  /* first pad on this param */
            pf_base[p] = *pf_param[p];
            pf_onbase[p] = *pf_onflag[p];
            pf_cur[p] = *pf_param[p];
            pf_tgt[p] = *pf_param[p];
        }
        pf_holdmask[p] |= (1u << t);
        *pf_onflag[p] = 1;
        tgt = pf_base[p] + (127 - pf_base[p]) * k / 4;
        if (tgt > pf_tgt[p])
            pf_tgt[p] = tgt;
        pf_active[p] = 1;
    }
    if (b == 2)
        digiwave_page = 0;                      /* pitch pad uses the PITCH group */
    else if (b == 3)
        digiwave_page = 1;                      /* pan pad uses the STEREO group */
    pf_running = 1;
}

static void pf_trig_up(int t)
{
    int b = pf_trig_block[t], i;
    pf_taken &= ~(1u << t);
    for (i = 0; i < 2; i++) {
        int p = pf_block_params[b][i];
        if (p < 0)
            break;
        pf_holdmask[p] &= ~(1u << t);
        if (!pf_holdmask[p])
            pf_tgt[p] = pf_base[p];             /* release: ramp back down */
    }
}

/* ev_render_in: the momentary ramp, before any insert reads its params. */
static void pf_apply(void)
{
    int p;
    if (!pf_running)
        return;
    pf_running = 0;
    for (p = 0; p < PF_N; p++) {
        int t, d;
        if (!pf_active[p])
            continue;
        t = pf_holdmask[p] ? pf_tgt[p] : pf_base[p];
        d = t - pf_cur[p];
        if (d > -32 && d < 32)
            pf_cur[p] = t;
        else
            pf_cur[p] += d >> 5;
        *pf_param[p] = pf_cur[p];
        if (!pf_holdmask[p] && pf_cur[p] == pf_base[p]) {
            pf_active[p] = 0;                   /* settled back: let go */
            *pf_onflag[p] = pf_onbase[p];
        } else {
            pf_running = 1;
        }
    }
}


#define DN_MASTER_VT  F_401845dc     /* the DT1's master view */
static char *dc_view;               /* the master view, from its last draw */
static int dc_vis;                  /* frames since the master view was drawn */
static int dc_perf_prev;            /* page index to return to from PERF */
static int dc_eacc[10];             /* per-encoder accumulation (ids 0..9), as
                                     * the OS does -- sized for id 9 too */
static volatile unsigned dc_out;    /* output half, for the ceiling stage */
static unsigned dc_mods;            /* DT1: modifier keys (ids 1..8) held */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ev_render_in: apply the track-FLTR-FREQ bridge before the master inserts run
 * (they read the params in this block's ev_render_out), so modulation is a
 * block-rate update, smooth at 1500 Hz. */
void digictl_mod_in(void)
{
    int idx, d = digimod_dest, v = digimod_voice;
    dc_out = dn_out_off();               /* the half this block writes */
    pf_apply();                          /* PERFORM-page momentary pads */
    /* The track-LFO bridge is parked: it only drives a param when explicitly
     * enabled (digimod_dest 1..5). A stock OS event never sets it. */
    if (v < 1 || v > 8 || d < 1 || d > 5)
        return;
    return;                              /* DT1: no FREQ record source (see above) */
    idx = 0;
    switch (d) {
    case 1: digiring_depth = idx; break;
    case 2: digiring_freq = idx; break;
    case 3: digifold_amount = idx; break;
    case 4: digieq_lo = idx; break;
    case 5: digieq_hi = idx; break;
    default: break;
    }
}

/* Output ceiling (last stage, order 64): keep the master mix below the 24-bit
 * full scale so a hot flanger-feedback / pitch-shift / drive setting can never
 * reach hard clipping. Soft knee at ~-2.5 dBFS, asymptotic max ~-0.7 dBFS, and
 * an absolute clamp well below 0x7fffff. Transparent for normal levels. */
#define DN_LIM_KNEE 0x600000
#define DN_LIM_MAX  0x740000
static inline int dn_lmt(int x)
{
    int s = x < 0 ? -1 : 1;
    int a = x < 0 ? -x : x;
    if (a > DN_LIM_KNEE) {
        a = DN_LIM_KNEE + ((a - DN_LIM_KNEE) >> 1);
        if (a > DN_LIM_MAX) a = DN_LIM_MAX;
    }
    return s * a;
}

void digictl_limit_out(void)
{
    int *p = (int *)(DN_OUT_BASE + dc_out);
    int i;
    for (i = 0; i < DN_OUT_FRAMES; i++) {
        p[0] = dn_lmt(p[0]);
        p[1] = dn_lmt(p[1]);
        p += 2;
    }
}

/* Alive (its primary vtable is still ours) AND drawn recently. */
static int master_live(char *v)
{
    return dc_vis > 0 && v && *(int *)v == DN_MASTER_VT;
}

/* DT1 port: our seven pages are seven of core's master pages (core 4.1, core_mpages: mod.json contributes
 * dc_pages[0..6]), after the stock pages. Core puts them in the master view's list (as kind-0 entries, the
 * firmware's unused "None" page), names them for the title bar and pop-up, and draws them through dc_draw. */
static void dc_draw(void *view, void *bmp);
struct core_mpage dc_pages[DN_NPAGES] = {
    {13, "RING",   "DIGI RING",   dc_draw, 0, 0, 0, 0},
    {13, "FOLD",   "DIGI FOLD",   dc_draw, 0, 0, 0, 0},
    {13, "TILT",   "DIGI TILT",   dc_draw, 0, 0, 0, 0},
    {13, "CRUSH",  "DIGI CRUSH",  dc_draw, 0, 0, 0, 0},
    {13, "PITCH",  "DIGI PITCH",  dc_draw, 0, 0, 0, 0},
    {13, "STEREO", "DIGI STEREO", dc_draw, 0, 0, 0, 0},
    {13, "PERF",   "PERFORM",     dc_draw, 0, 0, 0, 0},
};

/* the page shown: 0..6 (DN_FXKIND .. DN_PFKIND) for ours, -1 for any other */
static int cur_kind(char *view)
{
    struct core_mpage *p = core_mpage_shown(view);
    if (p < dc_pages || p >= dc_pages + DN_NPAGES)
        return -1;
    return p - dc_pages;
}

/* the index of our page `k` in the master vector, or -1 */
static int dc_find_kind(char *view, int k)
{
    return dc_pages[k].view == view ? dc_pages[k].pos : -1;
}

/* DT1: a page change as the stock page key makes one (the title popup "DIGI RING (4/10)"), page_glue.s */
void dt_page_show(void *view);

/* ---------------- small text helpers (stock 5-px font, optional 2x) ---------- */
static int dg_strw(const char *s, int sc)
{
    int w = 0;
    for (; *s; s++)
        w += (DN_FW[(unsigned char)*s] + 1) * sc;
    return w;
}

static void dg_glyph(void *bmp, int x, int ytop, int ch, int sc)
{
    int w = DN_FW[ch & 0xff];
    int off = DN_FO[ch & 0xff];
    int c, r;
    for (c = 0; c < w; c++) {
        int bits = DN_FB[off + c];
        for (r = 0; r < 8; r++) {
            if (bits & (1 << r)) {              /* bit 0 = top of the glyph */
                int px = x + c * sc;
                int py = ytop - r * sc;
                DN_FILLRECT(bmp, px, py, px + sc - 1, py - sc + 1, 1);
            }
        }
    }
}

static void dg_text(void *bmp, int x, int ytop, const char *s, int sc)
{
    for (; *s; s++) {
        dg_glyph(bmp, x, ytop, (unsigned char)*s, sc);
        x += (DN_FW[(unsigned char)*s] + 1) * sc;
    }
}

/* the page title, centred in the bottom third */
static void dg_title(void *bmp, const char *s)
{
    DN_TEXTF(bmp, DN_FONT5, (128 - dg_strw(s, 1)) / 2, 12, -1, "%s", s);
}

/* the little < > page arrows, centred vertically on each side */
static void dg_arrows(void *bmp)
{
    dg_glyph(bmp, 3, 30, '<', 1);
    dg_glyph(bmp, 122, 30, '>', 1);
}

/* ---------------- RING: animation + value bars ---------------- */
static unsigned dg_rng = 0x2545f491u;
static int dg_ang;

static int dg_rand(int m)
{
    dg_rng = dg_rng * 1103515245u + 12345u;
    return (int)((dg_rng >> 16) & 0x7fff) % (m > 0 ? m : 1);
}

static void dg_ring(void *bmp)
{
    int cx = 60, cy = 35, R = 14, i, n = 24;
    int dep = digiring_on ? digiring_depth * 258 : 0;   /* 0..127 -> Q15 */
    for (i = 0; i < n; i++) {
        int a = (i * 256 / n) & 0xff;
        int s = sin256[a], c = sin256[(a + 64) & 0xff];
        int x = cx + ((c * R) >> 15), y = cy + ((s * R) >> 15);
        DN_FILLRECT(bmp, x, y, x, y, 1);
    }
    if (dep > 0) {
        int s = sin256[dg_ang & 0xff], c = sin256[(dg_ang + 64) & 0xff];
        int amp = (dep * 13) >> 15;
        int jx = dg_rand(2 * amp + 1) - amp, jy = dg_rand(2 * amp + 1) - amp;
        int x = cx + ((c * R) >> 15) + jx, y = cy + ((s * R) >> 15) + jy;
        DN_FILLRECT(bmp, x - 1, y - 1, x + 1, y + 1, 1);
        dg_ang = (dg_ang + (digiring_freq / 8 + 1)) & 0xff;
    }
}

static void dg_vbar(void *bmp, int x0, int x1, int num, int den)
{
    int h = 42, y0 = 8, f;
    if (den <= 0)
        return;
    f = num * h / den;
    if (f < 0) f = 0;
    if (f > h) f = h;
    DN_FILLRECT(bmp, x0, y0, x1, y0 + h, 0);
    DN_FILLRECT(bmp, x0, y0, x1, y0 + f, 1);
}

static void dg_fx_page(void *bmp)
{
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_ring(bmp);
    dg_vbar(bmp, 104, 106, digiring_on ? digiring_depth : 0, 127);
    dg_vbar(bmp, 110, 112, digiring_freq, 127);
    DN_TEXTF(bmp, DN_FONT5, 2, 4, -1, "DEP %d FRQ %d", digiring_depth, digiring_freq);
    dg_title(bmp, "RING");
}

/* ---------------- FOLD: spiral + big mode + amount slider + meter ------------ */

/* the fold amount as a spiral: a straight line at 0, coiling as it folds */
static void dg_spiral(void *bmp, int amount)
{
    int cx = 20, cy = 30, i, n = 44, turns, rmax;
    if (amount <= 0) {                  /* straight when not folding */
        DN_FILLRECT(bmp, cx - 14, cy, cx + 14, cy, 1);
        return;
    }
    turns = 1 + amount * 3 / 128;       /* 1..3 turns */
    rmax = 7 + amount * 9 / 128;        /* 7..16 radius */
    for (i = 0; i < n; i++) {
        int t = i * 256 * turns / n;
        int r = 3 + (rmax - 3) * i / n;
        int a = t & 0xff;
        int s = sin256[a], c = sin256[(a + 64) & 0xff];
        int x = cx + ((c * r) >> 15), y = cy + ((s * r) >> 15);
        DN_FILLRECT(bmp, x, y, x, y, 1);
    }
}

static const char *dg_modename(int m)
{
    static const char *const n[4] = { "CLEAN", "MUD", "DIST", "TRSH" };
    return n[m & 3];
}

static void dg_slider(void *bmp, int x, int val, int vmax)
{
    int y0 = 10, y1 = 44, pos;
    DN_FRAMERECT(bmp, x - 2, y0 - 1, x + 2, y1 + 1, 1);
    val = clampi(val, 0, vmax);
    pos = y0 + val * (y1 - y0) / vmax;
    DN_FILLRECT(bmp, x - 1, y0, x + 1, pos, 1);
}

static void dg_fold_page(void *bmp)
{
    int band = digifold_mode / 32;
    const char *m;
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_spiral(bmp, digifold_on ? digifold_amount : 0);   /* left */
    if (band > 3) band = 3;
    m = dg_modename(band);
    dg_text(bmp, (128 - dg_strw(m, 2)) / 2, 36, m, 2);   /* big, centred */
    dg_slider(bmp, 94, digifold_on ? digifold_amount : 0, 127);
    dg_vbar(bmp, 106, 108, digimeter_l, 60);
    dg_vbar(bmp, 113, 115, digimeter_r, 60);
    DN_TEXTF(bmp, DN_FONT5, 89, 3, -1, "%d", digifold_amount);
    dg_title(bmp, "FOLD");
}

/* ---------------- TILT: a bent low/high shelf curve ---------------- */

static int dg_eq_y(int x)
{
    int lo = 30 + (digieq_lo - 64) / 3;
    int hi = 30 + (digieq_hi - 64) / 3;
    int y;
    if (!digieq_on)
        return 30;
    if (x < 24)
        y = lo;
    else if (x < 48)
        y = lo + (30 - lo) * (x - 24) / 24;
    else if (x < 80)
        y = 30;
    else if (x < 104)
        y = 30 + (hi - 30) * (x - 80) / 24;
    else
        y = hi;
    return clampi(y, 8, 50);
}

static void dg_eq_page(void *bmp)
{
    int x;
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    for (x = 8; x <= 120; x += 2)           /* 0 dB reference */
        DN_FILLRECT(bmp, x, 30, x, 30, 1);
    for (x = 8; x <= 120; x++)
        DN_FILLRECT(bmp, x, dg_eq_y(x), x, dg_eq_y(x), 1);
    DN_TEXTF(bmp, DN_FONT5, 4, 6, -1, "LO %d", digieq_lo);
    DN_TEXTF(bmp, DN_FONT5, 96, 6, -1, "HI %d", digieq_hi);
    dg_title(bmp, "TILT");
}

/* ---------------- CRUSH: big stage name + amount/tune bars ---------------- */
static const char *dg_crushname(int m)
{
    static const char *const n[3] = { "BITS", "SR", "CRUNCH" };
    int b = m * 3 / 128;
    if (b > 2) b = 2;
    return n[b];
}

/* left third, HORIZONTAL: collapses to a flat line at amount 0; the scroll
 * waveform gets quantised (BITS) / held (SR) / clipped (CRUNCH) and taller as
 * amount rises; tune adds dither specks. No frame. */
static int dg_cr_ph;
static void dg_crush_gfx(void *bmp)
{
    int mode = digicrush_mode * 3 / 128;
    int amt = digicrush_on ? digicrush_amount : 0;
    int cy = 26, x;
    if (mode > 2) mode = 2;
    dg_cr_ph += 1 + (amt >> 4);
    for (x = 4; x <= 40; x++) {
        int amp = (amt * 12) / 127;                     /* 0 -> a line */
        int px = (x - 4 + dg_cr_ph) & 0x1f;
        int v = (sin256[(px << 3) & 0xff] * amp) >> 15;
        int y;
        if (mode == 0 && amt > 0) {                     /* BITS: quantise */
            int step = 1 + (amt * 8) / 127;
            v = (v / step) * step;
        } else if (mode == 1 && amt > 0) {              /* SR: sample-hold */
            int hold = 1 + amt / 20;
            v = (v / hold) * hold;
        } else if (mode == 2 && amt > 0) {              /* CRUNCH: clip */
            int lim = amp - (amt * (amp - 1)) / 127;
            if (v > lim) v = lim;
            if (v < -lim) v = -lim;
        }
        y = cy + v;
        DN_FILLRECT(bmp, x, y, x, y, 1);
        if (mode == 0 && digicrush_tune > 0 && dg_rand(48) < digicrush_tune / 4)
            DN_FILLRECT(bmp, x, cy - 10, x, cy + 10, 1);  /* dither specks */
    }
}

static void dg_crush_page(void *bmp)
{
    const char *m = dg_crushname(digicrush_mode);
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_crush_gfx(bmp);
    dg_text(bmp, 44 + (82 - dg_strw(m, 2)) / 2, 36, m, 2);
    DN_TEXTF(bmp, DN_FONT5, 2, 4, -1, "AMT %d TUN %d",
             digicrush_amount, digicrush_tune);
    dg_title(bmp, "CRUSH");
}

/* ---------------- WAVE: two pages over one engine ----------------
 * kind 4 PITCH (WOB / SHFT), kind 5 STEREO (FLAN / PAN / WIDE). Both edit the
 * same digiwave_mode; the D encoder cycles within the page's group, so a page
 * only selects its own modes. The big title shows the engine's current mode. */
static const char *dg_pname(void)          /* PITCH page's own mode */
{
    static const char *const n[2] = { "WOB", "SHFT" };
    int b = (digiwave_pmode * 2) >> 7;
    return n[b > 1 ? 1 : b];
}

static const char *dg_sname(void)          /* STEREO page's own mode */
{
    static const char *const n[3] = { "FLAN", "PAN", "WIDE" };
    int b = (digiwave_smode * 3) >> 7;
    return n[b > 2 ? 2 : b];
}

/* left third, VERTICAL: two lines run top to bottom. They stay straight at
 * depth 0 and, as depth (amplitude) and speed (nodes) rise, wobble and
 * intertwine (cross) at more points. WOB and SHFT share it. No frame. */
static int dg_pi_ph;
static void dg_pitch_gfx(void *bmp)
{
    int dep = digiwave_depth, spd = digiwave_speed;
    int c1 = 15, c2 = 29;                           /* the two lines */
    int amp = (dep * 14) / 127;                     /* 0 -> straight */
    int k = 1 + (spd * 5) / 127;                    /* nodes 1..6 */
    int y;
    dg_pi_ph += 2 + (spd >> 3);
    for (y = 2; y <= 51; y++) {
        int idx = ((y * k * 256) / 52 + dg_pi_ph) & 0xff;
        int s = (sin256[idx] * amp) >> 15;
        int x1 = c1 + s;
        int x2 = c2 - s;                            /* opposite -> cross */
        DN_FILLRECT(bmp, x1, y, x1, y, 1);
        DN_FILLRECT(bmp, x2, y, x2, y, 1);
    }
}

/* left third, HORIZONTAL: collapses to one line at depth 0. FLAN = a comb
 * ripple (amp = depth, wavelength = speed); PAN = two lines moving apart
 * oppositely; WIDE = two lines spreading. No frame. */
static int dg_st_ph;
static void dg_stereo_gfx(void *bmp)
{
    int mode = digiwave_smode * 3 / 128;
    int dep = digiwave_depth, spd = digiwave_speed;
    int cy = 26, x;
    if (mode > 2) mode = 2;
    dg_st_ph += 2 + (spd >> 3);
    if (mode == 2) {                                    /* WIDE */
        int spread = (dep * 10) / 127;                  /* 0 -> one line */
        for (x = 4; x <= 40; x++) {
            DN_FILLRECT(bmp, x, cy - spread, x, cy - spread, 1);
            DN_FILLRECT(bmp, x, cy + spread, x, cy + spread, 1);
        }
    } else if (mode == 1) {                             /* PAN */
        int amp = (dep * 10) / 127;
        int s = (sin256[dg_st_ph & 0xff] * amp) >> 15;
        for (x = 4; x <= 40; x++) {
            DN_FILLRECT(bmp, x, cy + s, x, cy + s, 1);
            DN_FILLRECT(bmp, x, cy - s, x, cy - s, 1);
        }
    } else {                                            /* FLAN */
        int amp = (dep * 10) / 127;
        int wl = 6 + ((127 - spd) * 16) / 127;
        for (x = 4; x <= 40; x++) {
            int idx = ((x * 256 / wl) + dg_st_ph) & 0xff;
            int y = cy - ((sin256[idx] * amp) >> 15);
            DN_FILLRECT(bmp, x, y, x, y, 1);
        }
    }
}

static void dg_wave_bars(void *bmp, const char *m, const char *title, int which)
{
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    if (which == 0)
        dg_pitch_gfx(bmp);
    else
        dg_stereo_gfx(bmp);
    dg_text(bmp, 44 + (82 - dg_strw(m, 2)) / 2, 36, m, 2);
    DN_TEXTF(bmp, DN_FONT5, which == 0 ? 44 : 2, 4, -1, "SPD %d DEP %d",
             digiwave_speed, digiwave_depth);
    dg_title(bmp, title);
}

static void dg_wave_page(void *bmp)  { dg_wave_bars(bmp, dg_pname(), "PITCH", 0); }
static void dg_wave2_page(void *bmp) { dg_wave_bars(bmp, dg_sname(), "STEREO", 1); }

/* ---------------- PERF: the static performance page (no animation) -----------
 * One depth readout per block on the eight encoders, and the trig-pad legend.
 * Nothing here animates: the same pixels every frame, so the page adds no
 * per-frame work beyond the text draw. */
static const char *const dg_pflbl[8] = {
    "A RNG", "B FLD", "C CRS", "D PIT", "E STE", "F LO", "G HI", "H SPD"
};
static int *const dg_pfval[8] = {
    &digiring_depth, &digifold_amount, &digicrush_amount, &digiwave_depth,
    &digiwave_depth, &digieq_lo, &digieq_hi, &digiwave_speed
};

static void dg_perf_page(void *bmp)
{
    int i;
    DN_FILLRECT(bmp, 0, 0, 127, DG_TOP - 1, 0);
    DN_TEXTF(bmp, DN_FONT5, (128 - dg_strw("PERFORM", 1)) / 2, 45, -1, "PERFORM");
    for (i = 0; i < 8; i++) {
        int x = (i / 4) ? 66 : 4;
        int y = 34 - (i % 4) * 10;
        DN_TEXTF(bmp, DN_FONT5, x, y, -1, "%s %d",
                 dg_pflbl[i], *dg_pfval[i]);
    }
}

/* our pages' drawing (core calls it after the stock draw) */
static void dc_draw(void *view, void *bmp)
{
    int k;
    dc_view = (char *)view;
    dc_vis = 4;
    k = cur_kind(dc_view);
    if (k == DN_FXKIND)
        dg_fx_page(bmp);
    else if (k == DN_FEKIND)
        dg_fold_page(bmp);
    else if (k == DN_EQKIND)
        dg_eq_page(bmp);
    else if (k == DN_CRKIND)
        dg_crush_page(bmp);
    else if (k == DN_WVKIND)
        dg_wave_page(bmp);
    else if (k == DN_WSKIND)
        dg_wave2_page(bmp);
    else if (k == DN_PFKIND)
        dg_perf_page(bmp);
}

/* every UI frame: keep the master view redrawing while it is shown, so the
 * animation, the meter and LEFT/RIGHT navigation always keep working */
void digictl_tick(void *ctrl)
{
    static uint32_t seen;
    (void)ctrl;
    dn_store_sync();
    if (core_mview_draws != seen) {          /* core drew the master view (any of its pages) */
        seen = core_mview_draws;
        dc_view = (char *)core_mview;
        dc_vis = 4;
    }
    if (dc_vis)
        dc_vis--;
    if (dc_vis > 0 && dc_view && *(int *)dc_view == DN_MASTER_VT)
        DN_INVALIDATE(dc_view);
}

/* Stock convention: every parameter is 0..127, so one step is one unit. The
 * original panel sends 4 wire counts per encoder detent and the stock 0..127
 * params move one step per detent, so one step is 4 counts. */
static int dc_steps(int id, int d)
{
    int st;
    if ((d > 0 && dc_eacc[id] < 0) || (d < 0 && dc_eacc[id] > 0))
        dc_eacc[id] = 0;
    dc_eacc[id] += d;
    st = dc_eacc[id] / DC_PER_STEP;
    dc_eacc[id] -= st * DC_PER_STEP;
    return st;
}

int digictl_enc(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int delta = *(int *)((char *)ev + 16);
    int k, d;
    (void)brain;
    if (id < 0 || id > 9)
        return 0;                        /* not one of our encoders: pass on */
    if (!master_live(dc_view))
        return 0;
    k = cur_kind(dc_view);
    if (k != DN_FXKIND && k != DN_FEKIND && k != DN_EQKIND &&
        k != DN_CRKIND && k != DN_WVKIND && k != DN_WSKIND &&
        k != DN_PFKIND)
        return 0;
    if (!delta)
        return 1;
    d = dc_steps(id, delta);
    if (!d)
        return 1;

    if (k == DN_FEKIND) {
        switch (id) {
        case 1: digifold_on = (d > 0); break;
        case 4: digifold_mode = clampi(digifold_mode + d, 0, 127); break;
        case 5: case 6: case 7: case 8:     /* E/F/G/H all edit the amount */
            digifold_amount = clampi(digifold_amount + d, 0, 127); break;
        case 9: digimeter_on = (d > 0); break;
        default: break;
        }
    } else if (k == DN_EQKIND) {
        switch (id) {
        case 1: digieq_on = (d > 0); break;
        case 5: digieq_lo = clampi(digieq_lo + d, 0, 127); break;
        case 8: digieq_hi = clampi(digieq_hi + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_CRKIND) {
        switch (id) {
        case 1: digicrush_on = (d > 0); break;
        case 4: digicrush_mode = clampi(digicrush_mode + d, 0, 127); break;
        case 5: digicrush_amount = clampi(digicrush_amount + d, 0, 127); break;
        case 6: digicrush_tune = clampi(digicrush_tune + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_WVKIND) {             /* PITCH: WOB / SHFT */
        digiwave_page = 0;                   /* editing this page activates it */
        switch (id) {
        case 1: digiwave_on = (d > 0); break;
        case 4: {
            int b = (digiwave_pmode * 2) >> 7;
            if (b > 1) b = 1;
            b = clampi(b + d, 0, 1);
            digiwave_pmode = b * 64 + 32;
        } break;
        case 5: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        case 6: digiwave_depth = clampi(digiwave_depth + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_WSKIND) {             /* STEREO: FLAN / PAN / WIDE */
        digiwave_page = 1;
        switch (id) {
        case 1: digiwave_on = (d > 0); break;
        case 4: {
            int b = (digiwave_smode * 3) >> 7;
            if (b > 2) b = 2;
            b = clampi(b + d, 0, 2);
            digiwave_smode = b * 43 + 21;
        } break;
        case 5: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        case 6: digiwave_depth = clampi(digiwave_depth + d, 0, 127); break;
        case 7: digiwave_mix = clampi(digiwave_mix + d, 0, 127); break;
        case 8: digiwave_fb = clampi(digiwave_fb + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_PFKIND) {         /* PERF: one depth per block */
        switch (id) {
        case 1: digiring_depth = clampi(digiring_depth + d, 0, 127); break;
        case 2: digifold_amount = clampi(digifold_amount + d, 0, 127); break;
        case 3: digicrush_amount = clampi(digicrush_amount + d, 0, 127); break;
        case 4: digiwave_page = 0;        /* D: the PITCH group's depth */
                digiwave_depth = clampi(digiwave_depth + d, 0, 127); break;
        case 5: digiwave_page = 1;        /* E: the STEREO group's depth */
                digiwave_depth = clampi(digiwave_depth + d, 0, 127); break;
        case 6: digieq_lo = clampi(digieq_lo + d, 0, 127); break;
        case 7: digieq_hi = clampi(digieq_hi + d, 0, 127); break;
        case 8: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        default: break;
        }
    } else {                             /* DN_FXKIND */
        switch (id) {
        case 1: digiring_on = (d > 0); break;
        case 5: digiring_depth = clampi(digiring_depth + d, 0, 127); break;
        case 6: digiring_freq = clampi(digiring_freq + d, 0, 127); break;
        case 9: digimeter_on = (d > 0); break;
        default: break;
        }
    }
    dn_store_dirty = 1;
    DN_INVALIDATE(dc_view);
    return 1;
}

/* LEFT / RIGHT rotate the whole master page vector (stock pages included). */
int digictl_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int flags = *(int *)((char *)ev + 16);
    int k, *v, n, idx;
    (void)brain;
    /* DT1 port: the keys that make a trig mean something else while held (FUNC, TRK, PTN, BANK,
     * SONG, GLOBAL, SAMPLE, TEMPO: ids 1..8) are followed here, so PTN + trig still changes the pattern
     * and FUNC + trig still does what it does, on the PERFORM page too. */
    if (id >= 1 && id <= 8) {               /* their release has flags 0 (a trig's has 0x10) */
        if (flags & 1)
            dc_mods |= 1u << id;
        else
            dc_mods &= ~(1u << id);
    }
    if (!master_live(dc_view))
        return 0;
    k = cur_kind(dc_view);

    /* The trigs: momentary pads while the PERFORM page is up. A press we took
     * is always releases on the way up, even if the page changed first. */
    if (id >= DN_TRIG1 && id <= DN_TRIG16) {
        int t = id - DN_TRIG1;
        if (flags & 1) {
            if (flags & 8)
                return (pf_taken & (1u << t)) ? 1 : 0;   /* swallow repeats */
            if (k == DN_PFKIND && !(flags & DN_KF_FUNC) && !dc_mods) {
                pf_trig_down(t);
                DN_INVALIDATE(dc_view);
                return 1;
            }
            return 0;
        }
        if (pf_taken & (1u << t)) {                 /* our release */
            pf_trig_up(t);
            if (k == DN_PFKIND)
                DN_INVALIDATE(dc_view);
            return 1;
        }
        return 0;
    }

    /* UP: from any DIGI page to the PERFORM page, and back. */
    if (id == DN_K_UP && (flags & 1) && !(flags & DN_KF_FUNC) && !(flags & 8)) {
        if (k >= DN_FXKIND && k <= DN_WSKIND) {
            idx = dc_find_kind(dc_view, DN_PFKIND);
            if (idx >= 0) {
                dc_perf_prev = *(int *)(dc_view + 144);
                *(int *)(dc_view + 144) = idx;
                dt_page_show(dc_view);         /* DT1: the stock page change */
                return 1;
            }
        } else if (k == DN_PFKIND) {
            v = *(int **)(dc_view + 124);
            n = v ? (int)((*(int **)(dc_view + 128)) - v) : 0;
            idx = dc_perf_prev;
            if (idx < 0 || idx >= n)
                idx = 0;
            *(int *)(dc_view + 144) = idx;
            dt_page_show(dc_view);         /* DT1: the stock page change */
            return 1;
        }
    }

    if (!(flags & 1) || (flags & DN_KF_FUNC) || (flags & 8))
        return 0;                               /* ignore key-repeat */
    if (id != DN_K_LEFT && id != DN_K_RIGHT)    /* LEFT / RIGHT */
        return 0;
    v = *(int **)(dc_view + 124);
    n = (int)((*(int **)(dc_view + 128)) - v);
    if (!v || n <= 0)
        return 0;
    idx = *(int *)(dc_view + 144);
    if (idx < 0 || idx >= n)
        idx = 0;
    idx = (idx + (id == DN_K_RIGHT ? 1 : n - 1)) % n;
    *(int *)(dc_view + 144) = idx;
    dt_page_show(dc_view);         /* DT1: the stock page change */
    return 1;
}
