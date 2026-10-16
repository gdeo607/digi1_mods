/* SPDX-License-Identifier: GPL-2.0-or-later
 * digictl: the master-page UI for the DIGI suite.
 * Digital Alchemy DSP's, for the original FM groovebox (and its Keys model), OS 1.43; ported to
 * the DT1 (OS 1.53 / 1.54) through corea.h.
 *
 * Seven pages of our own, appended to the master page tree (view kinds 0..6),
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
 *                 engine on the encoders (A ring depth, B fold amount, C crush
 *                 amount, D wave pitch depth, E wave stereo depth, F EQ low,
 *                 G EQ high, H wave speed). Reached by pressing UP on any of the
 *                 six pages; UP again returns. The 16 trigs are momentary
 *                 performance pads, now one block per engine (1 2 9 10 ring,
 *                 3 4 fold, 11 12 crush, 5 6 13 14 pitch, 7 8 15 16 stereo):
 *                 a held trig ramps its engine's depth up (and turns it on),
 *                 holding at the intensity target, and restores the saved
 *                 pattern value on release. The store is never dirtied by the
 *                 pads, so the pattern value is the floor. The WAVE engine has
 *                 its own PITCH and STEREO depths, so the two pads never fight.
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
 * ("None") has no knobs, so the seven pages are seven kind-0 entries inserted after the stock pages, told
 * apart by their place in the vector (dc_page). Before the stock draw of one of them, kind 0's names (the
 * layout table 0x4197cf88) are set to that page's. Key ids are the DT1's (UP 14, LEFT 16, RIGHT 17,
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
#define DN_OP_NEW(n)  ((int *)((unsigned (*)(unsigned))F_400d4180)(n))
#define DN_STOCK_DRAW ((void (*)(void *, void *))0x400395dc)
#define DN_PAGENAMES  ((const char **)F_4197cf88)   /* kind 0's short and long names (layout table) */
#define DN_FXKIND     0
#define DN_FEKIND     1
#define DN_EQKIND     2
#define DN_CRKIND     3
#define DN_WVKIND     4            /* WAVE pitch page: WOB / SHFT */
#define DN_WSKIND     5            /* WAVE stereo page: FLAN / PAN / WIDE */
#define DN_PFKIND     6            /* PERF page: static depths + momentary trigs */
#define DN_WASHKIND   7            /* macro pages: one slider + a per-page graphic */
#define DN_RADKIND    8
#define DN_DESKIND    9
#define DN_SPCKIND   10
#define DN_DLYKIND   11            /* DELAYS: WRMZ / GRAINZ, one page, D switches */
#define DN_NPAGES    12
#define DN_TREE1_N   7             /* FX pages in the main scroll (tree 1) */
#define DN_K_UP       14           /* the UP key id (DT1) */
#define DN_K_DOWN     15           /* the DOWN key id (DT1) */
#define DN_K_LEFT     16
#define DN_K_RIGHT    17
#define DN_TRIG1      24           /* trig key ids 24..39 = trigs 1..16 (DT1) */
#define DN_TRIG16     39
#define DN_KF_FUNC    0x02         /* key event flags: bit 1 = FUNC held (DT1) */
#define DT_MK_COMP    11           /* the DT1's stock master-FX page kinds (COMP, int/ext mixer);
                                    * only these and our pages belong in the master cycle */
#define DT_MK_INTMIX  12
#define DT_MK_EXTMIX  13
#define DC_PER_STEP   1            /* the DT1 panel sends ~1 count per detent, so one step per
                                    * notch. (4, the Digitone's, was never validated on a DT1
                                    * unit -- it moved every knob 4x too slowly. Raise if a unit
                                    * feels too fast.) */
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
extern int digiwave_on, digiwave_son, digiwave_pmode, digiwave_smode, digiwave_page;
extern int digiwave_speed, digiwave_pdepth, digiwave_sdepth;
extern int digiwave_mix, digiwave_fb, digiwave_pmix;
extern int digiwrmz_on, digiwrmz_time, digiwrmz_fb, digiwrmz_tone;
extern int digiwrmz_mod, digiwrmz_rate, digiwrmz_mix, digiwrmz_mode;
extern int digigrainz_on, digigrainz_dens, digigrainz_size, digigrainz_pitch;
extern int digigrainz_spray, digigrainz_fb, digigrainz_mix;

/* MACRO page state (owned here, stored per pattern). WASH = reverb send +
 * a WAVE wobble: while it is on, it drives the WAVE engine into WOB and turns
 * the depth up with the macro amount (the WAVE pages' settings are saved and
 * restored around it). Reverb is a stock write, wired when its address lands. */
int digiwash_on = 0;
int digiwash_amount = 64;
/* RADIO / DESTROY / SPACE macro state (test build: .bss, not in the store yet) */
int dgmac2_on = 0, dgmac2_amount = 64;   /* RADIO   */
int dgmac3_on = 0, dgmac3_amount = 64;   /* DESTROY */
int dgmac4_on = 0, dgmac4_amount = 64;   /* SPACE   */

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
/* DT1 port: 3.5r has 35 params; the kit's RAM-only spare is 34 bytes
 * (kit + 0x530 .. 0x551). They are stored 7 bits each, packed, after a 3-byte
 * header ("DX" + version): ceil(35 * 7 / 8) = 31 bytes, 34 in all. Everything
 * (including the four macros and the WAVE dry/wet) stays per-pattern. */
#define DN_STORE_OFF  0x530u
#define DN_STORE_N    48
#define DN_STORE_VER  9
#define DN_STORE_LEN  34
#define DN_STORE_MAG(a) ((a)[0] == 0x44 && (a)[1] == 0x58 && \
                         (a)[2] == DN_STORE_VER)               /* "DX" v9 */

static int *const dn_fxparam[DN_STORE_N] = {
    &digiring_on, &digiring_depth, &digiring_freq,
    &digieq_on, &digieq_lo, &digieq_hi,
    &digifold_on, &digifold_amount, &digifold_mode,
    &digimeter_on,
    &digicrush_on, &digicrush_mode, &digicrush_amount, &digicrush_tune,
    &digiwave_on, &digiwave_son, &digiwave_pmode, &digiwave_smode, &digiwave_page,
    &digiwave_speed, &digiwave_pdepth, &digiwave_sdepth,
    &digiwave_mix, &digiwave_fb, &digiwave_pmix,
    &digiwash_on, &digiwash_amount,
    &dgmac2_on, &dgmac2_amount,
    &dgmac3_on, &dgmac3_amount,
    &dgmac4_on, &dgmac4_amount,
    &digiwrmz_on, &digigrainz_on, &digiwrmz_mode,
    &digiwrmz_time, &digiwrmz_fb, &digiwrmz_tone,
    &digiwrmz_mod, &digiwrmz_rate, &digiwrmz_mix,
    &digigrainz_dens, &digigrainz_size, &digigrainz_pitch,
    &digigrainz_spray, &digigrainz_fb, &digigrainz_mix,
};
/* Bits per param: 1 for the on/off/switch params, 7 for the 0..127 values.
 * The 15 one-bit params (vs 7) free enough of the 34-byte spare for the DELAYS
 * block; the two parked LFO-bridge params (digimod_dest/voice, always 0 on the
 * DT1) are dropped. 15*1 + 33*7 = 246 bits -> 31 bytes + 3 header = 34. */
static const unsigned char dn_stbits[DN_STORE_N] = {
    /* ring */ 1,7,7, /* eq */ 1,7,7, /* fold */ 1,7,7, /* meter */ 1,
    /* crush */ 1,7,7,7,
    /* wave */ 1,1,7,7,1, 7,7,7, 7,7,7,
    /* macros */ 1,7, 1,7, 1,7, 1,7,
    /* delays */ 1,1,1, 7,7,7,7,7,7, 7,7,7,7,7,7
};

static unsigned char *dn_store_blk;             /* block we last synced */
static unsigned char dn_store_copy[DN_STORE_LEN];
static int dn_store_dirty;                      /* a page edit is pending */
static int dn_sync_ph;                          /* ticks between block compares */

static unsigned char *dn_store_slot(void)
{
    unsigned p = (unsigned)DT_UI_KIT;
    if (p < DT_KIT0 || p >= DT_KIT0 + DN_PAT_SLOTS * DT_KIT_STEP ||
        (p - DT_KIT0) % DT_KIT_STEP)
        return 0;
    return (unsigned char *)(p + DN_STORE_OFF);
}

/* DN_STORE_N params, dn_stbits[i] bits each, little-endian bitstream after b[2]. */
static void dn_store_read(const unsigned char *b)
{
    unsigned acc = 0;
    int nbits = 0, i, o = 3;
    for (i = 0; i < DN_STORE_N; i++) {
        int w = dn_stbits[i];
        while (nbits < w) { acc |= (unsigned)b[o++] << nbits; nbits += 8; }
        *dn_fxparam[i] = (int)(acc & ((1u << w) - 1u));
        acc >>= w;
        nbits -= w;
    }
}

static void dn_store_write(unsigned char *b)
{
    unsigned acc = 0;
    int nbits = 0, i, o = 3;
    b[0] = 0x44; b[1] = 0x58;                             /* "DX" */
    b[2] = DN_STORE_VER;
    for (i = 0; i < DN_STORE_N; i++) {
        int w = dn_stbits[i];
        acc |= (unsigned)(*dn_fxparam[i] & ((1u << w) - 1u)) << nbits;
        nbits += w;
        while (nbits >= 8) {
            b[o++] = (unsigned char)(acc & 0xff);
            acc >>= 8;
            nbits -= 8;
        }
    }
    if (nbits)
        b[o++] = (unsigned char)(acc & 0xff);
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
        } else if (dn_sync_ph++ & 7) {
            return;                          /* nothing pending and no pattern
                                              * switch: compare only every 8th
                                              * tick (the adopt guard stays) */
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
static void dg_slider(void *bmp, int x, int val, int vmax);
static void dg_slot(void *bmp, int x, const char *s, int v);

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
/* DELAYS (4.0a). 80..87 are free on the DT1 (verify against its CC table). */
#define DN_CC_DLY_ON    80
#define DN_CC_DLY_MODE  81
#define DN_CC_DLY_1     82
#define DN_CC_DLY_2     83
#define DN_CC_DLY_3     84
#define DN_CC_DLY_4     85
#define DN_CC_DLY_5     86
#define DN_CC_DLY_6     87

static void dgdly_set(int slot, int val);

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
    case DN_CC_WV_ON:      if (digiwave_page) digiwave_son = (v >= 64);
                           else digiwave_on = (v >= 64); break;
    case DN_CC_WV_PMODE:   digiwave_pmode = v; digiwave_page = 0; break;
    case DN_CC_WV_SMODE:   digiwave_smode = v; digiwave_page = 1; break;
    case DN_CC_WV_PAGE:    digiwave_page = (v >= 64); break;
    case DN_CC_WV_SPD:     digiwave_speed = v; break;
    case DN_CC_WV_DEP:     if (digiwave_page) digiwave_sdepth = v;
                           else digiwave_pdepth = v; break;
    case DN_CC_WV_MIX:     digiwave_mix = v; break;
    case DN_CC_WV_FB:      digiwave_fb = v; break;
    case DN_CC_DLY_ON:     if (digiwrmz_mode) digigrainz_on = (v >= 64);
                           else digiwrmz_on = (v >= 64); break;
    case DN_CC_DLY_MODE:   digiwrmz_mode = (v >= 64); break;
    case DN_CC_DLY_1:      dgdly_set(0, v); break;
    case DN_CC_DLY_2:      dgdly_set(1, v); break;
    case DN_CC_DLY_3:      dgdly_set(2, v); break;
    case DN_CC_DLY_4:      dgdly_set(3, v); break;
    case DN_CC_DLY_5:      dgdly_set(4, v); break;
    case DN_CC_DLY_6:      dgdly_set(5, v); break;
    default: return;                 /* not ours: stock handles/ignores it */
    }
    dn_store_dirty = 1;              /* persist with the pattern */
}

/* ---- PERFORM page: static depths + momentary trig pads -----------------------
 * The 16 trigs are performance pads in five blocks, each pad with an intensity
 * (a block of four pads has 1..4, a block of two has 2,4). A held pad saves the
 * block's depth (and on/off), turns the block on, and ramps the depth up to the
 * intensity target (base + intensity/4 of the way to 127), where it holds.
 * Releasing it ramps back to the saved value and restores the on/off. This
 * never touches the per-pattern store, so the saved value is the resting
 * position.
 *
 * Each engine has its OWN depth: fold and crush are separate, and the WAVE
 * engine now has a PITCH depth and a STEREO depth (digiwave_pdepth / _sdepth),
 * so the pitch and stereo pads no longer fight over one value.
 *
 * The ramp runs in ev_render_in (audio context, 1500 Hz), one cheap step per
 * block, and only while a pad or its release ramp is active -- with no pad held
 * it costs a single flag test. */
#define PF_N 5
static const unsigned char pf_trig_block[16] = {
    0, 0, 1, 1, 3, 3, 4, 4, 0, 0, 2, 2, 3, 3, 4, 4
};
static const unsigned char pf_trig_inten[16] = {
    1, 2, 2, 4, 1, 2, 1, 2, 3, 4, 2, 4, 3, 4, 3, 4
};
/* block -> its params (index -1 = none) */
static const signed char pf_block_params[5][2] = {
    { 0, -1 },          /* ring: depth */
    { 1, -1 },          /* fold: amount */
    { 2, -1 },          /* crush: amount */
    { 3, -1 },          /* pitch: wave PITCH depth */
    { 4, -1 },          /* stereo: wave STEREO depth */
};
static int *const pf_param[PF_N] = {
    &digiring_depth, &digifold_amount, &digicrush_amount,
    &digiwave_pdepth, &digiwave_sdepth
};
static int *const pf_onflag[PF_N] = {
    &digiring_on, &digifold_on, &digicrush_on, &digiwave_on, &digiwave_son
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
    if (b == 3)
        digiwave_page = 0;                      /* pitch pad uses the PITCH group */
    else if (b == 4)
        digiwave_page = 1;                      /* stereo pad uses the STEREO group */
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
    pf_running = 1;                             /* make sure the release ramp runs */
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

/* ---- MACRO pages: one value drives a bundle of engines -----------------------
 * Each macro reuses the effects; the engines any of them touches are captured
 * when the first turns on and restored when the last turns off. Runs in
 * ev_render_in; a single flag test when none are on.
 *   WASH    = wobble (WAVE WOB) + SRR (crush SR, capped) + EQ tilt (hi+ / lo-)
 *   RADIO   = bandpass (EQ lo- hi-) + drive (fold) + bits (crush)
 *   DESTROY = fold + bits (crush) + ring
 *   SPACE   = stereo width (WAVE WIDE)
 * RADIO/DESTROY/SPACE are a test build: their state is .bss, not stored yet. */
#define MW_SRR   10                         /* SRR amount at full WASH (0..127) */
#define MW_TILT  24                         /* EQ tilt at full WASH */
static int mw_was;
static int mb_wav_on, mb_wav_son, mb_wav_page, mb_wav_pmode, mb_wav_smode;
static int mb_wav_pd, mb_wav_sd, mb_wav_mix, mb_wav_fb, mb_wav_spd;
static int mb_cru_on, mb_cru_mode, mb_cru_amt, mb_cru_tune;
static int mb_fold_on, mb_fold_amt, mb_fold_mode;
static int mb_rng_on, mb_rng_dep, mb_rng_frq;
static int mb_eq_on, mb_eq_lo, mb_eq_hi;

static void mw_capture(void)
{
    mb_wav_on = digiwave_on; mb_wav_son = digiwave_son; mb_wav_page = digiwave_page;
    mb_wav_pmode = digiwave_pmode; mb_wav_smode = digiwave_smode;
    mb_wav_pd = digiwave_pdepth; mb_wav_sd = digiwave_sdepth;
    mb_wav_mix = digiwave_mix; mb_wav_fb = digiwave_fb; mb_wav_spd = digiwave_speed;
    mb_cru_on = digicrush_on; mb_cru_mode = digicrush_mode;
    mb_cru_amt = digicrush_amount; mb_cru_tune = digicrush_tune;
    mb_fold_on = digifold_on; mb_fold_amt = digifold_amount; mb_fold_mode = digifold_mode;
    mb_rng_on = digiring_on; mb_rng_dep = digiring_depth; mb_rng_frq = digiring_freq;
    mb_eq_on = digieq_on; mb_eq_lo = digieq_lo; mb_eq_hi = digieq_hi;
}

static void mw_restore(void)
{
    digiwave_on = mb_wav_on; digiwave_son = mb_wav_son; digiwave_page = mb_wav_page;
    digiwave_pmode = mb_wav_pmode; digiwave_smode = mb_wav_smode;
    digiwave_pdepth = mb_wav_pd; digiwave_sdepth = mb_wav_sd;
    digiwave_mix = mb_wav_mix; digiwave_fb = mb_wav_fb; digiwave_speed = mb_wav_spd;
    digicrush_on = mb_cru_on; digicrush_mode = mb_cru_mode;
    digicrush_amount = mb_cru_amt; digicrush_tune = mb_cru_tune;
    digifold_on = mb_fold_on; digifold_amount = mb_fold_amt; digifold_mode = mb_fold_mode;
    digiring_on = mb_rng_on; digiring_depth = mb_rng_dep; digiring_freq = mb_rng_frq;
    digieq_on = mb_eq_on; digieq_lo = mb_eq_lo; digieq_hi = mb_eq_hi;
}

static void mw_one(int k, int a)
{
    int t;
    switch (k) {
    case DN_WASHKIND:                   /* wobble + SRR + tilt */
        t = a * MW_TILT / 127;
        digiwave_on = 1; digiwave_page = 0; digiwave_pmode = 0; digiwave_pdepth = a;
        digicrush_on = 1; digicrush_mode = 64; digicrush_amount = a * MW_SRR / 127;
        digieq_on = 1; digieq_lo = 64 - t; digieq_hi = 64 + t;
        break;
    case DN_RADKIND:                    /* radio: deep band cut + fold drive + crunch */
        t = a * 52 / 127;               /* push both shelves well down */
        digieq_on = 1; digieq_lo = 64 - t; digieq_hi = 64 - t;
        digifold_on = 1; digifold_mode = 127;               /* TRSH: full fold + OD */
        digifold_amount = a * 110 / 127;
        digicrush_on = 1; digicrush_mode = 106;             /* CRUNCH: hard clip */
        digicrush_amount = a * 72 / 127;                    /* (was 90, -20%) */
        break;
    case DN_DESKIND:                    /* fold + bits + ring */
        digifold_on = 1; digifold_amount = a * 60 / 127;
        digicrush_on = 1; digicrush_mode = 21; digicrush_amount = a * 70 / 127;
        digiring_on = 1; digiring_depth = a * 40 / 127;
        break;
    default:                            /* SPACE: stereo width */
        digiwave_son = 1; digiwave_page = 1; digiwave_smode = 106; digiwave_sdepth = a;
        break;
    }
}

static void mw_apply(void)
{
    int any = digiwash_on || dgmac2_on || dgmac3_on || dgmac4_on;
    if (any && !mw_was)
        mw_capture();
    if (any) {
        /* Rebuild the layer from the captured base every block, then apply each
         * active macro. Without this the engines a macro touched stayed "on"
         * after it was switched off while another macro was still active (e.g.
         * switch WASH off with RADIO on and the WOB wobble kept running). */
        mw_restore();
        if (digiwash_on)
            mw_one(DN_WASHKIND, digiwash_amount);
        if (dgmac2_on)
            mw_one(DN_RADKIND, dgmac2_amount);
        if (dgmac3_on)
            mw_one(DN_DESKIND, dgmac3_amount);
        if (dgmac4_on)
            mw_one(DN_SPCKIND, dgmac4_amount);
    } else if (mw_was) {
        mw_restore();
    }
    mw_was = any;
}

static int *mw_onp(int k)
{
    return k == DN_WASHKIND ? &digiwash_on :
           k == DN_RADKIND ? &dgmac2_on :
           k == DN_DESKIND ? &dgmac3_on : &dgmac4_on;
}

static int *mw_amtp(int k)
{
    return k == DN_WASHKIND ? &digiwash_amount :
           k == DN_RADKIND ? &dgmac2_amount :
           k == DN_DESKIND ? &dgmac3_amount : &dgmac4_amount;
}

/* DT1: our pages are all kind 0, told apart by place (dc_block/cur_kind); this
 * list is unused here but kept so the merged source reads as the original's. */
static const int dc_kinds[DN_NPAGES] = {
    DN_FXKIND, DN_FEKIND, DN_EQKIND, DN_CRKIND, DN_WVKIND, DN_WSKIND,
    DN_PFKIND, DN_WASHKIND, DN_RADKIND, DN_DESKIND, DN_SPCKIND, DN_DLYKIND
};

#define DN_MASTER_VT  F_401845dc     /* the DT1's master view */
static char *dc_view;               /* the master view, from its last draw */
static int dc_vis;                  /* frames since the master view was drawn */
static int dc_meter_vis;            /* FOLD page (the meter reader) is up */
static int dc_perf_prev = -1;       /* FX kind to return to from the macro tree */
static int dc_func;                 /* FUNC held: encoder turns snap to 0/127 */
static int dc_rate = 3;             /* ticks between redraws (animated pages) */
static int dc_ctr;                  /* ticks since the last redraw */
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
    mw_apply();                          /* MACRO-page drivers (WASH) */
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

/* Output ceiling (last stage, order 64). Must run on EVERY block: with every
 * section off the raw mix can still exceed the knee, and skipping the limiter
 * then hard-clips at 24 bits (audible distortion). An "any hot section" gate
 * (#5) was measured (~950 cycles/block when clean) but is NOT safe and was
 * reverted; the cost of always limiting is the correct price. */
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

/* The master view is ours AND still open. view+48 is the view's "closed" byte:
 * view_activate clears it (0x400e53da) and view_close sets it (0x400e51e6), so
 * a view that was closed but is still allocated cannot keep our key/encoder
 * handlers armed. Without this the decaying dc_vis window kept eating keys
 * meant for the sequencer (MIDI-track microtiming / volume). */
static int dc_view_open(char *v)
{
    return v && *(int *)v == DN_MASTER_VT && *((unsigned char *)v + 48) == 0;
}

/* Alive (open) AND drawn recently. */
static int master_live(char *v)
{
    return dc_vis > 0 && dc_view_open(v);
}

/* digimeter reads this (weak ref) to skip its per-block peak scan unless the
 * FOLD page, the only page that draws the bars, is up. */
int digictl_meter_active(void)
{
    return dc_meter_vis;
}

/* DT1 port: our seven pages are seven kind-0 entries in a row (DT_KIND, the "None" page); the
 * page number is the place in that run. The run's start in the vector, or -1. */
#define DT_KIND 0
static int dc_block(char *view)
{
    int *v = *(int **)(view + 124), *e = *(int **)(view + 128), n, i, m;
    if (!v || !e || e <= v)
        return -1;
    n = e - v;
    for (i = 0; i + DN_NPAGES <= n; i++) {
        for (m = 0; m < DN_NPAGES; m++)
            if (v[i + m] != DT_KIND)
                break;
        if (m == DN_NPAGES)
            return i;
    }
    return -1;
}

/* the page shown: 0..6 (DN_FXKIND .. DN_PFKIND) for ours, -1 for a stock page */
static int cur_kind(char *view)
{
    int *v = *(int **)(view + 124), *e = *(int **)(view + 128), i = *(int *)(view + 144);
    int b = dc_block(view);
    if (!v || i < 0 || i >= e - v || b < 0 || i < b || i >= b + DN_NPAGES)
        return -1;
    return i - b;
}

/* the index of our page `k` in the master vector, or -1 */
static int dc_find_kind(char *view, int k)
{
    int b = dc_block(view);
    return b < 0 ? -1 : b + k;
}

/* The two page trees. LEFT/RIGHT cycle within the active tree; UP moves between
 * them. Tree 1 = the FX pages, in play order (FOLD - CRUSH - RING - STEREO -
 * PITCH - TILT); tree 2 = PERFORM + the four macros, reached with UP. PERFORM is
 * not part of the tree-1 cycle. */
static const int dc_tree1[DN_TREE1_N] = {
    DN_FEKIND, DN_CRKIND, DN_FXKIND, DN_WSKIND, DN_WVKIND, DN_EQKIND, DN_DLYKIND
};
static const int dc_tree2[5] = {
    DN_PFKIND, DN_WASHKIND, DN_RADKIND, DN_DESKIND, DN_SPCKIND
};

void dt_page_show(void *view);   /* DT1: a page change as the stock page key makes one */

static int dc_goto(char *view, int kind)
{
    int pos = dc_find_kind(view, kind);
    if (pos < 0)
        return 0;
    *(int *)(view + 144) = pos;
    DN_INVALIDATE(view);
    dt_page_show(view);             /* DT1: the stock page change / title popup */
    return 1;
}

/* Go to a page by its index in the master vector (DT1: the stock pages and ours
 * share one vector, and our pages are told apart by place, so a stock kind
 * cannot be turned into an index -- the navigation is index-based). */
static int dc_goto_idx(char *view, int idx)
{
    *(int *)(view + 144) = idx;
    DN_INVALIDATE(view);
    dt_page_show(view);
    return 1;
}

/* The main LEFT/RIGHT scroll, as VECTOR INDICES: every stock page (not our
 * run), then our FX pages in play order. The macro tree (PERFORM + macros) is
 * NOT here - it is reached with UP. */
static int dc_main_list(char *view, int *buf)
{
    int *v = *(int **)(view + 124);
    int n = v ? (int)((*(int **)(view + 128)) - v) : 0;
    int b = dc_block(view);                 /* DT1: our pages are the kind-0 run */
    int i, m = 0;
    for (i = 0; i < n && m < 15; i++) {
        if (b >= 0 && i >= b && i < b + DN_NPAGES)  /* our pages: appended below */
            continue;
        if (v[i] < DT_MK_COMP || v[i] > DT_MK_EXTMIX)
            continue;                       /* skip anything that is not a stock master-FX page
                                             * (e.g. a track SRC/TRIG page that shares the vector) */
        buf[m++] = i;                       /* a stock master page: its index */
    }
    for (i = 0; i < DN_TREE1_N && m < 16; i++)
        if (dc_find_kind(view, dc_tree1[i]) >= 0)
            buf[m++] = dc_find_kind(view, dc_tree1[i]);   /* our tree-1 page */
    return m;
}

static void add_page(char *view)
{
    int *v = *(int **)(view + 124), *e = *(int **)(view + 128), *nv;
    int n = e - v, i, j, pos;
    if (!v || n < 1 || n > 13)
        return;
    for (i = 0; i < n; i++)
        if (v[i] == DT_KIND)                /* ours (or another mod's kind-0 page) is in */
            return;
    pos = n >= 4 ? 3 : n;                   /* after the stock master pages */
    nv = DN_OP_NEW(4u * (unsigned)(n + DN_NPAGES));
    if (!nv)
        return;
    for (i = j = 0; i < n; i++) {
        if (i == pos) {
            int m;
            for (m = 0; m < DN_NPAGES; m++)
                nv[j++] = DT_KIND;
        }
        nv[j++] = v[i];
    }
    if (pos == n) {
        int m;
        for (m = 0; m < DN_NPAGES; m++)
            nv[j++] = DT_KIND;
    }
    i = *(int *)(view + 144);
    *(int **)(view + 124) = nv;
    *(int **)(view + 128) = nv + n + DN_NPAGES;
    *(int **)(view + 132) = nv + n + DN_NPAGES;
    /* the old buffer is left alone (the firmware's operator delete halts on one not from its heap) */
    if (i >= pos && i < n)
        *(int *)(view + 144) = i + DN_NPAGES;   /* stay on the same stock page */
    else if (i >= n + DN_NPAGES || i < 0)
        *(int *)(view + 144) = 0;
}

/* the title bar's names for our pages: kind 0's short and long name, set to the page shown before the
 * stock code reads them (its draw, and the title, vtable slot 19: page_glue.s) */
static const char *const dc_short[DN_NPAGES] = {
    "RING", "FOLD", "TILT", "CRUSH", "PITCH", "STEREO", "PERF",
    "WASH", "RADIO", "DESTROY", "SPACE", "DELAYS"
};
static const char *const dc_long[DN_NPAGES] = {
    "DIGI RING", "DIGI FOLD", "DIGI TILT", "DIGI CRUSH", "DIGI PITCH", "DIGI STEREO", "PERFORM",
    "DIGI WASH", "DIGI RADIO", "DIGI DESTROY", "DIGI SPACE", "DIGI DELAYS"
};

void digictl_names(void *view)
{
    int k = cur_kind((char *)view);
    if (k >= 0) {
        DN_PAGENAMES[0] = dc_short[k];
        DN_PAGENAMES[1] = dc_long[k];
    }
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

/* ---- direct framebuffer drawing ---------------------------------------------
 * The panel is 1 bpp, column-major: byte = page + 8*column, page = 7-(row>>3),
 * bit = row&7, row measured from the TOP of the panel; at master-view draw time
 * the front buffer pointer is fb_front (0x40241a44), the buffer being composed
 * (the same buffer the stock page draw writes). The stock DN_FILLRECT /
 * DN_FRAMERECT cost a firmware call per pixel or span; these write the bytes
 * directly. Coordinates are the stock ones (y = 0 at the BOTTOM), so a stock
 * row y is display row 63-y. */
/* DT1 port: the stock DN_FILLRECT / DN_FRAMERECT, as the 3.2a port used. The
 * 3.5p direct-framebuffer drawing (the Digitone's fb_front) is a Digitone-only
 * optimization; the DT1 has no such pointer mapped here. dg_bmp is the panel
 * the page draw was given (set in digictl_mdraw). */
static void *dg_bmp;

static void dg_fill(int x0, int y0, int x1, int y1, int c)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 < y1) { t = y0; y0 = y1; y1 = t; }        /* y0 = the top row */
    DN_FILLRECT(dg_bmp, x0, y0, x1, y1, c);
}

static void dg_px(int x, int y)
{
    if ((unsigned)x > 127u || (unsigned)y > 63u)
        return;
    DN_FILLRECT(dg_bmp, x, y, x, y, 1);
}

static void dg_frame(int x0, int y0, int x1, int y1)
{
    DN_FRAMERECT(dg_bmp, x0, y0, x1, y1, 1);
}

static void dg_glyph(void *bmp, int x, int ytop, int ch, int sc)
{
    int w = DN_FW[ch & 0xff];
    int off = DN_FO[ch & 0xff];
    int c, r;
    (void)bmp;
    for (c = 0; c < w; c++) {
        int bits = DN_FB[off + c];
        for (r = 0; r < 8; r++) {
            if (bits & (1 << r)) {              /* bit 0 = top of the glyph */
                int px = x + c * sc;
                int py = ytop - r * sc;
                dg_fill(px, py, px + sc - 1, py - sc + 1, 1);
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

/* the big FX mode name, centred on the screen (same centre as the title),
 * sitting just above it */
static void dg_bigname(void *bmp, const char *s)
{
    dg_text(bmp, (128 - dg_strw(s, 2)) / 2, 34, s, 2);
}

/* the little < > page arrows, centred vertically on each side */
static void dg_arrows(void *bmp)
{
    dg_glyph(bmp, 3, 30, '<', 1);
    dg_glyph(bmp, 122, 30, '>', 1);
}

/* An OFF message box: a 1 px frame with "OFF" inside, centred horizontally and
 * sitting above the vertical middle of the drawable area. Drawn over the page
 * when its FX / macro section is switched off. */
static void dg_offbox(void *bmp)
{
    const char *s = "OFF";
    int w = dg_strw(s, 1);
    int x0 = (128 - (w + 12)) / 2, x1 = x0 + w + 11;
    int y0 = 51, y1 = 37;                   /* box top / bottom (stock coords) */
    dg_fill(x0, y0, x1, y1, 0);             /* clear the box area */
    dg_frame(x0, y0, x1, y1);
    dg_text(bmp, (128 - w) / 2, 48, s, 1);
}

/* The same framed box, top-right, shown when this page's FX is being driven by
 * one of the macro pages: the macro overwrites the FX params every block, so
 * the page's encoders are effectively locked. */
static void dg_macrobox(void *bmp)
{
    const char *s = "MACRO";
    int w = dg_strw(s, 1);
    int x0 = (128 - (w + 12)) / 2, x1 = x0 + w + 11;   /* same slot as OFF */
    int y0 = 51, y1 = 37;
    dg_fill(x0, y0, x1, y1, 0);
    dg_frame(x0, y0, x1, y1);
    dg_text(bmp, (128 - w) / 2, 48, s, 1);
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
    int cx = 60, cy = 35, R = 14, i, n = 16;
    int dep = digiring_on ? digiring_depth * 258 : 0;   /* 0..127 -> Q15 */
    (void)bmp;
    for (i = 0; i < n; i++) {
        int a = (i * 256 / n) & 0xff;
        int s = sin256[a], c = sin256[(a + 64) & 0xff];
        int x = cx + ((c * R) >> 15), y = cy + ((s * R) >> 15);
        dg_px(x, y);
    }
    if (dep > 0) {
        int s = sin256[dg_ang & 0xff], c = sin256[(dg_ang + 64) & 0xff];
        int amp = (dep * 13) >> 15;
        int jx = dg_rand(2 * amp + 1) - amp, jy = dg_rand(2 * amp + 1) - amp;
        int x = cx + ((c * R) >> 15) + jx, y = cy + ((s * R) >> 15) + jy;
        dg_fill(x - 1, y - 1, x + 1, y + 1, 1);
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
    (void)bmp;
    dg_fill(x0, y0, x1, y0 + h, 0);
    dg_fill(x0, y0, x1, y0 + f, 1);
}

/* the L/R output meter, far right, when H has switched it on */
static void dg_meter(void *bmp)
{
    if (!digimeter_on)
        return;
    dg_vbar(bmp, 120, 121, digimeter_l, 60);
    dg_vbar(bmp, 125, 126, digimeter_r, 60);
}

static void dg_fx_page(void *bmp)
{
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_ring(bmp);
    dg_slider(bmp, 96, digiring_on ? digiring_depth : 0, 127);    /* depth */
    dg_slider(bmp, 118, digiring_freq, 127);                      /* rate  */
    dg_slot(bmp, 96, "DEP", digiring_depth);
    dg_slot(bmp, 118, "FRQ", digiring_freq);
    dg_title(bmp, "RING");
}

/* ---------------- FOLD: spiral + big mode + amount slider + meter ------------ */

/* the fold amount as a spiral: a straight line at 0, coiling as it folds */
static void dg_spiral(void *bmp, int amount)
{
    int cx = 20, cy = 30, i, n = 30, turns, rmax;
    (void)bmp;
    if (amount <= 0) {                  /* straight when not folding */
        dg_fill(cx - 14, cy, cx + 14, cy, 1);
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
        dg_px(x, y);
    }
}

static const char *dg_modename(int m)
{
    static const char *const n[4] = { "CLEAN", "MUD", "DIST", "TRSH" };
    return n[m & 3];
}

static void dg_slider(void *bmp, int x, int val, int vmax)
{
    int y0 = 20, y1 = 44, pos;    /* leave room below for the label + value */
    DN_FRAMERECT(bmp, x - 2, y0 - 1, x + 2, y1 + 1, 1);
    val = clampi(val, 0, vmax);
    pos = y0 + val * (y1 - y0) / vmax;
    dg_fill(x - 1, y0, x + 1, pos, 1);
}

/* a short label above its value, both centred on the slider column x, at the
 * bottom of the screen */
static void dg_slot(void *bmp, int x, const char *s, int v)
{
    DN_TEXTF(bmp, DN_FONT5, x - dg_strw(s, 1) / 2, 12, -1, "%s", s);
    DN_TEXTF(bmp, DN_FONT5, x - ((v >= 100) ? 9 : (v >= 10) ? 6 : 3), 4, -1, "%d", v);
}

static void dg_fold_page(void *bmp)
{
    int band = digifold_mode / 32;
    const char *m;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_spiral(bmp, digifold_on ? digifold_amount : 0);   /* left */
    if (band > 3) band = 3;
    m = dg_modename(band);
    dg_bigname(bmp, m);                                  /* big, screen-centred */
    dg_slider(bmp, 100, digifold_on ? digifold_amount : 0, 127);
    dg_meter(bmp);
    dg_slot(bmp, 100, "AMT", digifold_amount);
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
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_fill(8, 30, 120, 30, 1);             /* 0 dB reference, one span */
    for (x = 8; x <= 120; x += 2)
        dg_px(x, dg_eq_y(x));
    DN_TEXTF(bmp, DN_FONT5, 4, 6, -1, "LO %d", digieq_lo);
    DN_TEXTF(bmp, DN_FONT5, 96, 6, -1, "HI %d", digieq_hi);
    dg_title(bmp, "TILT");
}

/* ---------------- CRUSH: big stage name + amount/tune bars ---------------- */
static const char *dg_crushname(int m)
{
    static const char *const n[3] = { "BITS", "REDUX", "CRUNCH" };
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
    (void)bmp;
    if (mode > 2) mode = 2;
    dg_cr_ph += 1 + (amt >> 4);
    for (x = 4; x <= 40; x += 2) {
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
        dg_px(x, y);
        if (mode == 0 && digicrush_tune > 0 && dg_rand(48) < digicrush_tune / 4)
            dg_fill(x, cy - 10, x, cy + 10, 1);          /* dither specks */
    }
}

static void dg_crush_page(void *bmp)
{
    const char *m = dg_crushname(digicrush_mode);
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    dg_crush_gfx(bmp);
    dg_bigname(bmp, m);
    dg_slider(bmp, 96, digicrush_on ? digicrush_amount : 0, 127);   /* depth */
    dg_slider(bmp, 118, digicrush_tune, 127);                       /* rate  */
    dg_meter(bmp);
    dg_slot(bmp, 96, "AMT", digicrush_amount);
    dg_slot(bmp, 118, "TUN", digicrush_tune);
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
    int dep = digiwave_pdepth, spd = digiwave_speed;
    int c1 = 15, c2 = 29;                           /* the two lines */
    int amp = (dep * 14) / 127;                     /* 0 -> straight */
    int k = 1 + (spd * 5) / 127;                    /* nodes 1..6 */
    int y;
    (void)bmp;
    dg_pi_ph += 2 + (spd >> 3);
    for (y = 2; y <= 51; y += 2) {
        int idx = ((y * k * 256) / 52 + dg_pi_ph) & 0xff;
        int s = (sin256[idx] * amp) >> 15;
        int x1 = c1 + s;
        int x2 = c2 - s;                            /* opposite -> cross */
        dg_px(x1, y);
        dg_px(x2, y);
    }
}

/* left third, HORIZONTAL: collapses to one line at depth 0. FLAN = a comb
 * ripple (amp = depth, wavelength = speed); PAN = two lines moving apart
 * oppositely; WIDE = two lines spreading. No frame. */
static int dg_st_ph;
static void dg_stereo_gfx(void *bmp)
{
    int mode = digiwave_smode * 3 / 128;
    int dep = digiwave_sdepth, spd = digiwave_speed;
    int cy = 26, x;
    (void)bmp;
    if (mode > 2) mode = 2;
    dg_st_ph += 2 + (spd >> 3);
    if (mode == 2) {                                    /* WIDE */
        int spread = (dep * 10) / 127;                  /* 0 -> one line */
        for (x = 4; x <= 40; x += 2) {
            dg_px(x, cy - spread);
            dg_px(x, cy + spread);
        }
    } else if (mode == 1) {                             /* PAN */
        int amp = (dep * 10) / 127;
        int s = (sin256[dg_st_ph & 0xff] * amp) >> 15;
        for (x = 4; x <= 40; x += 2) {
            dg_px(x, cy + s);
            dg_px(x, cy - s);
        }
    } else {                                            /* FLAN */
        int amp = (dep * 10) / 127;
        int wl = 6 + ((127 - spd) * 16) / 127;
        for (x = 4; x <= 40; x += 2) {
            int idx = ((x * 256 / wl) + dg_st_ph) & 0xff;
            int y = cy - ((sin256[idx] * amp) >> 15);
            dg_px(x, y);
        }
    }
}

static void dg_wave_bars(void *bmp, const char *m, const char *title, int which)
{
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    if (which == 0)
        dg_pitch_gfx(bmp);
    else
        dg_stereo_gfx(bmp);
    dg_bigname(bmp, m);
    if (which == 0) {                    /* PITCH: SPD, DEP, MIX */
        dg_slider(bmp, 86, digiwave_speed, 127);
        dg_slider(bmp, 104, digiwave_pdepth, 127);
        dg_slider(bmp, 122, digiwave_pmix, 127);
        dg_slot(bmp, 86, "SPD", digiwave_speed);
        dg_slot(bmp, 104, "DEP", digiwave_pdepth);
        dg_slot(bmp, 122, "MIX", digiwave_pmix);
    } else if ((digiwave_smode * 3 / 128) == 0) {   /* FLAN: SPD, DEP, MIX, FB */
        dg_slider(bmp, 88, digiwave_speed, 127);
        dg_slider(bmp, 100, digiwave_sdepth, 127);
        dg_slider(bmp, 112, digiwave_mix, 127);
        dg_slider(bmp, 124, digiwave_fb, 127);
        dg_slot(bmp, 88, "SPD", digiwave_speed);
        dg_slot(bmp, 100, "DEP", digiwave_sdepth);
        dg_slot(bmp, 112, "MIX", digiwave_mix);
        dg_slot(bmp, 124, "FB", digiwave_fb);
    } else {                             /* PAN / WIDE: SPD, DEP */
        dg_slider(bmp, 98, digiwave_speed, 127);
        dg_slider(bmp, 118, digiwave_sdepth, 127);
        dg_slot(bmp, 98, "SPD", digiwave_speed);
        dg_slot(bmp, 118, "DEP", digiwave_sdepth);
    }
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
    &digiring_depth, &digifold_amount, &digicrush_amount, &digiwave_pdepth,
    &digiwave_sdepth, &digieq_lo, &digieq_hi, &digiwave_speed
};

static void dg_perf_page(void *bmp)
{
    int i;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    DN_TEXTF(bmp, DN_FONT5, (128 - dg_strw("PERFORM", 1)) / 2, 45, -1, "PERFORM");
    for (i = 0; i < 8; i++) {
        int x = (i / 4) ? 66 : 4;
        int y = 34 - (i % 4) * 10;
        DN_TEXTF(bmp, DN_FONT5, x, y, -1, "%s %d",
                 dg_pflbl[i], *dg_pfval[i]);
    }
}

/* ---------------- MACRO pages: one slider (right) + a per-page graphic -------
 * Each draw is <= ~64 FILLRECT calls and the tick gates redraws (see
 * digictl_tick). The value is centred under the slider, the title on the
 * display. */
static int dg_wa_ph;
static void dg_wa_num(char *s, int v)
{
    if (v >= 100) {
        s[0] = '0' + v / 100; s[1] = '0' + (v / 10) % 10; s[2] = '0' + v % 10;
        s[3] = 0;
    } else if (v >= 10) {
        s[0] = '0' + v / 10; s[1] = '0' + v % 10; s[2] = 0;
    } else {
        s[0] = '0' + v; s[1] = 0;
    }
}

static void dg_macro_frame(void *bmp, const char *title, int on, int amt)
{
    char num[4];
    dg_slider(bmp, 121, on ? amt : 0, 127);   /* empty = off, filled = on */
    dg_wa_num(num, amt);
    DN_TEXTF(bmp, DN_FONT5, 121 - dg_strw(num, 1) / 2, 4, -1, "%s", num);
    DN_TEXTF(bmp, DN_FONT5, (128 - dg_strw(title, 1)) / 2, 24, -1, "%s", title);
}

/* WASH ??? shore: a filled band whose wavy top rises, wanders and distorts. */
static void dg_wash_page(void *bmp)
{
    int amt = digiwash_on ? digiwash_amount : 0;
    int top = 6 + amt * 30 / 127;
    int amp = 1 + amt * 7 / 127;
    int wl = 22 - amt * 16 / 127;
    int x;
    if (wl < 6)
        wl = 6;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_wa_ph += 1 + (amt >> 5);
    for (x = 0; x <= 112; x += 4) {
        int idx = ((x * 256) / wl + dg_wa_ph) & 0xff;
        int s = sin256[idx];
        int d2 = sin256[(idx * 3 + dg_wa_ph * 2) & 0xff];
        int y;
        s += (d2 * amt) >> 8;
        y = top + ((s * amp) >> 15);
        if (y < 0) y = 0;
        if (y > DG_TOP - 1) y = DG_TOP - 1;
        dg_fill(x, 0, x, y, 1);                   /* fill up to the surface */
    }
    dg_macro_frame(bmp, "WASH", digiwash_on, digiwash_amount);
}

/* RADIO ??? a speaker cone that shrinks as it is driven, with a pulsing ring. */
static int dg_ra_ph;
static void dg_radio_page(void *bmp)
{
    int amt = dgmac2_on ? dgmac2_amount : 0;
    int cx = 54, cy = 26;
    int R = 25 - amt * 12 / 127;        /* outer ring fills the height */
    int i, n = 24;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_ra_ph += 2 + (amt >> 4);
    for (i = 0; i < n; i++) {           /* outer ring + inner cone */
        int a = (i * 256 / n + dg_ra_ph) & 0xff;
        int s = sin256[a], c = sin256[(a + 64) & 0xff];
        int rr = R + ((sin256[a] * (amt >> 3)) >> 15);
        int x = cx + ((c * rr) >> 15), y = cy + ((s * rr) >> 15);
        dg_px(x, y);
        x = cx + ((c * (rr >> 1)) >> 15);
        y = cy + ((s * (rr >> 1)) >> 15);
        dg_px(x, y);
    }
    dg_px(cx, cy);
    dg_macro_frame(bmp, "RADIO", dgmac2_on, dgmac2_amount);
}

/* DESTROY ??? a sine that stair-steps (quantises) more with the amount. */
static int dg_de_ph;
static void dg_destroy_page(void *bmp)
{
    int amt = dgmac3_on ? dgmac3_amount : 0;
    int step = 1 + amt * 10 / 127;
    int amp = 6 + amt * 14 / 127;
    int x;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_de_ph += 1 + (amt >> 5);
    for (x = 0; x <= 112; x += 3) {
        int idx = ((x * 256) / 24 + dg_de_ph) & 0xff;
        int y = 26 + ((sin256[idx] * amp) >> 15);
        if (step > 1)
            y = (y / step) * step;
        if (y < 0) y = 0;
        if (y > DG_TOP - 1) y = DG_TOP - 1;
        dg_px(x, y);
    }
    dg_macro_frame(bmp, "DESTROY", dgmac3_on, dgmac3_amount);
}

/* SPACE ??? a centre line that splits into two as the width opens. */
static int dg_sp_ph;
static void dg_space_page(void *bmp)
{
    int amt = dgmac4_on ? dgmac4_amount : 0;
    int spread = 2 + amt * 16 / 127;
    int x;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_sp_ph += 1 + (amt >> 5);
    for (x = 0; x <= 112; x += 4) {
        int idx = ((x * 256) / 32 + dg_sp_ph) & 0xff;
        int s = (sin256[idx] * 4) >> 15;
        dg_px(x, 26 + spread + s);
        dg_px(x, 26 - spread + s);
    }
    dg_macro_frame(bmp, "SPACE", dgmac4_on, dgmac4_amount);
}

/* ---- DELAYS page: the one delay line in WRMZ or GRAINZ mode (D switches).
 * Slots 0..5 are the six params on B/C/E/F/G/H; A is on/off, D the mode. ---- */
static int *dgdly_wrmz[6] = {
    &digiwrmz_time, &digiwrmz_fb, &digiwrmz_tone,
    &digiwrmz_mod, &digiwrmz_rate, &digiwrmz_mix
};
static int *dgdly_grainz[6] = {
    &digigrainz_dens, &digigrainz_size, &digigrainz_pitch,
    &digigrainz_spray, &digigrainz_fb, &digigrainz_mix
};
static const char *const dgdly_wlbl[6] = { "TIM", "FB", "TON", "MOD", "RAT", "MIX" };
static const char *const dgdly_glbl[6] = { "DEN", "SIZ", "PIT", "SPR", "FB", "MIX" };
static int dc_act_id;               /* last-moved slider's encoder id (0 = none) */
static int dc_act_kind;             /* page of the last-moved encoder */

static int *dgdly_onp(void)
{
    return digiwrmz_mode ? &digigrainz_on : &digiwrmz_on;
}

static void dgdly_set(int slot, int val)
{
    int **a = digiwrmz_mode ? dgdly_grainz : dgdly_wrmz;
    if (slot >= 0 && slot < 6)
        *a[slot] = clampi(val, 0, 127);
}

static int dg_dl_ph;

static void dg_sec_caption(void *bmp, int x, const char *s, int v)
{
    char buf[12];
    int n = 0, w;
    while (s[n] && n < 4)
        buf[n] = s[n], n++;
    buf[n++] = ' ';
    if (v >= 100)
        buf[n++] = (char)('0' + v / 100);
    if (v >= 10)
        buf[n++] = (char)('0' + (v / 10) % 10);
    buf[n++] = (char)('0' + v % 10);
    buf[n] = 0;
    w = dg_strw(buf, 1);
    DN_TEXTF(bmp, DN_FONT5, x - w / 2, 1, -1, "%s", buf);
}

static void dg_delay_page(void *bmp)
{
    int **a = digiwrmz_mode ? dgdly_grainz : dgdly_wrmz;
    const char *const *lbl = digiwrmz_mode ? dgdly_glbl : dgdly_wlbl;
    int on = *dgdly_onp();
    int mixv = *a[5];
    int y, ph, amp, sec = -1;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    dg_arrows(bmp);
    amp = 3 + mixv * 9 / 127;
    dg_dl_ph += 1 + (mixv >> 5);
    ph = dg_dl_ph;
    for (y = 6; y <= 48; y += 2) {
        int s = (sin256[(y * 8 + ph) & 0xff] * amp) >> 15;
        dg_px(20 + s, y);
        dg_px(28 - s, y + 2);
    }
    dg_bigname(bmp, digiwrmz_mode ? "GRAINZ" : "WRMZ");
    dg_slider(bmp, 118, on ? mixv : 0, 127);          /* the one fixed slider */
    dg_slot(bmp, 118, "MIX", mixv);
    /* secondary slider: whichever of slots 0..4 was last moved on THIS page */
    if (dc_act_kind == DN_DLYKIND) {
        static const signed char so[9] = { -1, -1, 0, 1, -1, 2, 3, 4, 5 };
        int id = dc_act_id;
        if (id >= 0 && id <= 8 && so[id] >= 0 && so[id] < 5)
            sec = so[id];
    }
    if (sec >= 0) {
        dg_slider(bmp, 98, on ? *a[sec] : 0, 127);
        dg_sec_caption(bmp, 98, lbl[sec], *a[sec]);
    }
    dg_title(bmp, "DELAYS");
}

/* Is this page's FX being driven (locked) by one of the macro pages? Matches
 * what mw_one() touches: WASH -> WAVE pitch + crush + EQ, RADIO -> EQ + fold +
 * crush, DESTROY -> fold + crush + ring, SPACE -> WAVE stereo. */
static int dg_page_locked(int k)
{
    switch (k) {
    case DN_FXKIND:   return dgmac3_on;                              /* DESTROY */
    case DN_FEKIND:   return dgmac2_on || dgmac3_on;                 /* RADIO, DESTROY */
    case DN_EQKIND:   return digiwash_on || dgmac2_on;               /* WASH, RADIO */
    case DN_CRKIND:   return digiwash_on || dgmac2_on || dgmac3_on;  /* WASH/RADIO/DESTROY */
    case DN_WVKIND:   return digiwash_on;                            /* WASH (WOB) */
    case DN_WSKIND:   return dgmac4_on;                              /* SPACE (WIDE) */
    default:          return 0;
    }
}

/* Is the section the current page edits switched off? PERFORM has no on/off. */
static int dg_page_off(int k)
{
    switch (k) {
    case DN_FXKIND:   return !digiring_on;
    case DN_FEKIND:   return !digifold_on;
    case DN_EQKIND:   return !digieq_on;
    case DN_CRKIND:   return !digicrush_on;
    case DN_WVKIND:   return !digiwave_on;
    case DN_WSKIND:   return !digiwave_son;
    case DN_WASHKIND: return !digiwash_on;
    case DN_RADKIND:  return !dgmac2_on;
    case DN_DESKIND:  return !dgmac3_on;
    case DN_SPCKIND:  return !dgmac4_on;
    case DN_DLYKIND:  return !*dgdly_onp();
    default:          return 0;
    }
}

/* vtable slot 4 of the master view: draw */
void digictl_mdraw(void *view, void *bmp)
{
    int k;
    add_page((char *)view);
    digictl_names(view);
    DN_STOCK_DRAW(view, bmp);
    dc_view = (char *)view;
    k = cur_kind(dc_view);
    dc_meter_vis = (k == DN_FEKIND || k == DN_CRKIND);   /* CRUSH + FOLD draw the meter */
    /* Redraw cap (4.0a): animated pages redraw at dc_rate ticks; each invalidation
     * redraws the whole stock page + our overlay. 3 (~10 Hz) -> 6 halves it, and a
     * page whose section is OFF has nothing to animate so it takes the static rate. */
    dc_rate = 6;
    if (k == DN_PFKIND)
        dc_rate = 8;                          /* static page */
    else if (dg_page_off(k))
        dc_rate = 8;                          /* section off: no animation */
    else if (k >= DN_WASHKIND && k <= DN_SPCKIND && !*mw_onp(k))
        dc_rate = 8;                          /* macro off: static */
    dc_ctr = 0;
    dc_vis = dc_rate + 2;
    dg_bmp = bmp;                         /* DT1: the stock draw target */
    if (!dg_bmp)
        return;
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
    else if (k == DN_WASHKIND)
        dg_wash_page(bmp);
    else if (k == DN_RADKIND)
        dg_radio_page(bmp);
    else if (k == DN_DESKIND)
        dg_destroy_page(bmp);
    else if (k == DN_SPCKIND)
        dg_space_page(bmp);
    else if (k == DN_DLYKIND)
        dg_delay_page(bmp);
    if (dg_page_locked(k))
        dg_macrobox(bmp);                     /* driven by a macro: locked */
    else if (dg_page_off(k))
        dg_offbox(bmp);                       /* section off: show OFF */
}

/* Every UI frame, but only redraw at dc_rate (input still invalidates at once,
 * in the enc/key/CC branches). Animated pages tick at ~10 Hz, static ones at
 * ~4 Hz; leaving the page lets dc_vis decay and the work stops. */
void digictl_tick(void *ctrl)
{
    (void)ctrl;
    dn_store_sync();
    if (dc_vis)
        dc_vis--;
    if (!dc_vis)
        dc_meter_vis = 0;                     /* left the page: stop the scan */
    if (dc_vis > 0 && dc_view_open(dc_view)) {
        if (++dc_ctr >= dc_rate) {
            dc_ctr = 0;
            DN_INVALIDATE(dc_view);
        }
    } else if (dc_view && !dc_view_open(dc_view)) {
        dc_vis = 0;                           /* the view is closed: disarm now */
    }
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
        k != DN_PFKIND && k != DN_DLYKIND &&
        !(k >= DN_WASHKIND && k <= DN_SPCKIND))
        return 0;
    if (!delta)
        return 1;
    d = dc_steps(id, delta);
    if (!d)
        return 1;
    if (dc_func)
        d = (d > 0) ? 127 : -127;   /* FUNC + turn: snap the param to 100% / 0% */

    if (k == DN_FEKIND) {                /* FOLD: A on, D mode, E/F/G/H amount, LEVEL meter */
        switch (id) {
        case 1: digifold_on = (d > 0); break;
        case 4: digifold_mode = clampi(digifold_mode + d, 0, 127); break;
        case 5: case 6: case 7: case 8:
            digifold_amount = clampi(digifold_amount + d, 0, 127); break;
        case 9: digimeter_on = (d > 0); break;
        default: break;
        }
    } else if (k == DN_EQKIND) {         /* TILT: A on, E lo, H hi */
        switch (id) {
        case 1: digieq_on = (d > 0); break;
        case 5: digieq_lo = clampi(digieq_lo + d, 0, 127); break;
        case 8: digieq_hi = clampi(digieq_hi + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_CRKIND) {         /* CRUSH: A on, D mode, E amount, F tune, LEVEL meter */
        switch (id) {
        case 1: digicrush_on = (d > 0); break;
        case 4: digicrush_mode = clampi(digicrush_mode + d, 0, 127); break;
        case 5: digicrush_amount = clampi(digicrush_amount + d, 0, 127); break;
        case 6: digicrush_tune = clampi(digicrush_tune + d, 0, 127); break;
        case 9: digimeter_on = (d > 0); break;
        default: break;
        }
    } else if (k == DN_WVKIND) {         /* PITCH: A on, D mode, E speed, F depth, G mix */
        digiwave_page = 0;               /* editing this page activates it */
        switch (id) {
        case 1: digiwave_on = (d > 0); break;
        case 4: {
            int b = (digiwave_pmode * 2) >> 7;
            if (b > 1) b = 1;
            b = clampi(b + d, 0, 1);
            digiwave_pmode = b * 64 + 32;
        } break;
        case 5: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        case 6: digiwave_pdepth = clampi(digiwave_pdepth + d, 0, 127); break;
        case 7: digiwave_pmix = clampi(digiwave_pmix + d, 0, 127); break;
        default: break;
        }
    } else if (k == DN_WSKIND) {         /* STEREO: A on, D mode, E speed, F depth,
                                          * G mix, H feedback */
        digiwave_page = 1;
        switch (id) {
        case 1: digiwave_son = (d > 0); break;
        case 4: {
            int b = (digiwave_smode * 3) >> 7;
            if (b > 2) b = 2;
            b = clampi(b + d, 0, 2);
            digiwave_smode = b * 43 + 21;
        } break;
        case 5: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        case 6: digiwave_sdepth = clampi(digiwave_sdepth + d, 0, 127); break;
        case 7: digiwave_mix = clampi(digiwave_mix + d, 0, 127); break;
        case 8: digiwave_fb = clampi(digiwave_fb + d, 0, 127); break;
        default: break;
        }
    } else if (k >= DN_WASHKIND && k <= DN_SPCKIND) {   /* macros: A on/off, B..H amount */
        int *po = mw_onp(k), *pa = mw_amtp(k);
        if (id == 1)
            *po = (d > 0);
        else if (id >= 2 && id <= 8)
            *pa = clampi(*pa + d, 0, 127);
            dn_store_dirty = 1;                             /* persist with the pattern */
    } else if (k == DN_DLYKIND) {        /* DELAYS: A on, D mode, B/C/E/F/G/H params */
        if (id == 4) {                   /* D: switch WRMZ <-> GRAINZ */
            digiwrmz_mode = digiwrmz_mode ? 0 : 1;
        } else if (id == 1) {            /* A: on/off for the active mode */
            *dgdly_onp() = (d > 0);
        } else {
            static const signed char so[9] = { -1, -1, 0, 1, -1, 2, 3, 4, 5 };
            if (id >= 2 && id <= 8 && so[id] >= 0) {
                int **a = digiwrmz_mode ? dgdly_grainz : dgdly_wrmz;
                dgdly_set(so[id], *a[so[id]] + d);
            }
        }
        dc_act_id = id;
        dc_act_kind = DN_DLYKIND;
    } else if (k == DN_PFKIND) {         /* PERF: one depth per block */
        switch (id) {
        case 1: digiring_depth = clampi(digiring_depth + d, 0, 127); break;
        case 2: digifold_amount = clampi(digifold_amount + d, 0, 127); break;
        case 3: digicrush_amount = clampi(digicrush_amount + d, 0, 127); break;
        case 4: digiwave_page = 0;        /* D: the PITCH group's depth */
                digiwave_pdepth = clampi(digiwave_pdepth + d, 0, 127); break;
        case 5: digiwave_page = 1;        /* E: the STEREO group's depth */
                digiwave_sdepth = clampi(digiwave_sdepth + d, 0, 127); break;
        case 6: digieq_lo = clampi(digieq_lo + d, 0, 127); break;
        case 7: digieq_hi = clampi(digieq_hi + d, 0, 127); break;
        case 8: digiwave_speed = clampi(digiwave_speed + d, 0, 127); break;
        default: break;
        }
    } else {                             /* RING: A on, E depth, F freq */
        switch (id) {
        case 1: digiring_on = (d > 0); break;
        case 5: digiring_depth = clampi(digiring_depth + d, 0, 127); break;
        case 6: digiring_freq = clampi(digiring_freq + d, 0, 127); break;
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
    int k;
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

    if (id == 1) {                             /* FUNC: track, never consume */
        dc_func = (flags & 1) != 0;
        return 0;
    }

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

    /* UP / DOWN: from the FX tree to PERFORM + macros, and back to the FX page
     * we came from (or FOLD). Either key toggles the macro tree (DT1). */
    if ((id == DN_K_UP || id == DN_K_DOWN) &&
        (flags & 1) && !(flags & DN_KF_FUNC) && !(flags & 8)) {
        int i;
        for (i = 0; i < DN_TREE1_N; i++)
            if (dc_tree1[i] == k) {
                dc_perf_prev = *(int *)(dc_view + 144);   /* the FX page's index */
                return dc_goto_idx(dc_view, dc_find_kind(dc_view, DN_PFKIND));
            }
        for (i = 0; i < 5; i++)
            if (dc_tree2[i] == k) {
                int back = dc_perf_prev;
                if (back < 0)
                    back = dc_find_kind(dc_view, dc_tree1[0]);  /* default: FOLD */
                return dc_goto_idx(dc_view, back);
            }
    }

    if (!(flags & 1) || (flags & DN_KF_FUNC) || (flags & 8))
        return 0;                               /* ignore key-repeat */
    if (id != DN_K_LEFT && id != DN_K_RIGHT)    /* LEFT / RIGHT */
        return 0;
    {
        int buf[16], m, i, cur, idx = *(int *)(dc_view + 144);
        for (i = 0; i < 5; i++)
            if (dc_tree2[i] == k) {             /* inside the macro tree */
                cur = (i + (id == DN_K_RIGHT ? 1 : 4)) % 5;
                return dc_goto_idx(dc_view, dc_find_kind(dc_view, dc_tree2[cur]));
            }
        m = dc_main_list(dc_view, buf);         /* stock + FX, by index */
        if (m <= 0)
            return 0;
        for (i = 0; i < m; i++)
            if (buf[i] == idx)
                break;
        if (i >= m)
            i = 0;
        cur = (i + (id == DN_K_RIGHT ? 1 : m - 1)) % m;
        return dc_goto_idx(dc_view, buf[cur]);
    }
}
