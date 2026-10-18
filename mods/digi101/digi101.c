/* DH-101 (mod id digi101): an SH-101-style oscillator section as one of Digi Mono's machines (digimono_ext.h), as
 * Digi Macro is: Digi Mono renders its voices on its own stack, with its overload guard; machine-pages
 * draws its two SRC pages and its LFO destinations from the page its descriptor names; this mod brings
 * the engine (sh101.c), its state and its page.
 *
 * 0.25: the second SRC page is machine-pages 1.2's (cm_ui_v32): seven more parameters the firmware itself
 * keeps (the sound's free slots 46..52), so each takes p-locks, both LFOs reach it and it is saved with
 * the sound; its eighth knob shows the track's LFO 1 depth. SRC flips between the two pages. DH-101 reads
 * their final values (locks, smoothing and LFOs applied) with cm_src_value in the render. It patches no
 * firmware place of its own.
 *
 * Two registrations: the machine list entry (core_machines: id 29, DH-101, its icon; SLICE's params, as
 * machine-pages' second page needs; core 3.0's tail: its page) and the engine (Digi Mono's collection
 * digimono_ext).
 *
 * Parts follow Digi Mono and Digi Macro (gdeo607/digi1_mods, MIT): the machine's registration and icon
 * (digimacro.c). No firmware address is used: what it reads, it reads through core's and machine-pages'
 * exports (digitakt-mk1/core3.h). */
#if defined(HOST_TEST)
#include "../../tests/digi101_host_fw.h"              /* the desktop test's stand-ins */
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
static void track_page2(struct sh101_voice *v, int t);

#define FADE_N 64                               /* the fade-in from silence: 64 samples, a raised cosine */
static const int16_t fade_gain[FADE_N] = {
    20, 79, 177, 315, 491, 705, 958, 1247, 1573, 1935, 2331, 2761, 3224, 3719, 4244, 4799,
    5381, 5990, 6624, 7281, 7961, 8660, 9379, 10114, 10864, 11628, 12403, 13187, 13980, 14778, 15580, 16383,
    17187, 17989, 18787, 19580, 20364, 21139, 21903, 22653, 23388, 24107, 24806, 25486, 26143, 26777, 27386, 27968,
    28523, 29048, 29543, 30006, 30436, 30832, 31194, 31520, 31809, 32062, 32276, 32452, 32590, 32688, 32747, 32767};
#define AMP_PHASE(v) (fw_amp_env[3 * (v)])                    /* the AMP envelope's phase, 0: idle */
#define AMP_LEVEL(v) (fw_amp_env[3 * (v) + 1])                /* its level, Q31 */

static void d101_render(void *st, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct sh101_voice *v = (struct sh101_voice *)st;
    int t = voice_of(st), i;
    track_page2(v, t);                                  /* PORT and the filter: SRC page 2, as played */
    if (v->fresh)
        v->fresh--;
    else if (t >= 0 && v->envq <= ENVQ_QUIET && !AMP_PHASE(t)) {  /* the AMP idle and quiet */
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

/* ---- the machine list's entry (core_machines) ------------------------------------------------ *
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
    const void *vt;                             /* the firmware's Bitmap vtable (core's fw_bitmap_vt) */
    uint32_t w, h, one;
    const uint32_t *px, *mask;
    uint32_t end;
};
static const struct bitmap icon = {fw_bitmap_vt, 11, 7, 1, icon_px, icon_mask, 0};

/* ---- its two SRC pages and its LFO destinations (machine-pages' cm_ui5) ------------------------------ *
 * Page 1, SLICE's page: A TUNE, B RNGE, C SAW, D NOIS, E PULS, F PW, G PWM, H SUB, each a 0..127 knob drawn
 * as BR's, its value text from Digi Mono's (which asks knob_text above). Page 2 (SRC again): A PORT, B FREQ,
 * C RESO, D ENV, F KYB, G VIB, H RATE: machine-pages' parameters 8..14, kept by the firmware in the sound
 * (slots 46..52), so they take locks, the LFOs and the save as any other; E is the track's own LFO 1 depth
 * (stock parameter 59), where 0.24 had MOD: LFO 1 aimed at 101:Filter Freq does what MOD did, lockable.
 * The LFO page's list names all fourteen in the group "101". */
#define P_BR 0x86
#define KNOB(sn, ln, d, i, f) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | (f), \
    .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = digimono_fmt_##i }

/* page 2's value texts, from the value as stored (8.8) */
static char *put_hz(char *b, int v)             /* RATE: the LFO's frequency, from the engine's own table */
{
    unsigned h = (sh101_lfo_rate(v) + 447u) / 895u;   /* phase step a sample -> hundredths of a Hz (48 kHz) */
    if (h < 100) {                              /* .10 .. .99 */
        *b++ = '.';
        *b++ = (char)('0' + h / 10);
        *b++ = (char)('0' + h % 10);
    } else if (h < 1000) {                      /* 1.0 .. 9.9 */
        *b++ = (char)('0' + h / 100);
        *b++ = '.';
        *b++ = (char)('0' + h / 10 % 10);
    } else
        b = put_u(b, (int)((h + 50) / 100));    /* 10 .. 30 */
    return put_s(b, "Hz");
}
static int32_t fmt_port(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    v = (v >> 8) & 127;
    if (v)
        put_u(buf, v);
    else
        put_s(buf, "OFF");
    return 1;
}
static int32_t fmt_num(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    put_u(buf, (v >> 8) & 127);
    return 1;
}
static int32_t fmt_kyb(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    v = (v >> 8) & 127;
    if (v)
        put_s(put_u(buf, (v * 100 + 63) / 127), "%");
    else
        put_s(buf, "OFF");
    return 1;
}
static int32_t fmt_rate(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    (void)ctx; (void)m;
    put_hz(buf, (v >> 8) & 127);
    return 1;
}
#define RATE_DEF 85                             /* 4.5 Hz: 0.23's default (its stored 0) */
#define KNOB2(sn, ln, d, f) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = f }

#define ID_LFO1_DEP 59                          /* the track's LFO 1 depth (a stock sound parameter) */
static const struct cm_src_page pages[2] = {
    {"DH-101 1/2", {0, 1, 2, 3, 4, 5, 6, 7}, 0, {0, 0, 0}},
    {"DH-101 2/2", {8, 9, 10, 11, ID_LFO1_DEP, 12, 13, 14}, CM_SRC_STOCK(4), {0, 0, 0}},
};

const struct cm_ui32 digi101_page = {
    .base = {
        .abi = cm_ui_v32, .page_from = 3, .group = "101",
        .knob = {
            [0] = { .name = "TUNE", .lname = "Tune" },
            [1] = KNOB("RNGE", "Range", 32, 1, 0),
            [2] = KNOB("SAW", "Saw Level", 100, 2, 0),
            [3] = KNOB("NOIS", "Noise Level", 0, 3, CM_NOT_SAMPLE),
            [4] = KNOB("PULS", "Pulse Level", 0, 4, 0),
            [5] = KNOB("PW", "Pulse Width", 0, 5, 0),
            [6] = KNOB("PWM", "PWM Source", 0, 6, 0),
            [7] = KNOB("SUB", "Sub Osc", 0, 7, 0),
        },
    },
    .extra = {
        KNOB2("PORT", "Portamento", 0, fmt_port),
        KNOB2("FREQ", "Filter Freq", 127, fmt_num),
        KNOB2("RESO", "Filter Reso", 0, fmt_num),
        KNOB2("ENV", "Env Depth", 0, fmt_num),
        KNOB2("KYB", "Keyboard Follow", 0, fmt_kyb),
        KNOB2("VIB", "Vibrato", 0, fmt_num),
        KNOB2("RATE", "LFO Rate", RATE_DEF, fmt_rate),
    },
    .page_count = 2, .pages = pages,
};
#undef KNOB
#undef KNOB2

/* id, name, short name, icon, params (SLICE: machine-pages' second page reads SLICE's ids), render
 * (ONESHOT, as Digi Mono's machines); core 3.0's tail: its page */
const struct cm_machine digi101_m29 = {29, "DH-101", "101", &icon, 3, 0, CM_UI_TAG, &digi101_page.base};

/* ---- SRC page 2, as played: PORT and the filter, locks, smoothing and both LFOs applied ------------- */
static void track_page2(struct sh101_voice *v, int t)
{
    int32_t a;
    uint8_t *f[CM_EXTRAS];
    int i;
    if ((unsigned)t > 7)
        return;
    f[0] = &v->port; f[1] = &v->f_cut; f[2] = &v->f_res; f[3] = &v->f_env;
    f[4] = &v->f_kyb; f[5] = &v->f_vib; f[6] = &v->f_rate;
    v->f_mod = 0;                               /* MOD (0.24) is the track's LFO 1 on 101:Filter Freq now */
    for (i = 0; i < CM_EXTRAS; i++) {
        int32_t w = cm_src_value(t, 8 + i);     /* 8.8, 0 .. 0x7f00 */
        int x = (int)((w + 128) >> 8);
        *f[i] = (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x);
    }
    a = AMP_LEVEL(t);                           /* the track's AMP envelope, for ENV */
    if (a < 0)
        a = a == (int32_t)0x80000000 ? 0x7fffffff : -a;
    v->envq = (uint16_t)(a >> 16);
}

/* ---- which voice a state belongs to -------------------------------------------------------------- *
 * Digi Mono hands a machine its voice's state, not the voice's number. On the unit voice v plays
 * track v, so the voice is learnt at its trigs: the voices that (re)start this block are bits of
 * fw_voice_start (Digi Mono reads the same); of those, a DH-101 track's voice not yet known as another state's
 * is this one. A trig that is the only DH-101 start of its block is certain and corrects any guess. */
#define VOICE_START  fw_voice_start
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
