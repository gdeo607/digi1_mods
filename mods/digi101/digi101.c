/* DH-101 (mod id digi101): an SH-101-style oscillator section as one of Digi Mono's machines (digimono_ext.h), as
 * Digi Macro is: Digi Mono renders its voices on its own stack, with its overload guard; machine-pages
 * draws its SRC and LFO pages from the page its descriptor names; this mod brings the engine (sh101.c),
 * its state and its page.
 *
 * Two registrations: the machine list entry (core_machines: id 29, DH-101, its icon; ONESHOT's params and
 * render, as Digi Mono's machines; core 3.0's tail: its page) and the engine (Digi Mono's collection
 * digimono_ext).
 *
 * Parts follow Digi Mono and Digi Macro (gdeo607/digi1_mods, MIT): the machine's registration and icon
 * (digimacro.c), the SRC knobs' defaults on a clear (digimono.c, digimono_tick), and the per-pattern map
 * (src/kitstore.h). Firmware addresses come from digi1_mods' notes (docs/TECHNICAL_NOTES.md, tools/os154.json)
 * and this mod's own study of OS 1.54; no firmware code is included beyond the few instructions a hook
 * replaces and replays (glue.s). */
#if defined(HOST_TEST)
#include "../../tests/digi101_host_fw.h"              /* the desktop test's stand-ins for the firmware */
#elif defined(OS154)
#define BMP_VT 0x401b7734u                  /* the firmware's Bitmap vtable (digi1_mods tools/os154.json) */
#define F_UI_KIT 0x4199ec44u                /* the UI's kit pointer (Digi Mono's F_4199dc44) */
#define F_INVALIDATE 0x400c9a3au            /* View::invalidate (Digi Poly's F_400c9812) */
#define F_TEXT 0x400c27a4u                  /* printf-style text into a bitmap (F_400c257c) */
#define F_FONT5 0x40200ebcu                 /* the small font (F_40200b0c) */
#define F_FILLRECT 0x400c1bceu              /* fill a rectangle (F_400c19a6) */
#define F_ACTIVE 0x4197c6b4u                /* the active track (F_4197b6b4) */
#else
#define BMP_VT 0x401b73b4u
#define F_UI_KIT 0x4199dc44u
#define F_INVALIDATE 0x400c9812u
#define F_TEXT 0x400c257cu
#define F_FONT5 0x40200b0cu
#define F_FILLRECT 0x400c19a6u
#define F_ACTIVE 0x4197b6b4u
#endif
#ifndef F_SETPARAM
#ifdef OS154
#define MAIN_VPTR 0x40183268u                   /* MainScreenView's vtable pointer (its ctor, 0x4002f11c) */
#define F_PAGE_SWITCH 0x4002dbfcu               /* MainScreenView: show page (this, page, no_toggle) */
#else
#define MAIN_VPTR 0u                            /* not read on 1.53: the page is found as in 0.8 */
#define F_PAGE_SWITCH 0u
#endif
#define F_SETPARAM 0x400771e8u                  /* the same on OS 1.53 and 1.54 */
#ifdef OS154
#define F_AMP_LEVEL 0x4199ef58u                 /* voice v's AMP envelope level at + 12 v (Q31, signed) */
#else
#define F_AMP_LEVEL 0u                          /* not known on 1.53: ENV has no effect there */
#endif
#define F_ENGINE_KIT 0x800019acu
#define F_VOICE_START 0x80001228u
#endif

#include "digitakt-mk1/core3.h"            /* elekloader's SDK (sdk/include) */
#include "sh101.h"
#include "digimono_ext.h"

/* ---- the engine: a voice's struct sh101_voice in Digi Mono's voice ---------------------------------- */
static void d101_init(void *st)
{
    sh101_init((struct sh101_voice *)st);
}

static void learn_voice(const void *st);

/* "quiet": the AMP level under 2^19 (-72 dB), Digi Mono's own line for a voice that has stopped (it stops
 * rendering such a voice after 32 blocks, so the level last seen here is never above it). The envelope can
 * rest a little above zero after it closes, so "not zero" is not "sounding". */
#define ENVQ_QUIET 8

static void d101_trig(void *st)
{
    struct sh101_voice *v = (struct sh101_voice *)st;
    uint32_t ph = v->ph;
    uint8_t wraps = v->wraps;
    int sounding = v->envq > ENVQ_QUIET;        /* the AMP envelope open at the last block */
    sh101_eco = digimono_eco;                   /* Digi Mono's overload guard (0.23 and later) */
    learn_voice(st);
    sh101_trig(v);
    if (sounding) {                             /* a note over a sounding one: the oscillator runs on, as */
        v->ph = ph;                             /* the 101's VCO does (a restart mid-cycle is a click) */
        v->wraps = wraps;
    } else
        v->fade = 0;                            /* from silence: faded in over 1.3 ms (the mix of saw, */
                                                /* pulse and sub never starts at zero; with AMP ATK 0 */
                                                /* that first step was a click) */
    v->fresh = 8;      /* 5 ms rendered whatever the AMP envelope says */
}

static int voice_of(const void *st);
static int track_port(int t);
static void track_filter(struct sh101_voice *v, int t, uint8_t *q);

#define FADE_N 64                               /* the fade-in from silence: 64 samples, a raised cosine */
static const int16_t fade_gain[FADE_N] = {
    20, 79, 177, 315, 491, 705, 958, 1247, 1573, 1935, 2331, 2761, 3224, 3719, 4244, 4799,
    5381, 5990, 6624, 7281, 7961, 8660, 9379, 10114, 10864, 11628, 12403, 13187, 13980, 14778, 15580, 16383,
    17187, 17989, 18787, 19580, 20364, 21139, 21903, 22653, 23388, 24107, 24806, 25486, 26143, 26777, 27386, 27968,
    28523, 29048, 29543, 30006, 30436, 30832, 31194, 31520, 31809, 32062, 32276, 32452, 32590, 32688, 32747, 32767};
#define AMP_PHASE(v) (*(volatile const int32_t *)(F_AMP_LEVEL - 4u + 12u * (unsigned)(v)))   /* 0: idle */

static void d101_render(void *st, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct sh101_voice *v = (struct sh101_voice *)st;
    int t = voice_of(st), i;
    uint8_t q[DIGIMONO_EXT_KNOBS];
    for (i = 0; i < DIGIMONO_EXT_KNOBS; i++)
        q[i] = p[i];
    p = q;
    v->port = (uint8_t)track_port(t);                   /* this voice's track's PORT (SRC page 2) */
    track_filter(v, t, q);                              /* its filter, from its sound (SRC page 2), and its LFOs */
#ifdef HOST_TEST
    for (i = 0; i < DIGIMONO_EXT_KNOBS; i++)
        host_q[i] = q[i];
#endif
    if (v->fresh)
        v->fresh--;
    else if (t >= 0 && F_AMP_LEVEL && v->envq <= ENVQ_QUIET && !AMP_PHASE(t)) {  /* the AMP idle and quiet */
        sh101_rest(v, p, inc, out, n);                  /* (as Digi Mono's test): the AMP stage would silence it */
        return;
    }
    sh101_render(v, p, inc, out, n);
    if (v->fade < FADE_N) {
        for (i = 0; i < n && v->fade < FADE_N; i++, v->fade++)
            out[i] = (int16_t)((out[i] * (int32_t)fade_gain[v->fade]) >> 15);
    }
}

/* ---- the knobs, in Digi Mono's order B C E F G H D (sh101.h) ----------------------------------------- */
static const int8_t knob_p[DIGIMONO_EXT_KNOBS] = {SH_RNGE, SH_SAW, SH_PULS, SH_PW, SH_PWM, SH_SUB, SH_NOIS};
static const uint8_t knob_def[DIGIMONO_EXT_KNOBS] = {32, 100, 0, 0, 0, 0, 0};    /* 8', saw, PW MAN */
static const char *const knob_sname[DIGIMONO_EXT_KNOBS] = {"RNGE", "SAW", "PULS", "PW", "PWM", "SUB", "NOIS"};
static const char *const knob_lname[DIGIMONO_EXT_KNOBS] = {
    "Range", "Saw Level", "Pulse Level", "Pulse Width", "PWM Source", "Sub Osc", "Noise Level"};

static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

static char *put_u(char *b, int v)              /* 0..999 */
{
    if (v >= 100)
        *b++ = (char)('0' + v / 100);
    if (v >= 10)
        *b++ = (char)('0' + v / 10 % 10);
    *b++ = (char)('0' + v % 10);
    *b = 0;
    return b;
}

/* B: 16' 8' 4' 2'; G: MAN, LFO n, ENV n; H: the sub's mode and level. The others: the plain number. */
static int knob_text(int k, int v, char *buf)
{
    static const char *const range[4] = {"16'", "8'", "4'", "2'"};
    static const char *const pwm[3] = {"MAN", "LFO ", "ENV "};
    static const char *const sub[3] = {"-1 ", "-2 ", "-2P "};
    switch (k) {
    case 0:
        put_s(buf, range[sh101_range(v)]);
        return 1;
    case 4: {
        int m = sh101_pwm_mode(v);
        char *b = put_s(buf, pwm[m]);
        if (m)
            put_u(b, sh101_pwm_amount(v));
        return 1;
    }
    case 5:
        put_u(put_s(buf, sub[sh101_sub_mode(v)]), sh101_sub_level(v));
        return 1;
    }
    return 0;
}

const struct digimono_ext digi101_ext = {
    29, sizeof(struct sh101_voice), "101", knob_p, knob_def, knob_sname, knob_lname, knob_text,
    d101_init, d101_trig, d101_render,
};

/* ---- the machine list's entry (core 2.1 core_machines) ------------------------------------------------ *
 * The icon is a Bitmap as the firmware's own: vtable, width 11, height 7, 1, pixels, mask; one 32-bit
 * word a column, the top row in bit 25. It reads "101". */
static const uint32_t icon_px[11] = {
    0x84000000, 0xfe000000, 0x80000000, 0x00000000, 0x7c000000, 0x82000000,
    0x7c000000, 0x00000000, 0x84000000, 0xfe000000, 0x80000000,
};
static const uint32_t icon_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
struct bitmap {
    uint32_t vt, w, h, one;
    const uint32_t *px, *mask;
    uint32_t end;
};
static const struct bitmap icon = {BMP_VT, 11, 7, 1, icon_px, icon_mask, 0};

/* Its SRC and LFO pages, drawn by machine-pages (core 3.0; digitakt-mk1/core3.h; Digi Mono 0.24 and later):
 * SLICE's page, B RNGE, C SAW, D NOIS, E PULS, F PW, G PWM, H SUB, each a 0..127 knob drawn as BR's, its value
 * text from Digi Mono's (which asks knob_text above). On OS 1.54 three of them are other destinations on the
 * LFO page (lfo_route below): B Filter Freq, F PWM Source, G Filter Reso. The LFO page's group is "101". */
#define P_BR 0x86
#define KNOB(sn, ln, d, i, f) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | (f), \
    .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = digimono_fmt_##i
#if defined(OS154) || defined(HOST_TEST)
#define LFO(sn, ln) , .lfo = sn, .lfo_long = ln
#else
#define LFO(sn, ln)
#endif
static const struct cm_ui page = {
    .abi = cm_ui_v3, .page_from = 3, .group = "101",
    .knob = {
        [1] = KNOB("RNGE", "Range", 32, 1, 0) LFO("FREQ", "Filter Freq") },
        [2] = KNOB("SAW", "Saw Level", 100, 2, 0) },
        [3] = KNOB("NOIS", "Noise Level", 0, 3, CM_NOT_SAMPLE) },
        [4] = KNOB("PULS", "Pulse Level", 0, 4, 0) },
        [5] = KNOB("PW", "Pulse Width", 0, 5, 0) LFO("PWM", "PWM Source") },
        [6] = KNOB("PWM", "PWM Source", 0, 6, 0) LFO("RESO", "Filter Reso") },
        [7] = KNOB("SUB", "Sub Osc", 0, 7, 0) },
    },
};
#undef KNOB
#undef LFO

/* id, name, short name, icon, params (ONESHOT), render (ONESHOT); core 3.0's tail: its page */
const struct cm_machine digi101_m29 = {29, "DH-101", "101", &icon, 0, 0, CM_UI_TAG, &page};

/* ---- CLEAR PAGE ------------------------------------------------------------------------------------ *
 * The clear page on SRC resets B..H to ONESHOT's defaults, which mean nothing to DH-101 (16',
 * no saw, a loud 25 % sub). Digi Mono gives a machine its defaults on a machine switch only; this does the
 * same on a clear, as Digi Mono does a switch (digimono.c, digimono_tick): ev_tick, 30 Hz, watches the UI
 * kit; when a DH-101 track's B C E F G H turn, several at once, into exactly ONESHOT's defaults, it sets
 * DH-101's own through the firmware's knob path and in the kit. A new kit (pattern or kit load) is only
 * looked at, never changed. */
#define UI_KIT   (*(uint8_t *volatile const *)F_UI_KIT)          /* its sounds: + 0x20 + 0xa2 t */
typedef void (*setparam_t)(int32_t value, int32_t voice, int32_t slot);
#define SETPARAM ((setparam_t)F_SETPARAM)                        /* the same on OS 1.53 and 1.54 */

static const uint8_t knob_slot[DIGIMONO_EXT_KNOBS] = {18, 19, 21, 22, 23, 24, 20};   /* B C E F G H D */
static const uint16_t oneshot_def[6] = {0x0300, 0x0000, 0x0000, 0x7800, 0x0000, 0x6400};  /* B C E F G H */

static const uint8_t *seen_kit;
static uint8_t seen_mach[8];
static uint16_t seen_w[8][6];

static void page_tick(void *ctrl);

void digi101_tick(void *ctrl)
{
    uint8_t *kit = UI_KIT;
    int t, i;
    page_tick(ctrl);
    if (!kit)
        return;
    for (t = 0; t < 8; t++) {
        uint8_t *snd = kit + 0x20 + 0xa2 * t;
        const uint16_t *kw = (const uint16_t *)(snd + 0x14);
        int changed = 0, cleared = 1;
        for (i = 0; i < 6; i++) {
            uint16_t w = kw[knob_slot[i]];
            if (w != seen_w[t][i])
                changed++;
            if (w != oneshot_def[i])
                cleared = 0;
        }
        if (kit == seen_kit && snd[0x7e] == 29 && seen_mach[t] == 29 && cleared && changed >= 2) {
            for (i = 0; i < DIGIMONO_EXT_KNOBS; i++) {
                int32_t w = (int32_t)knob_def[i] << 8;
                *(uint16_t *)(snd + 0x14 + 2 * knob_slot[i]) = (uint16_t)w;
                SETPARAM(w, t, knob_slot[i]);
            }
        }
        seen_mach[t] = snd[0x7e];
        for (i = 0; i < 6; i++)
            seen_w[t][i] = kw[knob_slot[i]];
    }
    seen_kit = kit;
}


/* ---- PORT: per track, saved with the pattern -------------------------------------------------------- *
 * In the persistent map of the kit (kitstore.h, digi1_mods src/kitstore.h, MIT): 48 bytes a pattern that
 * the firmware saves and loads but never uses, shared by the mods. Digi EQ uses bytes 6t+4 and 6t+5 for
 * tracks 1..4 only, so those of tracks 5..8 (28 29 34 35 40 41 46 47) are free: one byte a track, here
 * bits 0..6 PORT and bit 7 kept for LEG. Track t (5..8) keeps its byte in its own sound (6t+4), track t
 * (1..4) in track t+4's (6(t+4)+5). 0, what every existing kit holds, is PORT off. As the other mods, a
 * RAM-only marker slot (48, unused by the other mods) tells a sound load from a kit load, so loading a
 * sound onto a track keeps every track's PORT. */
#include "kitstore.h"
#define KS_SLOT_D101 48                  /* RAM-only (46 and 47 are saved now: glue.s) */
#define ENGINE_KIT   (*(uint8_t *volatile const *)F_ENGINE_KIT)
static struct kstore store;

static int pbyte(int t)
{
    return t >= 4 ? 6 * t + 4 : 6 * (t + 4) + 5;
}

static unsigned char d101_mask(int n)
{
    return n >= 28 && n % 6 >= 4 ? 0xff : 0;
}

static int track_port(int t)
{
    const uint8_t *kit = ENGINE_KIT;
    if ((unsigned)t > 7 || !kit)
        return 0;
    return XB(kit, pbyte(t)) & 0x7f;
}

/* ---- the filter: in the sound, parameter slots 46 and 47 ------------------------------------------ *
 * glue.s saves and loads slots 46 and 47 with the rest of the sound (kits, project, sound pool, the
 * power-off mirror), so the filter is part of the sound, as FLTR's settings are. 32 bits for seven knobs:
 *   slot 46: bits 0..6 CUT (stored as 127 - CUT), 7..11 RES (32 steps), 12..15 VIB (16 steps)
 *   slot 47: bits 0..4 ENV (32), 5..9 MOD (32), 10..11 KYB (OFF 33 67 100 %), 12..15 RATE (16, stored
 *            10 steps round, so 0 is 4.5 Hz)
 * Every existing sound holds 0 there: the filter open and out of the way (CUT 127, the rest 0). */
enum { F_CUT, F_RES, F_ENV, F_MOD, F_KYB, F_VIB, F_RATE, F_N };
static const uint8_t f_word[F_N] = {46, 46, 47, 47, 47, 46, 47};
static const uint8_t f_shift[F_N] = {0, 7, 0, 5, 10, 12, 12};
static const uint8_t f_bits[F_N] = {7, 5, 5, 5, 2, 4, 4};
#define SLOTW(kit, t, s) ((volatile uint16_t *)((char *)(kit) + 0x20 + (t) * 0xa2 + 0x14 + 2 * (s)))
#define AMP_LEVEL(v) (*(volatile const int32_t *)(F_AMP_LEVEL + 12u * (unsigned)(v)))

/* the knob's step (0 .. 2^bits - 1) as stored, and back */
static int f_step(const uint8_t *kit, int t, int i)
{
    int s = (*SLOTW(kit, t, f_word[i]) >> f_shift[i]) & ((1 << f_bits[i]) - 1);
    if (i == F_CUT)
        s = 127 - s;
    else if (i == F_RATE)
        s = (s + 10) & 15;
    return s;
}

static void f_set(uint8_t *kit, int t, int i, int s)
{
    volatile uint16_t *w = SLOTW(kit, t, f_word[i]);
    int m = ((1 << f_bits[i]) - 1) << f_shift[i];
    if (i == F_CUT)
        s = 127 - s;
    else if (i == F_RATE)
        s = (s + 6) & 15;
    *w = (uint16_t)((*w & ~m) | ((s << f_shift[i]) & m));
}

/* the step as 0..127, as the engine takes it */
static int f_value(int i, int s)
{
    int top = (1 << f_bits[i]) - 1;
    return (s * 127 + top / 2) / top;
}

static int active_is_101(void);

/* ---- the LFO page's destinations on a DH-101 track (OS 1.54; glue.s digi101_slot2id) ---------------- *
 * An LFO's DEST is a sound slot; SRC's knobs are slots 17..24, the firmware's parameter ids 108..115
 * (ONESHOT's), and Digi Mono names them after the machine's knobs. On a DH-101 track three of them mean
 * something else (named so on the LFO page by the machine's page above, lfo and lfo_long):
 *   slot 18 (RNGE's) "Filter Frequency": page 2's FREQ
 *   slot 23 (PWM's)  "Resonance":        page 2's RESO
 *   slot 22 (PW's)   "PWM Source":       the PWM knob, kept in MAN and LFO (0..68: full depth sweeps
 *                                         exactly that span; a knob in ENV is left alone)
 * and the list shows them TUNE SAW NOIS PULS PWM-Source SUB, then FREQ RESO (just above the FILTER page's
 * own): the list is built slot by slot (0x40011348), so from there alone slots 17..24 answer the ids in
 * that order; everywhere else a slot keeps its own id, and an id its slot, so DEST still holds 18/22/23.
 * The firmware's LFO stage adds the LFO into the smoothed word of the slot (0x80002772 + 106 v + 2 s,
 * clamped to 0..32512), which Digi Mono hands the engine as RNGE/PW/PWM: those three are taken back out
 * (the word less the LFO, or the kit's value where the stage clamped), and the LFO goes where it is aimed.
 * The LFO's value (already faded) is at 0x421f4e14 + 80 v (LFO2 + 0x28), DEP in the smoothed word 8 / 16;
 * the amount is as the stage's: value (DEP - 16384) 2, full depth the whole 0..32512. */
#define D_FREQ   18                             /* RNGE's slot */
#define D_PWMSRC 22                             /* PW's */
#define D_RESO   23                             /* PWM's */
#define PWM_TOP  68                             /* the PWM knob's last value before ENV */
#ifndef F_LFO_VAL
#ifdef OS154
#define F_LFO_VAL   0x421f4e14u                 /* digi1_mods matrix.c (OS 1.53: 0x421f3e14) */
#else
#define F_LFO_VAL   0u
#endif
#define F_VOICE_WORDS 0x80002772u              /* the smoothed parameter words, 106 bytes a voice */
#endif
#define LFO_VAL(v, n) (*(volatile const int32_t *)(F_LFO_VAL + 80u * (unsigned)(v) + ((n) ? 0x28u : 0u)))
#define VWORD(v, s)   (*(volatile const int16_t *)(F_VOICE_WORDS + 106u * (unsigned)(v) + 2u * (unsigned)(s)))

static int clamp127(int x) { return x < 0 ? 0 : x > 127 ? 127 : x; }

/* q: the engine's parameters (a copy of Digi Mono's); val: page 2's knobs 0..127 */
static void lfo_route(const uint8_t *kit, int t, uint8_t *q, int *val)
{
    static const uint8_t dslot[3] = {D_FREQ, D_PWMSRC, D_RESO};
    static const int8_t pidx[3] = {SH_RNGE, SH_PW, SH_PWM};       /* what the stage moved instead */
    int32_t acc[3] = {0, 0, 0};
    int hit[3] = {0, 0, 0}, n, i, dpwm = 0;
    if (!F_LFO_VAL)
        return;
    for (n = 0; n < 2; n++) {
        int dest = (int8_t)(*SLOTW(kit, t, n ? 12 : 4) >> 8);
        int32_t mult;
        for (i = 0; i < 3 && dest != dslot[i]; i++)
            ;
        if (i == 3)
            continue;
        mult = ((int32_t)VWORD(t, n ? 16 : 8) - 16384) * 2;            /* DEP, as the stage */
        acc[i] += ((LFO_VAL(t, n) >> 16) * mult) >> 15;                /* in the word's units */
        hit[i] = 1;
    }
    for (i = 0; i < 3; i++) {
        int32_t w, base;
        int d;
        if (!hit[i])
            continue;
        w = VWORD(t, dslot[i]);
        if (w > 0 && w < 32512)
            base = w - acc[i];                  /* the knob (smoothed, p-locks and all) */
        else
            base = (int32_t)*SLOTW(kit, t, dslot[i]);   /* the stage clamped: the kit's own */
        q[pidx[i]] = (uint8_t)clamp127((int)((base + 128) >> 8));   /* rounded: a unit of LFO off is no step */
        d = (int)((acc[i] + 128) >> 8);         /* 32512 a full range -> 127 */
        if (dslot[i] == D_FREQ)
            val[F_CUT] = clamp127(val[F_CUT] + d);
        else if (dslot[i] == D_RESO)
            val[F_RES] = clamp127(val[F_RES] + d);
        else
            dpwm = d;
    }
    if (hit[1] && q[SH_PWM] <= PWM_TOP) {      /* PWM Source, after RESO has put the knob back */
        int x = q[SH_PWM] + (dpwm * (PWM_TOP + 1)) / 128;
        q[SH_PWM] = (uint8_t)(x < 0 ? 0 : x > PWM_TOP ? PWM_TOP : x);
    }
}

/* the list builder's call (OS 1.54): its slots 17..24 in the list's order */
#define LIST_FROM 0x400113beu
static const uint8_t list_id[8] = {108, 110, 111, 112, 113, 115, 109, 114};
/* TUNE SAW NOIS PULS PWM-Source(PW's) SUB FREQ(RNGE's) RESO(PWM's) */

int d101_slot2id(uint32_t caller, int32_t slot, int32_t machine);
int d101_slot2id(uint32_t caller, int32_t slot, int32_t machine)
{
    if (machine != 29 || caller != LIST_FROM || slot < 17 || slot > 24)
        return -1;
    return list_id[slot - 17];
}

static void track_filter(struct sh101_voice *v, int t, uint8_t *q)
{
    const uint8_t *kit = ENGINE_KIT;
    int32_t a;
    if ((unsigned)t > 7 || !kit)
        return;
    {
        int val[F_N], i;
        uint8_t *f[F_N];
        f[F_CUT] = &v->f_cut; f[F_RES] = &v->f_res; f[F_ENV] = &v->f_env; f[F_MOD] = &v->f_mod;
        f[F_KYB] = &v->f_kyb; f[F_VIB] = &v->f_vib; f[F_RATE] = &v->f_rate;
        for (i = 0; i < F_N; i++)
            val[i] = f_value(i, f_step(kit, t, i));
        lfo_route(kit, t, q, val);              /* the track's LFOs aimed at FREQ, RESO, PWM Source */
        for (i = 0; i < F_N; i++)
            *f[i] = (uint8_t)val[i];
    }
    a = F_AMP_LEVEL ? AMP_LEVEL(t) : 0;         /* the track's AMP envelope, for ENV */
    if (a < 0)
        a = a == (int32_t)0x80000000 ? 0x7fffffff : -a;
    v->envq = (uint16_t)(a >> 16);
}

/* ---- the track's name in the firmware's messages (glue.s; OS 1.54) ------------------------------------- *
 * TRK + a trig prints "<short name>: <sample>", OFF when there is none: "101: OFF" says nothing on a
 * synth. A DH-101 track's message is "DH-101" alone; every other machine's is as before. */
#ifdef OS154
#define F_MSHORT 0x4007912cu                    /* the machine's short name (core's for added machines) */
#endif
const char digi101_popup_fmt[] = "%s%.16s";
const char *digi101_popup_name(int id, const char **sample);
const char *digi101_popup_name(int id, const char **sample)
{
#ifdef F_MSHORT
    static char buf[24];
    const char *n = ((const char *(*)(int))F_MSHORT)(id);
    int i = 0;
    if (id == 29) {
        *sample = "";
        return "DH-101";
    }
    while (n && n[i] && i < 20) {
        buf[i] = n[i];
        i++;
    }
    buf[i++] = ':';
    buf[i++] = ' ';
    buf[i] = 0;
    return buf;
#else
    (void)id;
    (void)sample;
    return "";
#endif
}

/* ---- which voice a state belongs to -------------------------------------------------------------- *
 * Digi Mono hands a machine its voice's state, not the voice's number. On the unit voice v plays
 * track v, so the voice is learnt at its trigs: the voices that (re)start this block are bits of
 * 0x80001228 (Digi Mono reads the same); of those, a DH-101 track's voice not yet known as another state's
 * is this one. A trig that is the only DH-101 start of its block is certain and corrects any guess. */
#define VOICE_START  (*(volatile const uint32_t *)F_VOICE_START)
extern volatile uint8_t core_track_machine[8];
static const void *vstate[8];

static int voice_of(const void *st)
{
    int v;
    for (v = 0; v < 8; v++)
        if (vstate[v] == st)
            return v;
    return -1;
}

static void learn_voice(const void *st)
{
    uint32_t bits = VOICE_START;
    int v, n = 0, pick = -1, mine = -1;
    for (v = 0; v < 8; v++) {
        if (!((bits >> v) & 1) || core_track_machine[v] != 29)
            continue;
        n++;
        if (vstate[v] == st)
            mine = v;
        else if (!vstate[v] && pick < 0)
            pick = v;
    }
    if (n == 1) {                               /* the only DH-101 start: certain */
        for (v = 0; v < 8; v++)
            if (((bits >> v) & 1) && core_track_machine[v] == 29)
                pick = v;
    } else if (mine >= 0)
        return;                                 /* already known */
    if (pick < 0)
        return;
    for (v = 0; v < 8; v++)
        if (vstate[v] == st)
            vstate[v] = 0;
    vstate[pick] = st;
}

/* ---- SRC page 2: the DH-101 page ------------------------------------------------------------------ *
 * The SRC key a second time on a DH-101 track's SRC page shows this page in place of the sample's
 * waveform (which a DH-101 track has no use for); SRC again, NO, or any page key goes back. Through
 * core's hook bus only (ev_key, ev_enc, ev_draw, ev_tick), as Digi Matrix's page: the firmware's own page
 * code is not touched. Page keys are key ids 19..23 (TRIG SRC FLTR AMP LFO: 0x4002e1f8 calls the page
 * switch 0x4002dbfc with id - 19); with FUNC held they are the stock's (FUNC+SRC: the machine list).
 * Knob A is PORT; B..H are kept for what comes next (LEG). */
#define INVALIDATE   ((void (*)(void *))F_INVALIDATE)
#define TEXT         ((void (*)(void *, const void *, int, int, int, const char *, ...))F_TEXT)
#define FONT5        ((const void *)F_FONT5)
#define FILLRECT     ((void (*)(void *, int, int, int, int, int))F_FILLRECT)
#define ACTIVE_TRACK (*(volatile const uint32_t *)F_ACTIVE)
#define KEYID(ev)    (*(int *)((char *)(ev) + 12))
#define KEYFL(ev)    (*(int *)((char *)(ev) + 16))
#define K_PAGE0      19                         /* TRIG; SRC 20, FLTR 21, AMP 22, LFO 23 */
#define K_SRC        20
#define K_NO         13

/* The main screen (OS 1.54, read from 0x4002dbfc and drawAll 0x400ca5aa): the view controller ctrl that
 * ev_tick and ev_draw get keeps its views in a list (node: next +0, view +8; sentinel ctrl + 20, first
 * node at ctrl + 28); the MainScreenView among them is the one whose vtable pointer is MAIN_VPTR. Its
 * page switch 0x4002dbfc(this, page, flag) shows a page (TRIG 0, SRC 1, ...); flag 1 keeps it from
 * toggling the page's second view, so it rebuilds the page as it is, knobs and all (it invalidates the
 * screen). The page shown is this + 180, the pages of an audio track this + 236 + 8 page (+ 300 for a
 * MIDI track). Not found, or another OS: the page is followed from the page keys, as in 0.8. */
#define PAGE_SWITCH  ((void (*)(void *, int, int))F_PAGE_SWITCH)
#ifndef CTRL_FIRST
#define CTRL_FIRST   28                         /* the first node; host tests move it past the 8-byte pointers */
#endif
static char *main_view;

static void find_main(void *ctrl)
{
    char *c = (char *)ctrl, *n;
    int i;
    if (!MAIN_VPTR || !c)
        return;
    main_view = 0;                              /* looked up every frame: a screen that closes can take
                                                 * the main screen with it and the firmware makes a new
                                                 * one, so a pointer kept from before would be stale */
    n = *(char **)(c + CTRL_FIRST);
    for (i = 0; i < 16 && n && n != c + 20; i++) {
        char *v = *(char **)(n + 8);
        if (v && *(uint32_t *)v == MAIN_VPTR) {
            main_view = v;
            return;
        }
        n = *(char **)n;
    }
}

static int main_ok(void)
{
    return MAIN_VPTR && main_view && *(uint32_t *)main_view == MAIN_VPTR;
}

/* Something on top of the main screen. drawAll (0x400ca5aa) draws the list's views in order, each over
 * the last, so a screen or pop-up opened over the main one (Imp/Exp, Save Project, Direct, Tap Tempo, the
 * copy and paste messages) is a view after it. The views after the main screen when page 2 opens are
 * noted; any other view after it later (or the main screen gone from the list) is a screen on top, and
 * the page steps aside: it draws nothing and lets the knobs through, and comes back when that screen
 * closes. A key pressed meanwhile is the screen's, and closes page 2 (SRC opens it again, noting afresh).
 * (0.16-0.18 compared counts against the fewest ever seen, which a moment with fewer views could lower
 * for good: page 2 then stayed hidden.) */
static char *last_ctrl;
#define SNAP_N 12
static char *snap[SNAP_N];                      /* the views after the main screen when page 2 opened */
static int nsnap;

/* calls f(view) for each view after the main screen; -1 if the main screen is not in the list */
static int after_main(char **out, int max)
{
    char *c = last_ctrl, *n;
    int i, after = -1;
    if (!c || !main_ok())
        return -1;
    n = *(char **)(c + CTRL_FIRST);
    for (i = 0; i < 32 && n && n != c + 20; i++) {
        char *v = *(char **)(n + 8);
        if (after >= 0) {
            if (after < max)
                out[after] = v;
            after++;
        } else if (v == main_view)
            after = 0;
        n = *(char **)n;
    }
    return after;
}

static void note_views(void)
{
    int a = after_main(snap, SNAP_N);
    nsnap = a < 0 ? 0 : a > SNAP_N ? SNAP_N : a;
}

static int covered(void)
{
    char *now[SNAP_N];
    int a, i, j;
    if (!last_ctrl || !MAIN_VPTR)
        return 0;                               /* nothing known (another OS): as before */
    if (!main_ok())
        return 1;                               /* the main screen gone from the list: a screen over it */
    a = after_main(now, SNAP_N);
    if (a < 0 || a > SNAP_N)
        return 1;
    for (i = 0; i < a; i++) {
        for (j = 0; j < nsnap && snap[j] != now[i]; j++)
            ;
        if (j == nsnap)
            return 1;                           /* a view that was not there when the page opened */
    }
    return 0;
}

static int page_open;                           /* the DH-101 page is up */
static int slot_dirty;                          /* tracks whose slots 46/47 changed: into the mirror */
static int cur_page = -1;                       /* the page last chosen with a page key (0 TRIG, 1 SRC..) */
static int ate_src;                             /* we took an SRC press: take its release and repeats too */

/* the active track's sound is a DH-101: read from the UI kit (core_track_machine is the render's, which
 * takes a track's machine only when it plays, so a track just turned to DH-101 would not open page 2) */
static int active_is_101(void)
{
    uint32_t t = ACTIVE_TRACK;
    const uint8_t *kit = UI_KIT;
    return t < 8 && kit && kit[0x20 + 0xa2 * t + 0x7e] == 29;
}

/* the SRC page is the one shown: read from the main screen when found, else as the keys said */
static int on_src_page(void)
{
    if (main_ok() && ACTIVE_TRACK < 8)
        return *(void **)(main_view + 180) == *(void **)(main_view + 236 + 8);
    return cur_page == 1;
}

/* back to the SRC page's own view, redrawn whole */
static void close_page(void)
{
    page_open = 0;
    if (main_ok())
        PAGE_SWITCH(main_view, 1, 1);
}

int digi101_key(void *brain, void *ev)
{
    int id = KEYID(ev), fl = KEYFL(ev), press = (fl & 1) && !(fl & 8);
    (void)brain;
    if (page_open && covered()) {               /* a screen on top: its keys; page 2 steps out (so a */
        page_open = 0;                          /* screen that stays never leaves it unreachable) */
        return 0;
    }
    if (id >= K_PAGE0 && id <= K_PAGE0 + 4) {
        if (!press)
            return id == K_SRC && ate_src;
        ate_src = 0;
        if (fl & 2) {                           /* FUNC + a page key: the stock's */
            page_open = 0;
            cur_page = -1;
            return 0;
        }
        if (id == K_SRC && (page_open || on_src_page())) {   /* SRC again, on the SRC page */
            if (page_open) {
                close_page();
                ate_src = 1;
                cur_page = 1;
                return 1;
            }
            if (active_is_101()) {
                note_views();
                page_open = 1;
                ate_src = 1;
                cur_page = 1;
                return 1;
            }
            return 0;                           /* another machine: its waveform page */
        }
        page_open = 0;
        cur_page = id - K_PAGE0;
        return 0;
    }
    if (page_open && id == K_NO) {
        if (press)
            close_page();
        return 1;
    }
    return 0;
}

static void name_window(const uint8_t *kit, int t, int k);

int digi101_enc(void *brain, void *ev)
{
    int id = KEYID(ev), delta = KEYFL(ev);
    uint32_t t = ACTIVE_TRACK;
    uint8_t *kit;
    (void)brain;
    if (!page_open || covered())
        return 0;
    if (id < 1 || id > 8)
        return 0;                               /* LEVEL and the rest: as ever */
    kit = UI_KIT;
    if (!kit || t > 7 || !active_is_101())
        return 1;
    if (id == 1) {                              /* A: PORT */
        int n = pbyte((int)t), b = XB(kit, n), v = (b & 0x7f) + delta;
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        XB(kit, n) = (unsigned char)((b & 0x80) | v);
    } else {                                    /* B..H: the filter, one step a detent */
        int i = id - 2, top = (1 << f_bits[i]) - 1, v = f_step(kit, (int)t, i) + delta;
        v = v < 0 ? 0 : v > top ? top : v;
        f_set(kit, (int)t, i, v);
        slot_dirty |= 1 << t;                   /* into the power-off mirror at the next tick */
    }
    name_window(kit, (int)t, id - 1);
    return 1;                                   /* and not the hidden SRC page's knobs */
}

static const char *const rate_hz[16] = {
    ".10", ".14", ".21", ".31", ".46", ".66", ".99", "1.4", "2.1", "3.0", "4.5", "6.5", "9.8", "14", "21", "30"};
static const char *const kyb_txt[4] = {"OFF", "33%", "67%", "100%"};
static const char *const f_name[F_N] = {"FREQ", "RESO", "ENV", "MOD", "KYB", "VIB", "RATE"};
static const char *const f_lname[F_N] = {"Filter Frequency", "Resonance", "Envelope Depth", "LFO Depth",
                                         "Keyboard Follow", "Vibrato", "LFO Rate"};

/* The stock knob graphic (OS 1.54): the parameter pages draw a knob as one of the frames of the firmware's
 * knob sprites, a vector of Bitmaps at 0x421f94c4 (filled at boot), frame 12 + (value 72 + 16256) / 32512
 * for a value 0..127 << 8 (0x4005ffd4), with the blit 0x400c2b88(bmp, sprite, x, y, 0). Each frame is
 * checked before use (a Bitmap of a sane size); if anything looks wrong the page draws bars instead. */
#ifndef F_BLIT
#ifdef OS154
#define F_BLIT       0x400c2b88u
#define F_KNOB_ATLAS 0x421f94c4u
#else
#define F_BLIT       0u
#define F_KNOB_ATLAS 0u
#endif
#endif
#define BLIT ((void (*)(void *, const void *, int, int, int))F_BLIT)
#define BMP_VT2 0x401b774cu                     /* the Bitmap subclass the firmware's own images use */

static const uint32_t *knob_sprite(int v)
{
    const uintptr_t *vec = (const uintptr_t *)F_KNOB_ATLAS;
    uintptr_t b, e;
    const uint32_t *bm;
    unsigned n, i;
    if (!F_KNOB_ATLAS || !F_BLIT)
        return 0;
    b = vec[0];
    e = vec[1];
    if (!b || e <= b || (e - b) % 28 || (e - b) / 28 > 400)
        return 0;
    n = (unsigned)((e - b) / 28);
    i = 12 + (unsigned)(((v << 8) * 72 + 16256) / 32512);
    if (i >= n)
        i = n - 1;
    bm = (const uint32_t *)(b + 28 * i);
    if ((bm[0] != (uint32_t)BMP_VT && bm[0] != BMP_VT2) || !bm[1] || bm[1] > 24 || !bm[2] || bm[2] > 18)
        return 0;
    return bm;
}

static int text_w(const char *s)                /* the 5-px font: 4 px a character and a space */
{
    int n = 0;
    while (s[n])
        n++;
    return n ? 4 * n - 1 : 0;
}

/* The firmware's own text (OS 1.54): 0x400c29f4(bmp, font, x, y, align, 0, template, fmt, ...) draws fmt
 * centred (align 1) in the width of template; the SRC page's knob names are font 0x401fe44c in the width of
 * "XXXX" (0x40030d56). The name window is the main screen's message (0x40093ab0(main + 156, fmt, ...),
 * as its keys use it, 0x4002e046): "%s=%s", the long name and the value, as the stock knobs show it. */
#ifndef F_TEXT2
#ifdef OS154
#define F_TEXT2      0x400c29f4u
#define F_LABEL_FONT 0x401fe44cu
#define F_POPUP      0x40093ab0u
#else
#define F_TEXT2      0u
#define F_LABEL_FONT 0u
#define F_POPUP      0u
#endif
#endif
#define TEXT2 ((void (*)(void *, const void *, int, int, int, int, const char *, const char *, ...))F_TEXT2)
#define POPUP ((void (*)(void *, const char *, ...))F_POPUP)

/* knob k's value as text into buf (8 at most); its 0..127 into *v */
static const char *knob_value(const uint8_t *kit, int t, int k, char *buf, int *v)
{
    if (k == 0) {
        int port = XB(kit, pbyte(t)) & 0x7f;
        *v = port;
        put_u(buf, port);
        return port ? buf : "OFF";
    }
    {
        int i = k - 1, s = f_step(kit, t, i);
        *v = f_value(i, s);
        if (i == F_KYB)
            return kyb_txt[s];
        if (i == F_RATE) {
            put_s(put_s(buf, rate_hz[s]), "Hz");
            return buf;
        }
        put_u(buf, *v);
        return buf;
    }
}

static void name_window(const uint8_t *kit, int t, int k)
{
    char buf[8];
    int v;
    const char *txt = knob_value(kit, t, k, buf, &v);
    if (!F_POPUP || !main_ok())
        return;
    POPUP(*(void **)(main_view + 156), "%s=%s", k ? f_lname[k - 1] : "Portamento", txt);
}

/* knob k (0 A .. 7 H) exactly where the SRC page puts its own (its grid 0x40031802: x 24 + 26 column, the
 * upper row's cell at y 27 and the lower's at 0, y up; the cell 0x40030c0c: the knob's sprite at (x, y + 7),
 * its name centred on x + 9 at y). The track's SMP and LEV column stays at the left. Without the sprites,
 * the value over the name and a bar. */
static void cell(void *bmp, int k, const char *name, const char *txt, int v)
{
    int x = 24 + 26 * (k & 3), y = k < 4 ? 27 : 0, cx = x + 9;
    const uint32_t *sp = knob_sprite(v);
    if (F_TEXT2 && F_LABEL_FONT)
        TEXT2(bmp, (const void *)F_LABEL_FONT, cx, y, 1, 0, "XXXX", "%s", name);
    else
        TEXT(bmp, FONT5, cx - text_w(name) / 2, y, -1, "%s", name);
    if (sp) {
        BLIT(bmp, sp, x, y + 7, 0);
        return;
    }
    TEXT(bmp, FONT5, cx - text_w(txt) / 2, y + 12, -1, "%s", txt);
    FILLRECT(bmp, cx - 9, y + 8, cx + 9, y + 8, -1);
    if (v > 0)
        FILLRECT(bmp, cx - 9, y + 7, cx - 9 + (v * 18 + 63) / 127, y + 9, -1);
}

void digi101_draw(void *bmp, void *ctrl)
{
    const uint8_t *kit = UI_KIT;
    uint32_t t = ACTIVE_TRACK;
    int k;
    if (ctrl)
        last_ctrl = (char *)ctrl;
    if (!page_open || covered())
        return;
    if (!active_is_101() || !kit || t > 7) {    /* another track's: back to the stock page */
        page_open = 0;
        return;
    }
    FILLRECT(bmp, 21, 0, 127, 52, 0);           /* the knobs' area only: the top bar and the SMP/LEV */
    for (k = 0; k < 8; k++) {                   /* column stay the firmware's, messages and all */
        char buf[8];
        int v;
        const char *txt = knob_value(kit, (int)t, k, buf, &v);
        cell(bmp, k, k ? f_name[k - 1] : "PORT", txt, v);
    }
}

static void page_tick(void *ctrl)
{
    if (ctrl)
        last_ctrl = (char *)ctrl;
    find_main(ctrl);

    kstore_tick(&store, UI_KIT, KS_SLOT_D101, d101_mask);  /* PORT survives sound loads, power cycles */
    if (slot_dirty && UI_KIT) {                             /* as the firmware after an edit */
        int t;
        for (t = 0; t < 8; t++)
            if ((slot_dirty >> t) & 1)
                ks_persist(UI_KIT, t);
        slot_dirty = 0;
    }
    if (page_open)
        *((volatile char *)ctrl + 0x20) = 1;               /* keep the frame coming */
}
