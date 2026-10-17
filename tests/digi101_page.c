/* Desktop test of DH-101 0.24 (digi101.c): its two SRC pages as machine-pages reads them (cm_ui5, elekloader
 * pull request #103), page 2's values reaching the engine from cm_src_value (locks and LFOs applied there),
 * the value texts, the voice a state belongs to, and the rest / fade around the AMP envelope. The firmware's
 * exports (digitakt-mk1/core3.h) are stood in for here. Build and run, from the top folder:
 *   gcc -O1 -DHOST_TEST -I<elekloader with PR #103>/elekloader/sdk/include -o out/digi101_page \
 *       tests/digi101_page.c mods/digi101/digi101.c mods/digi101/sh101.c -lm && out/digi101_page */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "../mods/digi101/digimono_ext.h"
#include "../mods/digi101/sh101.h"

/* core's and machine-pages' exports */
volatile int32_t fw_amp_env[24];
volatile uint32_t fw_voice_start;
const char fw_bitmap_vt[4];
volatile uint8_t core_track_machine[8];
const char cm_ui_v5[1];
static int32_t src[8][16];                     /* cm_src_value's answers: [track][parameter], 8.8 */
int32_t cm_src_value(int32_t t, int32_t k) { return (unsigned)t < 8 && (unsigned)k < 16 ? src[t][k] : 0; }
/* Digi Mono's */
uint8_t digimono_eco;
const struct digimono_ext *const digimono_ext[1];
#define FMT(i) int32_t digimono_fmt_##i(char *b, int32_t v, int32_t c, int32_t m) { (void)b; (void)v; (void)c; (void)m; return 0; }
FMT(1) FMT(2) FMT(3) FMT(4) FMT(5) FMT(6) FMT(7)
#undef FMT

extern const struct digimono_ext digi101_ext;
extern const struct cm_machine digi101_m29;
extern const struct cm_ui5 digi101_page;

static int fails;
#define CHECK(c, ...) do { if (c) printf("PASS "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

/* machine-pages' own check of a cm_ui5 page (mods/machine-pages/src.c, valid()): a page it refuses is not
 * drawn at all */
static int mp_valid(const struct cm_machine *m, const struct cm_ui5 *u)
{
    int k, page;
    if (m->id < 4 || m->id > 127 || m->params != 3 || u->base.page_from > 3 || u->base.flags ||
        u->base.reserved || u->page_count < 1 || u->page_count > 2 || !u->pages ||
        u->reserved[0] || u->reserved[1] || u->reserved[2])
        return 0;
    for (k = 0; k < 8; ++k) {
        const struct cm_knob *p = &u->extra[k];
        if (!p->look || p->look >= 164 || p->reserved || (p->flags & ~31) ||
            (p->flags & (CM_RANGE | CM_DEFAULT)) != (CM_RANGE | CM_DEFAULT) ||
            p->min != 0 || p->max != 0x7f00 || p->def < 0 || p->def > 0x7f00)
            return 0;
    }
    for (page = 0; page < u->page_count; ++page) {
        if (!u->pages[page].title)
            return 0;
        for (k = 0; k < 8; ++k)
            if (u->pages[page].parameter[k] != CM_SRC_BLANK && u->pages[page].parameter[k] >= 16)
                return 0;
    }
    return 1;
}

static const char *text(const struct cm_knob *k, int v)
{
    static char b[16];
    memset(b, 0, sizeof b);
    return k->fmt && k->fmt(b, v << 8, 1, 29) ? b : "(stock)";
}

static int any(const int16_t *o)
{
    int i;
    for (i = 0; i < 32; i++)
        if (o[i])
            return 1;
    return 0;
}

int main(void)
{
    static const char *const n2[8] = {"PORT", "FREQ", "RESO", "ENV", "MOD", "KYB", "VIB", "RATE"};
    static const char *const n1[8] = {"TUNE", "RNGE", "SAW", "NOIS", "PULS", "PW", "PWM", "SUB"};
    const struct cm_ui5 *u = &digi101_page;
    char st[8][1024];
    uint8_t p[7] = {32, 100, 0, 0, 0, 0, 0};
    int16_t out[32];
    int i, ok, t;

    /* ---- the pages machine-pages reads ---- */
    CHECK(digi101_m29.id == 29 && digi101_m29.params == 3 && digi101_m29.tag == CM_UI_TAG &&
          digi101_m29.ui == &u->base && u->base.abi == cm_ui_v5, "descriptor: machine 29, SLICE's params, a cm_ui_v5 page");
    CHECK(mp_valid(&digi101_m29, u), "machine-pages accepts the page (its own checks)");
    for (i = 0, ok = 1; i < 8; i++)
        ok &= !strcmp(u->base.knob[i].name, n1[i]) && !strcmp(u->extra[i].name, n2[i]) &&
              u->pages[0].parameter[i] == i && u->pages[1].parameter[i] == 8 + i;
    CHECK(ok && u->page_count == 2, "page 1 TUNE RNGE SAW NOIS PULS PW PWM SUB, page 2 PORT FREQ RESO ENV MOD KYB VIB RATE");
    CHECK(u->base.knob[3].flags & CM_NOT_SAMPLE, "knob D (NOIS) opens no sample list");
    CHECK(u->extra[0].def == 0 && u->extra[1].def == 127 << 8 && u->extra[2].def == 0 && u->extra[7].def == 85 << 8,
          "defaults: PORT off, FREQ open, RESO 0, RATE 85 (0.23's)");
    CHECK(!strcmp(u->base.group, "101") && !strcmp(u->extra[1].lname, "Filter Freq") && !u->extra[1].lfo,
          "LFO page: group 101, page 2's long names (Filter Freq ...)");

    /* ---- value texts ---- */
    CHECK(!strcmp(text(&u->extra[0], 0), "OFF") && !strcmp(text(&u->extra[0], 45), "45"), "PORT: OFF, 45");
    CHECK(!strcmp(text(&u->extra[1], 100), "100"), "FREQ: 100");
    CHECK(!strcmp(text(&u->extra[5], 0), "OFF") && !strcmp(text(&u->extra[5], 127), "100%") &&
          !strcmp(text(&u->extra[5], 42), "33%"), "KYB: OFF, 33%%, 100%%");
    {
        static const int rv[5] = {0, 50, 85, 100, 127};
        static const char *const rt[5] = {".10Hz", ".94Hz", "4.5Hz", "8.9Hz", "30Hz"};
        for (i = 0; i < 5; i++)
            CHECK(!strcmp(text(&u->extra[7], rv[i]), rt[i]), "RATE %d: %s (%s)", rv[i], rt[i], text(&u->extra[7], rv[i]));
    }

    /* ---- the voice a state belongs to, and page 2's values reaching it ---- */
    core_track_machine[0] = 29;
    core_track_machine[2] = 29;
    for (t = 0; t < 8; t++) {                   /* page 2 at its defaults, as a new sound has it */
        for (i = 0; i < 8; i++)
            src[t][8 + i] = u->extra[i].def;
    }
    src[2][8] = 12 << 8;                        /* track 3: PORT 12 */
    src[0][8] = 45 << 8;                        /* track 1: PORT 45, and a whole page of values, as a */
    src[0][9] = 100 << 8;                       /* p-lock or an LFO leaves them (cm_src_value's) */
    src[0][10] = (127 << 8) + 0x7f;
    src[0][11] = 12 << 8; src[0][12] = 16 << 8; src[0][13] = 42 << 8; src[0][14] = 127 << 8; src[0][15] = 102 << 8;
    digi101_ext.init(st[0]);
    digi101_ext.init(st[2]);
    fw_voice_start = 1 << 2;                    /* voice 3 alone */
    digi101_ext.trig(st[2]);
    digi101_ext.render(st[2], p, 0x01000000, out, 32);
    {
        struct sh101_voice *v1 = (struct sh101_voice *)st[0], *v3 = (struct sh101_voice *)st[2];
        CHECK(v3->port == 12 && v3->f_cut == 127 && v3->f_res == 0 && v3->f_rate == 85,
              "voice 3 learnt: track 3's page 2 (PORT 12, open, RATE 85)");
        fw_voice_start = (1 << 0) | (1 << 2);   /* voices 1 and 3 together */
        digi101_ext.trig(st[0]);
        digi101_ext.trig(st[2]);
        fw_amp_env[1] = -0x40000000;            /* voice 1's AMP envelope at half, either sign */
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->port == 45 && v1->f_cut == 100 && v1->f_res == 127 && v1->f_env == 12 && v1->f_mod == 16 &&
              v1->f_kyb == 42 && v1->f_vib == 127 && v1->f_rate == 102,
              "voice 1 beside a known voice 3: track 1's page 2 (%d %d %d %d %d %d %d %d)", v1->port, v1->f_cut,
              v1->f_res, v1->f_env, v1->f_mod, v1->f_kyb, v1->f_vib, v1->f_rate);
        CHECK(v1->envq == 0x4000, "ENV follows the AMP envelope's level (%d)", v1->envq);
        src[0][9] = 20 << 8;                    /* a lock on the next trig, or the LFO moving FREQ */
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 20, "a new FREQ reaches the engine the next block (%d)", v1->f_cut);
        src[0][9] = -300;                       /* out of range: clamped */
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 0, "clamped at 0");
        src[0][9] = 127 << 8;
        {
            int b, quiet = 0, loud = 0;
            uint32_t ph;
            fw_amp_env[1] = 0;                  /* the AMP envelope closed and idle */
            fw_amp_env[0] = 0;
            fw_voice_start = 1;
            digi101_ext.trig(st[0]);
            for (b = 0; b < 12; b++) {
                ph = v1->ph;
                digi101_ext.render(st[0], p, 0x01000000, out, 32);
                if (any(out))
                    loud++;
                else if (v1->ph == ph + 32u * 0x01000000u)
                    quiet++;
            }
            CHECK(loud == 8 && quiet == 4, "AMP closed: 8 blocks rendered after the trig, then rest, in time (%d, %d)",
                  loud, quiet);
            fw_amp_env[1] = 0x20000000;         /* sounding: a new note keeps the oscillator running */
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            ph = v1->ph;
            digi101_ext.trig(st[0]);
            CHECK(v1->ph == ph, "a trig over a sounding note: the phase runs on (%08x)", (unsigned)v1->ph);
            fw_amp_env[1] = 0x00060000;         /* closed, the level resting just above zero */
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            digi101_ext.trig(st[0]);
            CHECK(v1->ph == 0x80000000u && v1->fade == 0, "from silence (a level left under -72 dB): restart, faded in");
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            CHECK(abs(out[0]) < 40 && v1->fade == 32, "the fade's first samples near zero (%d)", out[0]);
            fw_amp_env[1] = 0x00100000;
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            CHECK(any(out), "AMP open again (-66 dB): rendered");
        }
    }
    printf("\n%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
