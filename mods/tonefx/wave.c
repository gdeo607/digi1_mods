/* SPDX-License-Identifier: GPL-2.0-or-later
 * digiwave: one modulation insert, five modes, one set of controls.
 * Digitone mk1 / Keys, OS 1.43.
 *
 *   WOB   vibrato: a sine-modulated, interpolated short delay (pitch wobble)
 *   SHFT  pitch shifter: two crossfaded grains read at a ratio (transpose)
 *   FLAN  flanger: a modulated, interpolated short delay + feedback + mix
 *   PAN   autopan: a sine pan-law (no delay)
 *   WIDE  stereo width: mid/side (no LFO)
 *
 * One sine LFO (sin256), ramped at block rate (one sine per block, then a
 * linear step -- no per-sample sine), and one shared delay line serve
 * WOB/SHFT/FLAN. The delay is fractional (linear interpolation) and clamped
 * positive, so a swept delay is click-free. All integer, no 64-bit multiply.
 * ev_render_in remembers the output half; ev_render_out processes in place.
 * Subscriptions only: it combines with every mod.
 */
#include "corea.h"
#include "emac.h"
#include "buffx.h"
#include "sin256.h"

/* runtime parameters (exported; digictl edits them). 0..127, one step/notch. */
int digiwave_on = 0;            /* PITCH group (WOB / SHFT) on/off */
int digiwave_son = 0;           /* STEREO group (FLAN / PAN / WIDE) on/off */
int digiwave_pmode = 0;         /* PITCH page: 0..127 -> WOB / SHFT */
int digiwave_smode = 0;         /* STEREO page: 0..127 -> FLAN / PAN / WIDE */
int digiwave_page = 0;          /* active group: 0 PITCH, 1 STEREO */
int digiwave_speed = 40;        /* PITCH group LFO rate (independent now) */
int digiwave_sspeed = 40;       /* STEREO group LFO rate (was shared) */
int digiwave_pdepth = 64;       /* PITCH page depth (wobble / semitones) */
int digiwave_sdepth = 64;       /* STEREO page depth (sweep / pan / width) */
int digiwave_mix = 64;          /* wet mix (FLAN) */
int digiwave_fb = 0;            /* flanger feedback (capped at 75%) */
int digiwave_pmix = 127;        /* PITCH group dry/wet (WOB/SHFT); 127 = full wet */

#define WV_WOB 0
#define WV_SHF 1
#define WV_FLA 2
#define WV_PAN 3
#define WV_WID 4

#define WV_N     1024           /* WOB/SHFT dry line (was 2048): 2048 covers
                                 * 765 samples (WOB) and ~756 (SHFT grain), so
                                 * 1024 (~21 ms) is enough and halves the RAM */
#define WV_MASK  (WV_N - 1)
#define WV_GRAIN 512            /* SHFT grain length, samples (~10.7 ms) */
#define WV_LEAD  256            /* SHFT restart delay */
#define WV_FN    256            /* FLAN's own feedback line (~5.3 ms): the flanger
                                 * needs ~255 samples (base 128 + span 127) */
#define WV_FMASK (WV_FN - 1)

#define WV_K 89478              /* 2^32 / 48000: Hz -> Q32 phase inc */

static volatile unsigned wv_off;

/* +1 guard sample mirrors index 0, so a read at idx+1 never needs a mask.
 * WOB and SHFT share the dry line; FLAN keeps its OWN line because it writes
 * feedback into it -- that feedback must never reach WOB/SHFT (the
 * flanger/pitch "link"): with separate buffers there is no cross-talk to flush. */
/* s16 lines (was int/24-bit): the dominant cost of WOB/SHFT/FLAN is the line DDR
 * traffic, so the delay lines store 16-bit and scale <<8/>>8 at the edges. The
 * quantization is the agreed WOB/FLAN/SHFT trade; the wave input never needs
 * more than s16 (the 24-bit bus >> 8). */
static short wv_buf[2][WV_N + 1] __attribute__((aligned(16)));
static unsigned wv_w;           /* dry write index (WOB/SHFT) */
static short wv_fbuf[2][WV_FN + 1] __attribute__((aligned(16)));
static unsigned wv_fw;          /* FLAN feedback write index */
static unsigned wv_phase;       /* LFO phase, Q32 */

/* ramped (de-zipper) state, Q15: each mode's neutral value = true bypass */
static int wv_wob_wet;
static int wv_fla_mix;
static int wv_shf_wet;
static int wv_shf_dep = 64;     /* ramped SHFT ratio index (glide) */
static int wv_pan_dep;
static int wv_wide_cur = 64;

/* SHFT grain state */
static unsigned wv_gpos[2];     /* read position, Q16.16 */
static int wv_age[2];
static int wv_primed;

/* ratio: depth 0..127 -> 2^(semitones/12), semitones -12..+12, Q16.16 */
static const int wv_ratio[128] = {
    32768, 33125, 33486, 33850, 34219, 34591, 34968, 35349,
    35734, 36123, 36516, 36914, 37316, 37722, 38133, 38548,
    38968, 39392, 39821, 40255, 40693, 41136, 41584, 42037,
    42495, 42958, 43425, 43898, 44376, 44859, 45348, 45842,
    46341, 46846, 47356, 47871, 48393, 48920, 49452, 49991,
    50535, 51085, 51642, 52204, 52773, 53347, 53928, 54515,
    55109, 55709, 56316, 56929, 57549, 58176, 58809, 59449,
    60097, 60751, 61413, 62081, 62757, 63441, 64132, 64830,
    65536, 66250, 66971, 67700, 68438, 69183, 69936, 70698,
    71468, 72246, 73032, 73828, 74632, 75444, 76266, 77096,
    77936, 78785, 79642, 80510, 81386, 82273, 83169, 84074,
    84990, 85915, 86851, 87796, 88752, 89719, 90696, 91684,
    92682, 93691, 94711, 95743, 96785, 97839, 98905, 99982,
    101070, 102171, 103283, 104408, 105545, 106694, 107856, 109031,
    110218, 111418, 112631, 113858, 115098, 116351, 117618, 118899,
    120194, 121502, 122825, 124163, 125515, 126882, 128263, 129660,
};

/* raised-cosine (Hann) grain window, 256 entries (Q15), over WV_GRAIN. Smoother
 * than the old triangle (no slope discontinuity at the peak), so the SHFT grain
 * crossfade has far less edge -- the "distortion" at 100% wet. Still satisfies
 * w[(i+128)&255] = 32767 - w[i], so the one-multiply complementary crossfade
 * (sl = sl1 + (sl0-sl1)*w0) is unchanged. */
static const short wv_win[256] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593,
    705, 827, 958, 1098, 1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542,
    2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518, 4799, 5086, 5381, 5682,
    5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378,
    14778, 15178, 15580, 15981, 16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184,
    19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280, 22653, 23023, 23388, 23750,
    24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639,
    30832, 31018, 31194, 31361, 31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369,
    32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762, 32767, 32762, 32747, 32723,
    32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
    31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299,
    29048, 28789, 28523, 28249, 27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817,
    25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023, 22653, 22280, 21903, 21523,
    21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
    16384, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014,
    11628, 11244, 10864, 10487, 10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618,
    7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086, 4799, 4518, 4244, 3978,
    3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
    1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123,
    79, 44, 20, 5,
};

/* store a 24-bit bus sample into an s16 line (saturating at the extreme, so a
 * hot sum above 24-bit wraps instead of folding the sign); the guard sample
 * mirrors index 0 so a fractional read at idx+1 never needs a second mask. */
#define WV_PUT(ch, v) do { short s_ = (short)bx_sat16((v) >> 8); \
    wv_buf[ch][wv_w] = s_; if (wv_w == 0) wv_buf[ch][WV_N] = s_; } while (0)
#define WV_FPUT(ch, v) do { short s_ = (short)bx_sat16((v) >> 8); \
    wv_fbuf[ch][wv_fw] = s_; if (wv_fw == 0) wv_fbuf[ch][WV_FN] = s_; } while (0)

void digiwave_render_in(void)
{
    wv_off = dn_out_off();
}

/* interpolated read at pos (Q16.16), one channel (guard sample => no mask beyond
 * `mask`); `mask` is WV_MASK for the dry line and WV_FMASK for FLAN's line */
static inline int wv_read(const short *line, unsigned pos, unsigned mask)
{
    unsigned idx = (pos >> 16) & mask;
    int frac = (int)((pos >> 1) & 0x7fff);          /* Q15 */
    int a = line[idx];                             /* interpolate in the s16
                                                    * scale, scale to 24-bit once */
    return (a + bx_mul15(line[idx + 1] - a, frac)) << 8;
}

/* A ramp step that always reaches the target: (t-c)>>5 stalls at small
 * differences (e.g. 8>>5 == 0), which left a mode "off" but still running (the
 * flanger kept writing feedback). Snap to 1 near the target instead. */
static inline int wv_step(int cur, int tgt)
{
    int s = (tgt - cur) >> 5;
    if (s == 0 && cur != tgt)
        s = (tgt > cur) ? 1 : -1;
    return s;
}

/* one sine sample (Q15) at a Q32 phase */
static inline int wv_sin(unsigned ph)
{
    unsigned hi = ph >> 24;
    int fr = (int)((ph >> 16) & 0xffu);
    int s0 = sin256[hi & 0xffu];
    int s1 = sin256[(hi + 1) & 0xffu];
    return s0 + (((s1 - s0) * fr) >> 8);
}

static int wv_seq = -1;

/* Drop stale state on a mode change / fresh enable. WOB and SHFT SHARE the dry
 * line (wv_buf) and each writes the live input into it every frame, so it never
 * holds another mode's residue -- clearing it only puts a read discontinuity in
 * the delay. FLAN has its OWN feedback line (wv_fbuf) since the flanger/pitch
 * split, so only that line needs clearing, and only when the returning mode
 * reads it. This removes the 2564-store whole-line flush that was the wave
 * p99/worst spike (`_timing_wave.py`). BX_FULL_FLUSH restores the old behaviour
 * for an A/B. */
static void wv_flush(int mode)
{
    int i, j;
#if defined(BX_FULL_FLUSH)
    (void)mode;
    for (i = 0; i < 2; i++) {
        for (j = 0; j <= WV_N; j++)
            wv_buf[i][j] = 0;
        for (j = 0; j <= WV_FN; j++)
            wv_fbuf[i][j] = 0;
    }
    wv_w = 0;
    wv_fw = 0;
#else
    if (mode == WV_FLA) {
        for (i = 0; i < 2; i++)
            for (j = 0; j <= WV_FN; j++)
                wv_fbuf[i][j] = 0;
        wv_fw = 0;
    }
#endif
    wv_gpos[0] = wv_gpos[1] = 0;
    wv_age[0] = wv_age[1] = 0;
    wv_primed = 0;
}

void digiwave_render_out(void)
{
    int *p, i;
    int mode;
    int depth = digiwave_page ? digiwave_sdepth : digiwave_pdepth;
    int on = (digiwave_page ? digiwave_son : digiwave_on) ? 1 : 0;

    if (digiwave_page) {                        /* STEREO group */
        int b = 2 + ((digiwave_smode * 3) >> 7);
        mode = b > WV_WID ? WV_WID : b;
    } else {                                    /* PITCH group */
        int b = (digiwave_pmode * 2) >> 7;
        mode = b > 1 ? 1 : b;
    }

    if (!on) {
        wv_seq = -1;                            /* flush on the next enable */
    } else if (wv_seq != mode) {
        wv_flush(mode);                         /* mode change: drop the residue */
        wv_seq = mode;
    }

    {
        /* Each group has its own LFO rate now (PITCH = _speed, STEREO = _sspeed);
         * the active group's feeds the shared phase. 0.1..5.0 Hz (milli-Hz): a
         * slower, musical window than the old 1..12 Hz. */
        unsigned spd = (unsigned)(digiwave_page ? digiwave_sspeed : digiwave_speed);
        unsigned mhz = 100u + (spd * 4900) / 127u;
        unsigned inc = (mhz * WV_K / 1000u) * DN_OUT_FRAMES;    /* per block */
        int mix = bx_wet(digiwave_mix * 258);           /* Q15, capped 0.75 */
        int fb = (digiwave_fb * 24575) / 127;           /* Q15, capped at 0.75 */
        unsigned ph = wv_phase;
        int l0 = wv_sin(ph);                            /* block-rate LFO: */
        int lstep = (wv_sin(ph + inc) - l0) >> 5;       /* linear ramp      */
        p = (int *)(DN_OUT_BASE + wv_off);

        if (mode == WV_WOB) {
            int base = 384;                             /* ~8 ms */
            int span = depth * 3;                       /* up to ~8 ms */
            int wet = wv_wob_wet;
            int wet_t = (on && depth > 0) ?
                        bx_wet(32767 * digiwave_pmix / 127) : 0;  /* neutral = dry */
            int wstep;
            unsigned rp, rpstep;
            if (wet_t == 0 && wet == 0) return;         /* true bypass */
            wstep = wv_step(wet, wet_t);
            /* rp is maintained incrementally: each frame it advances one sample
             * (0x10000 in Q16.16) minus the LFO's delay step, so no per-frame
             * multiply. base (384) > span (<=381), so the delay never reaches
             * the wrapper and no clamp is needed. */
            rpstep = 0x10000u - (unsigned)(span * 2 * lstep);
            rp = ((wv_w << 16) + (WV_N << 16) -
                  (unsigned)((base << 16) + span * l0 * 2)) & ((WV_N << 16) - 1);
            bx_emac_enter();
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int l = p[0], r = p[1];
                int wl = wv_read(wv_buf[0], rp, WV_MASK);
                int wr = wv_read(wv_buf[1], rp, WV_MASK);
                p[0] = l + bx_mul15(wl - l, wet);
                p[1] = r + bx_mul15(wr - r, wet);
                WV_PUT(0, l);
                WV_PUT(1, r);
                wv_w = (wv_w + 1) & WV_MASK;
                rp = (rp + rpstep) & ((WV_N << 16) - 1);
                wet += wstep;
                p += 2;
            }
            bx_emac_leave();
            wv_wob_wet = wet;
        } else if (mode == WV_FLA) {
            int base = 128;                             /* ~2.7 ms */
            int span = depth;                           /* up to ~2.7 ms */
            int m = wv_fla_mix;
            int m_t = on ? mix : 0;                     /* neutral = dry */
            int mstep;
            if (m_t == 0 && m == 0) return;
            mstep = wv_step(m, m_t);
            bx_emac_enter();
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int l = p[0], r = p[1];
                int dq = (base << 16) + span * l0 * 2;
                unsigned rp;
                int wl, wr;
                if (dq < (2 << 16)) dq = (2 << 16);
                rp = ((wv_fw << 16) + (WV_FN << 16) - (unsigned)dq) &
                     ((WV_FN << 16) - 1);
                wl = wv_read(wv_fbuf[0], rp, WV_FMASK); /* FLAN's own line */
                wr = wv_read(wv_fbuf[1], rp, WV_FMASK);
                int fl, fr;
                p[0] = l + bx_mul15(wl - l, m);
                p[1] = r + bx_mul15(wr - r, m);
                /* soft limit the feedback so a hot 100%-wet comb peak
                 * compresses instead of hard-clipping (was a hard clamp) */
                fl = bx_soft(l + bx_mul15(wl, fb), 0x700000);
                fr = bx_soft(r + bx_mul15(wr, fb), 0x700000);
                WV_FPUT(0, fl);                         /* never the dry line */
                WV_FPUT(1, fr);
                wv_fw = (wv_fw + 1) & WV_FMASK;
                l0 += lstep;
                m += mstep;
                p += 2;
            }
            bx_emac_leave();
            wv_fla_mix = m;             /* store the RAMped mix, not the target:
                                         * storing `mix` restarted the ramp at
                                         * full mix every block, so FLAN never
                                         * bypassed and buzzed when switched off */
        } else if (mode == WV_SHF) {
            const short *bl = wv_buf[0], *br = wv_buf[1];
            unsigned r0, r1, inc2;
            int a0, a1, ed;
            int w = wv_shf_wet;
            int w_t, wstep;
            wv_shf_dep += (depth - wv_shf_dep) >> 5;    /* glide the ratio */
            ed = wv_shf_dep < 0 ? 0 : wv_shf_dep > 127 ? 127 : wv_shf_dep;
            w_t = (on && ed != 64) ? bx_wet(32767 * digiwave_pmix / 127) : 0;  /* neutral = dry */
            if (w_t == 0 && w == 0) { wv_primed = 0; return; }
            wstep = wv_step(w, w_t);
            inc2 = (unsigned)wv_ratio[ed];
            if (!wv_primed) {
                r0 = r1 = ((wv_w + WV_N - WV_LEAD) & WV_MASK) << 16;
                a0 = 0;
                a1 = WV_GRAIN / 2;
                wv_primed = 1;
            } else {
                r0 = wv_gpos[0];
                r1 = wv_gpos[1];
                a0 = wv_age[0];
                a1 = wv_age[1];
            }
            bx_emac_enter();
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int l = p[0], r = p[1];
                unsigned i0 = (r0 >> 16) & WV_MASK;
                unsigned i1 = (r1 >> 16) & WV_MASK;
                int f0 = (int)((r0 >> 1) & 0x7fff);
                int f1 = (int)((r1 >> 1) & 0x7fff);
                int q0[2], q1[2];                   /* interp MACs, 2 at a time;
                                                     * s16 line: interpolate and
                                                     * crossfade in s16, scale once */
                bx_mul15x2(bl[i0 + 1] - bl[i0], br[i0 + 1] - br[i0], f0, q0);
                bx_mul15x2(bl[i1 + 1] - bl[i1], br[i1 + 1] - br[i1], f1, q1);
                int sl0 = bl[i0] + q0[0];
                int sr0 = br[i0] + q0[1];
                int sl1 = bl[i1] + q1[0];
                int sr1 = br[i1] + q1[1];
                int w0 = wv_win[a0 >> 1];
                int sl = (sl1 + bx_mul15(sl0 - sl1, w0)) << 8;  /* crossfaded grains */
                int sr = (sr1 + bx_mul15(sr0 - sr1, w0)) << 8;
                /* complementary windows -> one mul; then dry<->shifted */
                p[0] = l + bx_mul15(sl - l, w);
                p[1] = r + bx_mul15(sr - r, w);
                WV_PUT(0, l);
                WV_PUT(1, r);
                wv_w = (wv_w + 1) & WV_MASK;

                r0 += inc2; a0++;
                if (a0 >= WV_GRAIN) {
                    a0 = 0;
                    r0 = ((wv_w + WV_N - WV_LEAD) & WV_MASK) << 16;
                }
                r1 += inc2; a1++;
                if (a1 >= WV_GRAIN) {
                    a1 = 0;
                    r1 = ((wv_w + WV_N - WV_LEAD) & WV_MASK) << 16;
                }
                w += wstep;
                p += 2;
            }
            bx_emac_leave();
            wv_gpos[0] = r0;
            wv_gpos[1] = r1;
            wv_age[0] = a0;
            wv_age[1] = a1;
            wv_shf_wet = w;
        } else if (mode == WV_PAN) {
            int dep = wv_pan_dep;
            int d_t = on ? depth * 258 : 0;             /* neutral = centre */
            int dstep;
            if (d_t == 0 && dep == 0) return;           /* true bypass */
            dstep = wv_step(dep, d_t);
            bx_emac_enter();
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int att = bx_mul15(l0, dep);
                int gl = 32767, gr = 32767;
                if (att > 0) gl = 32767 - att;
                else gr = 32767 + att;
                p[0] = bx_mul15(p[0], gl);
                p[1] = bx_mul15(p[1], gr);
                l0 += lstep;
                dep += dstep;
                p += 2;
            }
            bx_emac_leave();
            wv_pan_dep = dep;
        } else {                                        /* WV_WID */
            int d = wv_wide_cur;
            int d_t = on ? depth : 64;                  /* neutral = 64 (1.0) */
            int dstep;
            if (d_t == 64 && d == 64) return;           /* true bypass */
            dstep = wv_step(d, d_t);
            bx_emac_enter();
            for (i = 0; i < DN_OUT_FRAMES; i++) {
                int l = p[0], r = p[1];
                int m = (l + r) >> 1;
                int s = bx_mul14w((l - r) >> 1, d * 256);
                p[0] = m + s;
                p[1] = m - s;
                d += dstep;
                p += 2;
            }
            bx_emac_leave();
            wv_wide_cur = d;
        }
        wv_phase = ph + inc;
    }
}
