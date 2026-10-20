/* Digi TX (mod id digitx): TX KIT, a whole drum kit as one machine (tx.h), one of Digi Mono's machines
 * (digimono_ext.h): Digi Mono renders its voices, on its own stack, with its overload guard; machine-pages
 * (core 3.0) draws its SRC page. Knob B picks the drum: BD9 BD8 SD9 SD8 LT9 MT9 HT9 LT8 MT8 HT8, read at each
 * trig, so a p-lock on B plays a different drum on that step. It names no firmware address.
 *
 * Two registrations: the machine list's entry (core_machines: id 30, TX KIT; ONESHOT's parameters and render,
 * as Digi Mono's machines) and the engine (Digi Mono's digimono_ext, 0.25 or later: ids 20..31). */
#include "digitakt-mk1/core3.h"
#include "tx.h"
#include "digimono_ext.h"

static void dtx_init(void *st)
{
    tx_init((struct tx_voice *)st);
}

/* Digi Mono calls trig, then render with the knobs in the same block: the drum is set up there */
static void dtx_trig(void *st)
{
    ((struct tx_voice *)st)->live = -1;
}

static void dtx_render(void *st, const uint8_t *p, uint32_t inc, int16_t *out, int n)
{
    struct tx_voice *v = (struct tx_voice *)st;
    if (v->live < 0)
        tx_trig(v, p);
    tx_render(v, p, inc, out, n);
}

/* ---- the knobs, in Digi Mono's order B C E F G H D ------------------------------------------------- */
static const int8_t knob_p[DIGIMONO_EXT_KNOBS] = {
    TX_P_TYPE, TX_P_SWT, TX_P_TONE, TX_P_DRV, TX_P_SNAP, TX_P_CHAR, TX_P_VAR};
static const uint8_t knob_def[DIGIMONO_EXT_KNOBS] = {0, 64, 64, 0, 40, 64, 0};
static const char *const knob_sname[DIGIMONO_EXT_KNOBS] = {"TYPE", "SWT", "TONE", "DRV", "SNAP", "CHAR", "VAR"};
static const char *const knob_lname[DIGIMONO_EXT_KNOBS] = {
    "Drum", "Sweep Time", "Tone", "Drive", "Snap", "Character", "Variation"};

/* B shows the drum; the others their number */
static int knob_text(int k, int v, char *buf)
{
    const char *s;
    if (k != 0)
        return 0;
    s = tx_drum_name[tx_drum_of(v)];
    while ((*buf++ = *s++))
        ;
    return 1;
}

const struct digimono_ext digitx_ext = {
    30, sizeof(struct tx_voice), "TXK", knob_p, knob_def, knob_sname, knob_lname, knob_text,
    dtx_init, dtx_trig, dtx_render,
};

/* ---- the machine list's entry: an 11 x 7 icon, one word a column, the top row in bit 25: "TX" ------ */
static const uint32_t icon_px[11] = {
    0x80000000, 0x80000000, 0xfe000000, 0x80000000, 0x80000000, 0x00000000,
    0xc6000000, 0x28000000, 0x10000000, 0x28000000, 0xc6000000,
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

/* Its SRC page, drawn by machine-pages: A TUNE (the drum's pitch: C4 and 0 play it as the original), B TYPE,
 * C SWT, D VAR, E TONE, F DRV, G SNAP, H CHAR (no pitch or decay knob: TUNE and the AMP envelope are those), each a 0..127 knob drawn as BR's, its value text from Digi Mono's
 * (B shows the drum). The LFO page names them in the group "TXK". */
#define P_BR 0x86
#define KNOB(sn, ln, d, i, f) { .name = sn, .lname = ln, .look = P_BR, .flags = CM_RANGE | CM_DEFAULT | (f), \
    .min = 0, .max = 127 << 8, .def = (d) << 8, .fmt = digimono_fmt_##i }
static const struct cm_ui page = {
    .abi = cm_ui_v3, .page_from = 3, .group = "TXK",
    .knob = {
        [0] = { .name = "TUNE", .lname = "Tune" },
        [1] = KNOB("TYPE", "Drum", 0, 1, 0),
        [2] = KNOB("SWT", "Sweep Time", 64, 2, 0),
        [3] = KNOB("VAR", "Variation", 0, 3, CM_NOT_SAMPLE),
        [4] = KNOB("TONE", "Tone", 64, 4, 0),
        [5] = KNOB("DRV", "Drive", 0, 5, 0),
        [6] = KNOB("SNAP", "Snap", 40, 6, 0),
        [7] = KNOB("CHAR", "Character", 64, 7, 0),
    },
};
#undef KNOB

/* id, name, short name, icon, params (ONESHOT), render (ONESHOT); core 3.0's tail: its page */
const struct cm_machine digitx_m30 = {30, "TX KIT", "TXK", &icon, 0, 0, CM_UI_TAG, &page};
