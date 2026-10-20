/* Digi Mono: synth machines for the audio tracks (DESIGN.md). The render side.
 *
 * digimono_blocks() runs once a render block, after the voice loop has left each voice's 32 Q31 samples at
 * 0x80001a18 + 128 v and before the filter, amp envelope and mixer stages work on them. For every voice
 * playing one of our machines (core_track_machine 20..28; they render as ONESHOT) it writes the engine's
 * block there, so the voice's filter, amp envelope, VOL, LFOs, sends and level follow as for a sample. Firmware addresses are OS 1.53's; how each was found: docs/TECHNICAL_NOTES.md.
 */

/* ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ---- */
#ifdef OS154
#define F_40151cc2 0x40151eea
#define F_4197b6b4 0x4197c6b4
#define F_4197c7fc 0x4197d7fc
#define F_4197ce98 0x4197de98
#define F_4197cf5c 0x4197df5c
#define F_4199dc44 0x4199ec44
#define F_4199df54 0x4199ef54
#define F_4199df58 0x4199ef58
#else
#define F_40151cc2 0x40151cc2
#define F_4197b6b4 0x4197b6b4
#define F_4197c7fc 0x4197c7fc
#define F_4197ce98 0x4197ce98
#define F_4197cf5c 0x4197cf5c
#define F_4199dc44 0x4199dc44
#define F_4199df54 0x4199df54
#define F_4199df58 0x4199df58
#endif
/* ---- end of the moved addresses ---- */

#include "digitakt-mk1/core3.h"
#include "mono.h"
#include "digimono_ext.h"

/* the UI and the per-block glue: built for size (the samples are made in mono.c, and in other mods for
 * their machines: mods/digimono/digimono_ext.h) */
#pragma GCC optimize("Os", "no-tree-loop-distribute-patterns")

#define MACH_FIRST   20                      /* digimono_m20..m28 in glue.s: SIN NOIS SAW PULS ENS VO PSIN, 28 CHORDS */
#define MACH_SLOTS   12                      /* 20..31: Digi Mono's and other mods' (27 MACRO, 29 DH-101, 30 TX KIT) */
#define VOICE_MACH   core_track_machine
#define VOICE_START  (*(volatile const uint32_t *)0x80001228) /* bit v: voice v (re)started this block */
#define VOICE_NOTE   ((volatile const int32_t *)0x80001f28)   /* per voice: MIDI note << 16 */
#define VOICE_WORDS  0x80002772                /* smoothed parameter words, 106 bytes a voice */
#define BLOCK(v)     ((int32_t *)(0x80001a18 + 128 * (v)))   /* voice v's 32 samples, Q31 */
/* The amp envelope's phase (0: idle) and level, per track, as the AMP stage leaves them (digisophie reads
 * the same). Not the voice gain at 0x8000edc4 + 94 v + 16: that is LEV^2 x the track level, and LEV is
 * knob H, an engine parameter here. */
#define AMP_PHASE(v) (*(volatile const int32_t *)(F_4199df54 + 12 * (v)))
#define AMP_LEVEL(v) (*(volatile const int32_t *)(F_4199df58 + 12 * (v)))
#define QUIET_BLOCKS 32                      /* 21 ms of a silent envelope: the voice sleeps */

#define SLOT_TUNE    17                        /* SRC knob A */
#define KNOBS        7
static const uint8_t knob_slot[KNOBS] = {18, 19, 21, 22, 23, 24, 20};  /* SRC knobs B C E F G H, then D */

/* Which engine parameter each knob (B C E F G H D) sets, or -1: the knob is not on the machine's page. */
static const int8_t knob_p[MONO_MACHINES][KNOBS] = {
    {-1, -1, -1, -1, -1, -1, -1},       /* SIN                                            */
    { 0,  1,  2, -1, -1, -1, -1},       /* NOIS  ST RED STON                              */
    { 0,  1,  2,  4,  5,  6, -1},       /* SAW   UNIL UNIW UNIX SUBX SUB1 SUB2            */
    { 0,  1,  2,  4,  5,  6,  3},       /* PULS  UNIL UNIW SUB1 PW PWAD PWRS, D SUB2      */
    { 0,  1,  2,  3,  5,  6,  4},       /* ENS   PCH2 PCH3 PCH4 WAVE CHRL CHRW, D PW      */
    { 0,  1,  2,  4,  5,  6,  3},       /* VO    VOC1 VOC2 V-SW CONS CLEN CVOL, D VOIC    */
    { 0,  1,  3,  4, -1, -1,  2},       /* PSIN  NOT1 NOT2 EDEP ESPD, D NOT3              */
    {-1, -1, -1, -1, -1, -1, -1},       /* (27: another mod's: digimono_ext)              */
    { 0,  1,  2,  3,  5,  6,  4},       /* CHORD TYPE INV SPRD WAVE CHRL CHRW, D PW       */
};

struct digimono_voice {
    struct mono_voice mv;
    uint8_t live;
    uint8_t quiet;                          /* blocks the amp envelope has been silent; QUIET_BLOCKS: asleep */
    uint8_t mach;                           /* the machine it last played (an id; 0: none yet)        */
    uint8_t strikes;                        /* times the guard has rested it since the render was calm */
    uint16_t rest;                          /* blocks left in which it rests (the overload guard)      */
    uint16_t cost;                          /* its last block's time, DTIM ticks (the guard)           */
    uint16_t want;                          /* resting: the time its block took before                 */
};

/* A machine's knobs: Digi Mono's own (tables below) or another mod's (its struct digimono_ext). */
struct kinfo {
    const int8_t *kp;                       /* knob -> parameter, -1: not on the page */
    const uint8_t *def;                     /* the knobs' values on a switch to it */
    const struct digimono_ext *x;           /* another mod's machine, or 0 */
    int m;                                  /* id - MACH_FIRST */
};
static int kinfo_of(int id, struct kinfo *k);
static struct digimono_voice voices[8];
static int shed;                            /* blocks left in which every voice rests (the overload guard) */

static int16_t word(int v, int slot)
{
    return *(volatile const int16_t *)(VOICE_WORDS + 106 * v + 2 * slot);
}

/* The engine's seven parameters from the SRC knobs B..H (DESIGN.md, "The SRC page"). */
static void params(int v, const struct kinfo *k, uint8_t *p)
{
    int i, model = k->m;
    for (i = 0; i < MONO_PARAMS; i++)
        p[i] = 0;
    for (i = 0; i < KNOBS; i++) {
        int w, j = k->kp[i];
        if (j < 0)
            continue;
        w = word(v, knob_slot[i]) >> 8;
        p[j] = w < 0 ? 0 : w > 127 ? 127 : (uint8_t)w;
    }
    if (!k->x && (model == MONO_ENS || model == MONO_CHORD))   /* PW on D: 0 = square (the engine's 64) .. 127 = thinnest */
        p[4] = (uint8_t)(64 + (p[4] >> 1));
}

static void digimono_block(int v)
{
    struct digimono_voice *d;
    struct kinfo k;
    int id, i;
    int32_t pitch;
    uint8_t p[MONO_PARAMS];
    int16_t out[32];
    if ((unsigned)v > 7)
        return;
    id = VOICE_MACH[v];
    if (!kinfo_of(id, &k))
        return;
    d = &voices[v];
    if (!d->live) {
        mono_init(&d->mv);
        d->mv.rng ^= (uint32_t)v * 0x9e3779b9u;
        d->live = 1;
        d->quiet = QUIET_BLOCKS;
    }
    if (d->mach != id) {                    /* turned to this machine: another mod's starts afresh (its
                                             * state in the chorus line's memory), and the line is
                                             * cleared for Digi Mono's after one of those */
        struct kinfo pk;
        int was_ext = d->mach && kinfo_of(d->mach, &pk) && pk.x;
        if (k.x)
            k.x->init(d->mv.dl);
        else if (was_ext)
            d->mv.own = 0;
        d->mach = (uint8_t)id;
    }
    if ((VOICE_START >> v) & 1) {
        if (k.x)
            k.x->trig(d->mv.dl);
        else
            mono_trig(&d->mv, k.m);
        d->quiet = 0;
    } else {
        int32_t l = AMP_LEVEL(v);
        if (AMP_PHASE(v) != 0 || (l < 0 ? -l : l) > (1 << 19))
            d->quiet = 0;
        else if (d->quiet < QUIET_BLOCKS)
            d->quiet++;
        if (d->quiet >= QUIET_BLOCKS)
            return;                         /* asleep: the stock block is silence already */
    }
    if (shed || d->rest)
        return;                             /* the CPU is overloaded: rest (see the overload guard) */
    /* the pitch as the render computes a sample's: the note plus TUNE (after the LFOs), in Q16 semitones */
    pitch = VOICE_NOTE[v] + ((int32_t)(word(v, SLOT_TUNE) - 16384) << 8);
    params(v, &k, p);
    if (k.x)
        k.x->render(d->mv.dl, p, mono_pitch_inc(pitch >> 9), out, 32);
    else
        mono_render(&d->mv, k.m, p, mono_pitch_inc(pitch >> 9), out, 32);
    for (i = 0; i < 32; i++)
        BLOCK(v)[i] = (int32_t)out[i] << 16;
}

/* ---- the overload guard ---------------------------------------------------------------------------- *
 * The audio render runs at a higher priority than the UI. If a render takes longer than the time between
 * two renders, the next one starts at once, the UI never runs again (no knob, no key release reaches the
 * firmware) and the outputs repeat their last buffers: a freeze only a power cycle ends. So each render is
 * timed (core's ev_render_in / ev_render_out, DTIM0: core starts it free-running at the 132 MHz bus clock;
 * a 32-frame render period at 48 kHz is 88,000 ticks), and so is each Digi Mono voice's block. In steps:
 *   1. eco: a render over 7/8 of the period sets digimono_eco: the machines take their cheaper paths (ENS
 *      and CHORDS fade their chorus out; MACRO's 2OP FM starts its notes at 2x instead of 4x, PARTCL
 *      fires four of its six particles). Two seconds of renders under 5/8 of the period end it.
 *   2. one voice: three renders in a row over 94 % (a single long one is a spike, not an overload) rest the
 *      voice whose block took longest (its block stays the stock silence, its state waits): 32 ms, twice as
 *      long each time again (up to 1 s) until the render is calm, and after that until the render has
 *      room for its block again (no blips of a voice that does not fit). The other voices play on.
 *   3. every voice: twelve renders in a row over 94 % (the load is not Digi Mono's, or resting voices
 *      did not help) rest them all for 16 ms, which gives the CPU back: a dropout instead of a freeze. */
#define DTCN0        (*(volatile const uint32_t *)0xFC07000C)
#define RENDER_TICKS 88000u
#define ECO_LIMIT    (RENDER_TICKS - RENDER_TICKS / 8)
#define CALM_LIMIT   (RENDER_TICKS * 5 / 8)
#define CALM_BLOCKS  3000                       /* 2 s */
#define SHED_LIMIT   (RENDER_TICKS - RENDER_TICKS / 16)
#define REST_BLOCKS  48                         /* 32 ms, doubled with each strike */
#define REST_STRIKES 5                          /* up to 1 s */
#define SHED_BLOCKS  24                         /* 16 ms */
static uint32_t rin_t;
static int over;                                /* renders over the limit in a row */
static int calm;                                /* renders under CALM_LIMIT in a row */
uint32_t digimono_overloads;                    /* voices rested since power-on (diagnostics) */

void digimono_rin(void)
{
    rin_t = DTCN0;
}

/* rest the voice whose last block took longest; 0 if none is playing */
static int rest_heaviest(void)
{
    struct digimono_voice *h = 0;
    int v;
    for (v = 0; v < 8; v++) {
        struct digimono_voice *d = &voices[v];
        if (!d->rest && d->cost > 400 && (!h || d->cost > h->cost))   /* (400: a voice asleep) */
            h = d;
    }
    if (!h)
        return 0;
    h->rest = (uint16_t)(REST_BLOCKS << h->strikes);
    h->want = h->cost;
    if (h->strikes < REST_STRIKES)
        h->strikes++;
    digimono_overloads++;
    return 1;
}

void digimono_rout(void)
{
    uint32_t dur = DTCN0 - rin_t;
    int v;
    if (dur >= 0x80000000u)
        dur = 0;
    if (dur > ECO_LIMIT) {
        digimono_eco = 1;
        calm = 0;
    } else if (dur < CALM_LIMIT) {
        if (++calm >= CALM_BLOCKS) {
            calm = 0;
            digimono_eco = 0;
            for (v = 0; v < 8; v++)
                voices[v].strikes = 0;
        }
    } else {
        calm = 0;
    }
    if (dur > SHED_LIMIT) {
        ++over;
        if (over >= 12) {
            shed = SHED_BLOCKS;
            over = 0;
            digimono_overloads++;
        } else if (over % 3 == 0) {
            rest_heaviest();
        }
    } else {
        int back = 0;                           /* one voice back a render, if there is room for it */
        over = 0;
        if (shed)
            shed--;
        for (v = 0; v < 8; v++) {
            struct digimono_voice *d = &voices[v];
            if (d->rest > 1)
                d->rest--;
            else if (d->rest && !back && dur + d->want < SHED_LIMIT)
                d->rest = 0, back = 1;
        }
    }
}

/* The render's own stack (glue.s digimono_rblock): the engines' deepest calls take about 2 KB (MACRO's
 * drums 1.9 KB), the rest is room for the interrupts above the render's level. */
#define DIGIMONO_STACK 4096
uint32_t digimono_stack[DIGIMONO_STACK / 4] __attribute__((aligned(16)));
__asm__(".globl digimono_stack_top\n\t.set digimono_stack_top, digimono_stack + " "4096");

void digimono_blocks(void)
{
    int v;
    for (v = 0; v < 8; v++) {
        uint32_t t = DTCN0;
        digimono_block(v);
        t = DTCN0 - t;
        voices[v].cost = (uint16_t)(t > 0xffffu ? 0xffffu : t);
    }
}

/* ---- the SRC and LFO pages (machine-pages, core 3.0) ---------------------------------------------
 * Each machine describes its page (struct cm_ui, elekloader's digitakt-mk1/core3.h), and the machine-pages
 * mod draws it: SLICE's page (page_from 3: parameter ids 0x84..0x8b, knobs A..H) with the knobs the machine
 * does not have blank, each knob it has with its name and long name, drawn as BR's (a plain round knob, a
 * value 0..127: look P_BR) with its own value text (fmt), a 0..127 range and the machine's default; knob D,
 * where a machine has it, an engine knob (no sample list). A stays TUNE. The LFO page names the same knobs
 * (ONESHOT's parameters, the machines' params: ids 0x6c..0x73), in the machine's group ("MSAW"). Another
 * mod's machine (digimono_ext) describes its own page, and can take its texts from digimono_fmt below. */

#define UI_KIT       (*(uint8_t *volatile const *)F_4199dc44)   /* its sounds: + 0x20 + 0xa2 t */
#define P_BR         0x86                      /* BR's parameter id: the look every knob borrows */

/* how each knob's value reads */
enum { F_NUM, F_VOICES, F_SEMI, F_PW, F_PWENS, F_VOWEL, F_CONS, F_MS, F_SHAPE, F_BIPST, F_BIPOFF,
       F_CHORD, F_INV, F_SPRD, F_CHWAVE };
static const uint8_t knob_fmt[MONO_MACHINES][KNOBS] = {
    {F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM},
    {F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM},
    {F_NUM, F_NUM, F_VOICES, F_SHAPE, F_NUM, F_NUM, F_NUM},
    {F_NUM, F_NUM, F_NUM, F_PW, F_NUM, F_NUM, F_NUM},
    {F_SEMI, F_SEMI, F_SEMI, F_SHAPE, F_NUM, F_NUM, F_PWENS},
    {F_VOWEL, F_VOWEL, F_NUM, F_CONS, F_MS, F_NUM, F_NUM},
    {F_SEMI, F_SEMI, F_BIPST, F_BIPOFF, F_NUM, F_NUM, F_SEMI},
    {F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM, F_NUM},
    {F_CHORD, F_INV, F_SPRD, F_CHWAVE, F_NUM, F_NUM, F_PWENS},
};

/* CHORDS: inversion and spread (mono.c mono_chord_notes: 32 values a step) */
static const char *const inv_name[4] = {"ROOT", "1ST", "2ND", "3RD"};
static const char *const sprd_name[4] = {"CLOSE", "OPEN", "WIDE", "XWIDE"};

/* VO's vowels (VOC1, VOC2: the continuum of mono.c's VOWEL, nearest) and consonants (CONS: 8 zones) */
static const char *const vowel_name[10] = {"OO", "U", "AW", "AH", "UH", "AE", "EH", "IH", "EE", "ER"};
static const char *const cons_name[8] = {"-", "S", "SH", "F", "H", "T", "K", "P"};

static char *put_s(char *o, const char *t)
{
    while ((*o = *t++))
        o++;
    return o;
}

static char *put_u(char *o, uint32_t n)
{
    char d[10];
    int i = 0;
    do {
        d[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i)
        *o++ = d[--i];
    *o = 0;
    return o;
}

/* A knob's value v (0..127) as text: units where it has them. */
static void text_into(const struct kinfo *ki, int k, int v, char *buf)
{
    char *o = buf;
    if (ki->x) {                            /* another mod's machine: its text, or the number */
        if (!ki->x->text(k, v, buf))
            put_u(o, (uint32_t)v);
        return;
    }
    switch (knob_fmt[ki->m][k]) {
    case F_VOICES:                          /* mono.c render_saw: 1 to 3 unison saws */
        o = put_u(o, v < 43 ? 1 : v < 86 ? 2 : 3);
        put_s(o, v < 43 ? " SAW" : " SAWS");
        break;
    case F_SEMI: {                          /* render_ens: 63 = the main oscillator's pitch */
        int s = v - 63;
        s = s < -36 ? -36 : s > 36 ? 36 : s;
        if (s > 0)
            *o++ = '+';
        if (s < 0) {
            *o++ = '-';
            s = -s;
        }
        o = put_u(o, (uint32_t)s);
        put_s(o, "st");
        break;
    }
    case F_PW:                              /* duty(): 50 % at 64, 0.763 % a step */
        o = put_u(o, (uint32_t)((50 * 131 + (v - 64) * 100 + 65) / 131));
        put_s(o, "%");
        break;
    case F_PWENS:                           /* D 0..127 -> the engine's 64..127 */
        v = 64 + (v >> 1);
        o = put_u(o, (uint32_t)((50 * 131 + (v - 64) * 100 + 65) / 131));
        put_s(o, "%");
        break;
    case F_VOWEL:
        put_s(o, vowel_name[(v * 9 * 2 + 127) / 254]);
        break;
    case F_CONS:
        put_s(o, cons_name[v >> 4]);
        break;
    case F_CHORD:
        put_s(o, mono_chord_name[mono_chord_type(v)]);
        break;
    case F_INV:
        put_s(o, inv_name[v >> 5]);
        break;
    case F_SPRD:
        put_s(o, sprd_name[v >> 5]);
        break;
    case F_CHWAVE:                          /* render_chord: 0..15 sines, then saw..pulse in % */
        if (v < 16) {
            put_s(o, "SINE");
            break;
        }
        o = put_u(o, (uint32_t)(((v - 16) * 100 + 55) / 111));
        put_s(o, "%");
        break;
    case F_MS:                              /* render_vo: CLEN 96 + v^2 x 1188 / 1024 samples */
        o = put_u(o, (uint32_t)((96 + ((v * v * 1188) >> 10)) / 48));
        put_s(o, "ms");
        break;
    case F_BIPST:                           /* render_psin: EDEP, 64 = none, in semitones */
    case F_BIPOFF: {                        /* render_psin: ESPD, 64 = off, - rises, + decays */
        int s = v - 64;
        if (s == 0 && knob_fmt[ki->m][k] == F_BIPOFF) {
            put_s(o, "OFF");
            break;
        }
        if (s > 0)
            *o++ = '+';
        if (s < 0) {
            *o++ = '-';
            s = -s;
        }
        o = put_u(o, (uint32_t)s);
        if (knob_fmt[ki->m][k] == F_BIPST)
            put_s(o, "st");
        break;
    }
    case F_SHAPE:                           /* SUBX square..saw, WAVE saw..pulse: a mix, in % */
        o = put_u(o, (uint32_t)((v * 100 + 63) / 127));
        put_s(o, "%");
        break;
    default:
        put_u(o, (uint32_t)v);
    }
}

/* A knob's value text for the machine-pages page: page knob i (A..H: 0..7) of machine m. */
static int32_t page_text(int i, char *buf, int32_t value, int32_t m)
{
    static const int8_t k_of[8] = {-1, 0, 1, 6, 2, 3, 4, 5};   /* A..H -> B C E F G H D's index */
    struct kinfo ki;
    int k = k_of[i];
    if (k < 0 || !kinfo_of((int)m, &ki) || ki.kp[k] < 0)
        return 0;
    text_into(&ki, k, (value >> 8) & 0x7f, buf);
    return 1;
}
#define FMT(i) int32_t digimono_fmt_##i(char *buf, int32_t value, int32_t ctx, int32_t m) \
    { (void)ctx; return page_text(i, buf, value, m); }
FMT(1) FMT(2) FMT(3) FMT(4) FMT(5) FMT(6) FMT(7)
#undef FMT

#define DM_KNOB(sn, ln, d, i) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT, \
    .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = digimono_fmt_##i }
#define DM_KNOB_D(sn, ln, d, i) { .name = sn, .lname = ln, .look = P_BR, \
    .flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE, .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = digimono_fmt_##i }
#define DM_HIDDEN { .flags = CM_HIDDEN }

/* The machines' pages, which the descriptors' tails name (glue.s digimono_m20..m28): knobs A..H; the names,
 * long names and defaults are the machines' B C E F G H D (knob_p, mono_def) */
const struct cm_ui digimono_ui_sin = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MSIN",
    .knob = {
        [1] = DM_HIDDEN,
        [2] = DM_HIDDEN,
        [3] = DM_HIDDEN,
        [4] = DM_HIDDEN,
        [5] = DM_HIDDEN,
        [6] = DM_HIDDEN,
        [7] = DM_HIDDEN,
    },
};
const struct cm_ui digimono_ui_nois = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MNOI",
    .knob = {
        [1] = DM_KNOB("ST", "Sample Hold Rate", 0, 1),
        [2] = DM_KNOB("RED", "Red Noise", 0, 2),
        [3] = DM_HIDDEN,
        [4] = DM_KNOB("STON", "Tuned Noise", 0, 4),
        [5] = DM_HIDDEN,
        [6] = DM_HIDDEN,
        [7] = DM_HIDDEN,
    },
};
const struct cm_ui digimono_ui_saw = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MSAW",
    .knob = {
        [1] = DM_KNOB("UNIL", "Unison Level", 0, 1),
        [2] = DM_KNOB("UNIW", "Unison Detune", 40, 2),
        [3] = DM_HIDDEN,
        [4] = DM_KNOB("UNIX", "Unison Voices", 0, 4),
        [5] = DM_KNOB("SUBX", "Sub Shape", 0, 5),
        [6] = DM_KNOB("SUB1", "Sub 1 Oct Lev", 0, 6),
        [7] = DM_KNOB("SUB2", "Sub 2 Oct Lev", 0, 7),
    },
};
const struct cm_ui digimono_ui_puls = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MPLS",
    .knob = {
        [1] = DM_KNOB("UNIL", "Unison Level", 0, 1),
        [2] = DM_KNOB("UNIW", "Unison Detune", 40, 2),
        [3] = DM_KNOB_D("SUB2", "Sub 2 Oct Lev", 0, 3),
        [4] = DM_KNOB("SUB1", "Sub 1 Oct Lev", 0, 4),
        [5] = DM_KNOB("PW", "Pulse Width", 64, 5),
        [6] = DM_KNOB("PWAD", "PWM Depth", 0, 6),
        [7] = DM_KNOB("PWRS", "PWM Rate", 40, 7),
    },
};
const struct cm_ui digimono_ui_ens = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MENS",
    .knob = {
        [1] = DM_KNOB("PCH2", "Pitch Osc 2", 63, 1),
        [2] = DM_KNOB("PCH3", "Pitch Osc 3", 63, 2),
        [3] = DM_KNOB_D("PW", "Pulse Width", 0, 3),
        [4] = DM_KNOB("PCH4", "Pitch Osc 4", 63, 4),
        [5] = DM_KNOB("WAVE", "Saw-Pulse", 0, 5),
        [6] = DM_KNOB("CHRL", "Chorus Level", 0, 6),
        [7] = DM_KNOB("CHRW", "Chorus Width", 127, 7),
    },
};
const struct cm_ui digimono_ui_vo = {
    .abi = cm_ui_v3, .page_from = 3, .group = "MVO",
    .knob = {
        [1] = DM_KNOB("VOC1", "Vowel 1", 43, 1),
        [2] = DM_KNOB("VOC2", "Vowel 2", 113, 2),
        [3] = DM_KNOB_D("VOIC", "Breath", 0, 3),
        [4] = DM_KNOB("V-SW", "Vowel Glide", 64, 4),
        [5] = DM_KNOB("CONS", "Consonant", 0, 5),
        [6] = DM_KNOB("CLEN", "Cons. Length", 40, 6),
        [7] = DM_KNOB("CVOL", "Cons. Level", 100, 7),
    },
};
const struct cm_ui digimono_ui_psin = {
    .abi = cm_ui_v3, .page_from = 3, .group = "PSIN",
    .knob = {
        [1] = DM_KNOB("NOT1", "Note 1", 63, 1),
        [2] = DM_KNOB("NOT2", "Note 2", 63, 2),
        [3] = DM_KNOB_D("NOT3", "Note 3", 63, 3),
        [4] = DM_KNOB("EDEP", "Env Depth", 64, 4),
        [5] = DM_KNOB("ESPD", "Env Speed", 64, 5),
        [6] = DM_HIDDEN,
        [7] = DM_HIDDEN,
    },
};
const struct cm_ui digimono_ui_chrd = {
    .abi = cm_ui_v3, .page_from = 3, .group = "CHRD",
    .knob = {
        [1] = DM_KNOB("TYPE", "Chord Type", 0, 1),
        [2] = DM_KNOB("INV", "Inversion", 0, 2),
        [3] = DM_KNOB_D("PW", "Pulse Width", 0, 3),
        [4] = DM_KNOB("SPRD", "Spread", 0, 4),
        [5] = DM_KNOB("WAVE", "Sine-Saw-Pulse", 16, 5),
        [6] = DM_KNOB("CHRL", "Chorus Level", 0, 6),
        [7] = DM_KNOB("CHRW", "Chorus Width", 127, 7),
    },
};


/* ---- defaults on a switch ------------------------------------------------------------------------ */
/* Switching a track's machine gives it the parameters' machine's defaults (ONESHOT's: core 2.1), which
 * mean nothing here (ENS would start 36 semitones down). digimono_tick (ev_tick, 30 Hz) watches the UI
 * kit: when a track of the same kit turns into a Digi Mono machine while its knobs still hold switch
 * defaults (ONESHOT's for E F G H, which a stock switch resets, or the Digi Mono machine it was before: the machine
 * list switches as its cursor moves), it sets the new machine's own, through the firmware's knob path
 * (0x400771e8, the engine's copy) and in the kit (what is saved and shown). A kit or sound load that
 * brings its own values matches neither, or comes with a new kit, and is left alone. */
typedef void (*setparam_t)(int32_t value, int32_t voice, int32_t slot);
#define SETPARAM     ((setparam_t)0x400771e8)

static const uint16_t oneshot_def[6] = {0x0300, 0x0000, 0x0000, 0x7800, 0x0000, 0x6400};  /* B C E F G H */
static const uint8_t mono_def[MONO_MACHINES][KNOBS] = {   /* B C E F G H D */
    {0, 0, 0, 0, 0, 0, 0},              /* SIN                                         */
    {0, 0, 0, 0, 0, 0, 0},              /* NOIS  ST RED STON                           */
    {0, 40, 0, 0, 0, 0, 0},             /* SAW   UNIL UNIW UNIX SUBX SUB1 SUB2         */
    {0, 40, 0, 64, 0, 40, 0},           /* PULS  UNIL UNIW SUB1 PW PWAD PWRS, SUB2     */
    {63, 63, 63, 0, 0, 127, 0},         /* ENS   PCH2 PCH3 PCH4 WAVE CHRL CHRW, PW (square) */
    {43, 113, 64, 0, 40, 100, 0},       /* VO    VOC1 (AH) VOC2 (EE) V-SW CONS CLEN CVOL, VOIC */
    {63, 63, 64, 64, 0, 0, 63},         /* PSIN  NOT1 NOT2, EDEP ESPD off, D NOT3: MONO SIN */
    {0, 0, 0, 0, 0, 0, 0},              /* (27: another mod's: digimono_ext)           */
    {0, 0, 0, 16, 0, 127, 0},           /* CHORD MAJ, ROOT, CLOSE, saw, chorus off, PW (square) */
};

/* A machine's knobs, if it is one here: Digi Mono's own, or another mod's (digimono_ext). */
static int kinfo_of(int id, struct kinfo *k)
{
    const struct digimono_ext *const *e;
    int m = id - MACH_FIRST;
    if ((unsigned)m >= MACH_SLOTS)
        return 0;
    k->m = m;
    if (m < MONO_MACHINES && m != MONO_EXT7) {
        k->kp = knob_p[m];
        k->def = mono_def[m];
        k->x = 0;
        return 1;
    }
    for (e = digimono_ext; *e; e++)
        if ((*e)->id == (uint32_t)id && (*e)->state_size <= DIGIMONO_EXT_STATE) {
            k->x = *e;
            k->kp = k->x->knob_p;
            k->def = k->x->def;
            return 1;
        }
    return 0;
}

static const uint8_t *seen_kit;
static uint8_t seen_mach[8];

static uint16_t knobw(const uint8_t *snd, int i)
{
    return *(const uint16_t *)(snd + 0x14 + 2 * knob_slot[i]);
}

/* Do knobs B..H hold what a machine switch leaves there? prev: the machine before (id); nk: the new one's
 * knobs (machine-pages may already have set E..H to its defaults: CM_DEFAULT). */
static int switch_defaults(const uint8_t *snd, int prev, const struct kinfo *nk)
{
    struct kinfo ki;
    int i;
    for (i = 2; i < 6 && knobw(snd, i) == (uint16_t)(nk->def[i] << 8); i++)
        ;
    if (i == 6)
        return 1;
    if (kinfo_of(prev, &ki)) {
        for (i = 0; i < KNOBS; i++)
            if (knobw(snd, i) != (uint16_t)(ki.def[i] << 8))
                break;
        if (i == KNOBS)
            return 1;
    }
    for (i = 2; i < 6; i++)                 /* a stock switch resets E..H; B and C it leaves or clears */
        if (knobw(snd, i) != oneshot_def[i])
            return 0;
    return 1;
}

void digimono_tick(void *ctrl)
{
    uint8_t *kit = UI_KIT;
    int t, i;
    (void)ctrl;
    if (!kit)
        return;
    if (kit != seen_kit) {                  /* another pattern's kit: take it as it is */
        for (t = 0; t < 8; t++)
            seen_mach[t] = kit[0x9e + 0xa2 * t];
        seen_kit = kit;
        return;
    }
    for (t = 0; t < 8; t++) {
        uint8_t *snd = kit + 0x20 + 0xa2 * t;
        struct kinfo ki;
        int m = snd[0x7e], prev = seen_mach[t];
        if (m == prev)
            continue;
        seen_mach[t] = m;
        if (!kinfo_of(m, &ki) || !switch_defaults(snd, prev, &ki))
            continue;
        for (i = 0; i < KNOBS; i++) {
            int32_t w = (int32_t)ki.def[i] << 8;
            *(uint16_t *)(snd + 0x14 + 2 * knob_slot[i]) = (uint16_t)w;
            SETPARAM(w, t, knob_slot[i]);
        }
    }
}
