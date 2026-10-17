/* Desktop test of DH-101's SRC page 2, per-track PORT storage and voice learning (digi101.c), against
 * stand-ins for the firmware (host_fw.h). Build and run, from the top folder:
 *   gcc -O1 -DHOST_TEST -Isrc -I<elekloader>/elekloader/sdk/include -o out/digi101_page tests/digi101_page.c \
 *       mods/digi101/digi101.c mods/digi101/sh101.c -lm
 *   out/digi101_page
 * (mods/digi101 has no kitstore.h of its own: build_elemods.py copies src/kitstore.h in.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "digi101_host_fw.h"
#include "digitakt-mk1/core3.h"
#include "../mods/digi101/digimono_ext.h"
#include "../src/kitstore.h"
#include "../mods/digi101/sh101.h"
#define offsetof_port() offsetof(struct sh101_voice, port)

uint8_t *host_ui_kit, *host_engine_kit;
uint32_t host_active, host_voice_start;
uint8_t host_font[1];
int32_t host_amp[25];
uintptr_t host_atlas[2];
int32_t host_lfo[8 * 20];
int16_t host_vwords[8 * 53];
uint8_t host_q[7];
const char cm_ui_v3[1];
#define FMT(i) int32_t digimono_fmt_##i(char *b, int32_t v, int32_t c, int32_t m) { (void)b; (void)v; (void)c; (void)m; return 0; }
FMT(1) FMT(2) FMT(3) FMT(4) FMT(5) FMT(6) FMT(7)
#undef FMT
static uint32_t sprites[85][7];
static int nblit, blit_frame[16], blit_x[16], blit_y[16];
void host_blit(void *b, const void *sp, int x, int y, int m)
{
    (void)b; (void)m;
    if (nblit < 16) {
        blit_frame[nblit] = (int)((const uint32_t (*)[7])sp - sprites);
        blit_x[nblit] = x;
        blit_y[nblit] = y;
    }
    nblit++;
}
uint8_t digimono_eco;
const struct digimono_ext *const digimono_ext[1];
volatile uint8_t core_track_machine[8];
static int nparam, ntext;
/* a stand-in main screen: vptr at +0, the page shown at +180, an audio track's pages at +236 + 8 p */
static uint32_t mainscr[0x200 / 4];
static char pageview[5][16];
static int nswitch, last_switch_page = -1, last_switch_flag = -1;
static char *ctrl_list[3];                      /* the controller's view list: one node */
void host_page_switch(void *m, int page, int flag)
{
    (void)m;
    nswitch++;
    last_switch_page = page;
    last_switch_flag = flag;
    *(void **)((char *)mainscr + 180) = pageview[page];
}
static void show_page(int p) { *(void **)((char *)mainscr + 180) = pageview[p]; }
static char screen[2048];
static char popup[128];
static void *popup_obj;
void host_popup(void *o, const char *fmt, ...)
{
    va_list ap;
    popup_obj = o;
    va_start(ap, fmt);
    vsnprintf(popup, sizeof popup, fmt, ap);
    va_end(ap);
}
void host_text2(void *b, const void *f, int x, int y, int al, int z, const char *tpl, const char *fmt, ...)
{
    char t[64];
    va_list ap;
    (void)b; (void)f; (void)al; (void)z; (void)tpl;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    snprintf(screen + strlen(screen), sizeof screen - strlen(screen), "[%d,%d]%s ", x, y, t);
}
void host_setparam(int32_t w, int32_t t, int32_t s) { (void)w; (void)t; (void)s; nparam++; }
void host_invalidate(void *p) { (void)p; }
void host_fillrect(void *b, int x0, int y0, int x1, int y1, int c) { (void)b; (void)x0; (void)y0; (void)x1; (void)y1; (void)c; }
void host_text(void *b, const void *f, int x, int y, int c, const char *fmt, ...)
{
    char t[64];
    va_list ap;
    (void)b; (void)f; (void)c;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    snprintf(screen + strlen(screen), sizeof screen - strlen(screen), "[%d,%d]%s ", x, y, t);
    ntext++;
}

extern const struct digimono_ext digi101_ext;
int digi101_key(void *brain, void *ev);
int digi101_enc(void *brain, void *ev);
void digi101_draw(void *bmp, void *ctrl);
void digi101_tick(void *ctrl);

static uint8_t kit[0x91e];
static char ctrl[64];   /* the sentinel at +20, the redraw flag at +32, the first node at CTRL_FIRST */
static void setup_main(void)
{
    int p;
    mainscr[0] = MAIN_VPTR;
    for (p = 0; p < 5; p++)
        *(void **)((char *)mainscr + 236 + 8 * p) = pageview[p];
    show_page(0);
    /* the node: next at +0, the view at +8 (pointer-sized slots on the PC: [0] next, [1] view) */
    ctrl_list[0] = (char *)(ctrl + 20);
    ctrl_list[1] = (char *)mainscr;
    {
        char **node = ctrl_list;
        memcpy(ctrl + CTRL_FIRST, &node, sizeof node);
    }
}
static int fails;
#define CHECK(c, ...) do { if (c) printf("PASS "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static int key(int id, int flags)
{
    int ev[8] = {0};
    ev[3] = id;
    ev[4] = flags;
    return digi101_key(0, ev);
}
/* a press and its release; a page key the mod lets through is the stock's: it shows that page */
static int press(int id)
{
    int r = key(id, 1);
    key(id, 0x10);
    if (!r && id >= 19 && id <= 23)
        show_page(id - 19);
    return r;
}
static int enc(int id, int d)
{
    int ev[8] = {0};
    ev[3] = id;
    ev[4] = d;
    return digi101_enc(0, ev);
}
static const char *draw(void)
{
    screen[0] = 0;
    digi101_draw(0, ctrl);
    return screen;
}
static int port_byte(int t) { return XB(kit, t >= 4 ? 6 * t + 4 : 6 * (t + 4) + 5); }
static void tick(void) { digi101_tick(ctrl); }

int main(void)
{
    int t, ok;
    char st[8][1024];
    uint8_t p[7] = {32, 100, 0, 0, 0, 0, 0};
    int16_t out[32];
    host_ui_kit = host_engine_kit = kit;
    setup_main();
    core_track_machine[0] = 29;                 /* track 1: DH-101 */
    core_track_machine[1] = 0;                  /* track 2: ONESHOT */
    core_track_machine[2] = 29;                 /* track 3: DH-101 */
    for (t = 0; t < 8; t++)
        kit[0x20 + 0xa2 * t + 0x7e] = core_track_machine[t];
    tick();
    tick();

    /* the page opens only with SRC pressed on the SRC page of a DH-101 track */
    host_active = 0;
    CHECK(press(20) == 0, "first SRC press goes to the stock (to the SRC page)");
    CHECK(draw()[0] == 0, "page not drawn after one SRC press");
    CHECK(press(20) == 1, "second SRC press taken");
    CHECK(strstr(draw(), "PORT") && strstr(screen, "PORT") && strstr(screen, "OFF"), "page drawn: %s", screen);
    tick();
    nswitch = 0;
    CHECK(press(20) == 1 && draw()[0] == 0, "third SRC press closes it, taken");
    CHECK(nswitch == 1 && last_switch_page == 1 && last_switch_flag == 1,
          "closing re-selects the SRC page through the firmware, no toggle (calls %d, page %d, flag %d)",
          nswitch, last_switch_page, last_switch_flag);
    press(20);
    nswitch = 0;
    CHECK(press(13) == 1 && draw()[0] == 0 && nswitch == 1, "NO closes it, and re-selects the SRC page");
    press(20);
    CHECK(press(21) == 0 && draw()[0] == 0, "FLTR closes it and goes to the stock");
    CHECK(press(20) == 0, "SRC after FLTR: the stock's SRC page again");
    host_active = 1;                            /* an ONESHOT track */
    CHECK(press(20) == 0 && draw()[0] == 0, "on an ONESHOT track the second press is the stock's (waveform)");
    show_page(1);
    host_active = 0;
    press(19); press(20);
    CHECK(key(20, 1 | 2) == 0 && draw()[0] == 0, "FUNC+SRC: the stock's machine list");
    press(19); press(20); press(20);
    CHECK(key(20, 1 | 8) == 1, "a repeat of a taken SRC press is taken");

    /* the shown page is read from the main screen, not guessed: already on SRC (after power-on, say),
     * one press opens the page */
    host_active = 0;
    press(19);
    show_page(1);                               /* the screen moved to SRC without a key we saw */
    CHECK(press(20) == 1 && strstr(draw(), "PORT"), "on the SRC page already: one SRC press opens it");
    press(20);
    show_page(2);                               /* and on FLTR (moved by something else) SRC is the stock's */
    CHECK(press(20) == 0 && draw()[0] == 0, "not on SRC: the press is the stock's");
    press(20);

    /* a screen or pop-up over the main one (Imp/Exp, Save Project, a copy message): the page steps aside */
    {
        static char *popup_node[2];
        static uint32_t popup_view[4] = {0x12345678};
        host_active = 0;
        show_page(1);
        tick();
        if (draw()[0])
            press(20);                          /* closed first */
        CHECK(press(20) == 1 && strstr(draw(), "PORT"), "page open again");
        popup_node[0] = (char *)(ctrl + 20);
        popup_node[1] = (char *)popup_view;
        ctrl_list[0] = (char *)popup_node;      /* main -> pop-up -> end */
        CHECK(draw()[0] == 0, "covered: nothing drawn over the pop-up");
        CHECK(enc(1, 5) == 0 && port_byte(0) == 0, "covered: the knobs are the screen's");
        CHECK(press(13) == 0, "covered: NO is the screen's, and page 2 steps out");
        ctrl_list[0] = (char *)(ctrl + 20);     /* the pop-up gone */
        tick();
        CHECK(draw()[0] == 0, "page 2 stays closed after a key went to the screen on top");
        CHECK(press(20) == 1 && strstr(draw(), "PORT"), "SRC opens it again");
        ctrl_list[0] = (char *)popup_node;      /* covered with no key pressed: */
        tick();
        CHECK(draw()[0] == 0, "hidden while covered");
        ctrl_list[0] = (char *)(ctrl + 20);
        tick();
        CHECK(strstr(draw(), "PORT") != 0, "and back when the screen on top goes");
        {                                       /* a view that is always there when the page opens */
            static char *always[2];
            static uint32_t always_view[4] = {0x55555555};
            press(20);
            always[0] = (char *)(ctrl + 20);
            always[1] = (char *)always_view;
            ctrl_list[0] = (char *)always;
            tick();
            CHECK(press(20) == 1 && strstr(draw(), "PORT"), "a view already after the main screen is not 'on top'");
            ctrl_list[0] = (char *)(ctrl + 20);  /* it goes (fewer views than ever): still shown */
            tick();
            CHECK(strstr(draw(), "PORT") != 0, "fewer views than when it opened: still shown");
            ctrl_list[0] = (char *)always;
            tick();
            CHECK(strstr(draw(), "PORT") != 0, "and when it is back");
            ctrl_list[0] = (char *)(ctrl + 20);
        }
        press(20);
        CHECK(draw()[0] == 0, "and SRC closes it");
        press(20);                              /* open, for what follows */
    }

    /* the main screen made anew elsewhere (the old one's memory still looks like one): the new one is used */
    {
        static uint32_t mainscr2[0x200 / 4];
        void *got = 0;
        memcpy(mainscr2, mainscr, sizeof mainscr2);
        ctrl_list[1] = (char *)mainscr2;
        host_active = 0;
        tick();
        if (draw()[0])
            press(20);
        *(void **)((char *)mainscr2 + 180) = *(void **)((char *)mainscr2 + 236 + 8);   /* on SRC */
        CHECK(press(20) == 1 && strstr(draw(), "PORT"), "page 2 opens with the new main screen");
        nswitch = 0;
        press(20);
        got = *(void **)((char *)mainscr2 + 180);
        CHECK(nswitch == 1 && got != 0, "and closes through it");
        ctrl_list[1] = (char *)mainscr;
        tick();
        press(20);
    }

    /* knob A sets PORT for the active track, kept in its byte; B..H do nothing but are taken */
    CHECK(enc(1, 30) == 1 && port_byte(0) == 30, "A sets track 1's PORT to 30 (byte %d)", port_byte(0));
    CHECK(enc(1, 200) == 1 && port_byte(0) == 127, "clamped at 127");
    enc(1, -127 + 45);
    CHECK(port_byte(0) == 45 && strstr(draw(), "45"), "45 shown: %s", screen);
    CHECK(strstr(draw(), "FREQ") && strstr(screen, "127") && strstr(screen, "RATE") && strstr(screen, "4.5"),
          "a sound that never had them: FREQ 127, RATE 4.5 Hz, the rest 0: %s", screen);
    CHECK(*(uint16_t *)(kit + 0x20 + 0x14 + 92) == 0 && *(uint16_t *)(kit + 0x20 + 0x14 + 94) == 0,
          "and nothing stored for that");
    CHECK(enc(2, -27) == 1 && enc(9, 5) == 0, "B (CUT) taken, LEVEL passes");
    CHECK(*(uint16_t *)(kit + 0x20 + 0x14 + 92) == 27, "CUT 100 stored as 27 in slot 46 (%d)",
          *(uint16_t *)(kit + 0x20 + 0x14 + 92));
    enc(3, 40);                                 /* RES: 32 steps, clamped at 31 */
    enc(4, 3); enc(5, 4); enc(6, 1); enc(7, 15); enc(8, 2);
    {
        uint16_t w46 = *(uint16_t *)(kit + 0x20 + 0x14 + 92), w47 = *(uint16_t *)(kit + 0x20 + 0x14 + 94);
        CHECK(w46 == (27 | 31 << 7 | 15 << 12) && w47 == (3 | 4 << 5 | 1 << 10 | 2 << 12),
              "packing: slot 46 %04x, slot 47 %04x", w46, w47);
        CHECK(strstr(draw(), "100") && strstr(screen, "33%") && strstr(screen, "9.8"), "shown: %s", screen);
    }
    enc(2, 100);
    CHECK(*(uint16_t *)(kit + 0x20 + 0x14 + 92) == (31 << 7 | 15 << 12), "CUT back to 127: stored 0");
    host_active = 2;
    CHECK(strstr(draw(), "OFF") != 0, "track 3's PORT is its own (OFF)");
    enc(1, 70);
    CHECK(port_byte(2) == 70 && port_byte(0) == 45, "track 3 = 70, track 1 still 45");
    host_active = 1;
    CHECK(draw()[0] == 0, "switching to an ONESHOT track closes the page");
    CHECK(enc(1, 5) == 0, "and the knobs are the stock's again");

    /* the stock knob sprites, when the firmware's vector of them looks right */
    {
        int i;
        host_active = 0;
        if (!draw()[0])
            press(20);
        for (i = 0; i < 85; i++) {
            sprites[i][0] = BMP_VT;
            sprites[i][1] = 13;
            sprites[i][2] = 13;
        }
        host_atlas[0] = (uintptr_t)sprites;
        host_atlas[1] = (uintptr_t)(sprites + 85);
        for (i = 0; i < 130; i++)
            tick();
        nblit = 0;
        draw();
        CHECK(nblit == 8, "eight knobs drawn as sprites (%d)", nblit);
        CHECK(blit_frame[0] == 38 && blit_frame[1] == 84, "PORT 45: frame 38, CUT 127: frame 84 (%d %d)",
              blit_frame[0], blit_frame[1]);
        CHECK(blit_x[0] == 24 && blit_y[0] == 34 && blit_x[4] == 24 && blit_y[4] == 7 && blit_x[3] == 102, "placed: A (%d,%d), E (%d,%d)",
              blit_x[0], blit_y[0], blit_x[4], blit_y[4]);
        popup[0] = 0;
        enc(3, 4);
        CHECK(!strcmp(popup, "Resonance=127") && popup_obj == *(void **)((char *)mainscr + 156),
              "a knob turned: the main screen's message, name=value (%s)", popup);
        enc(8, -1);
        CHECK(!strcmp(popup, "LFO Rate=6.5Hz"), "RATE in Hz (%s)", popup);
        enc(8, 1);
        enc(1, 0);
        CHECK(!strcmp(popup, "Portamento=45"), "PORT (%s)", popup);
        CHECK(strstr(draw(), "[33,27]PORT") && strstr(screen, "[33,0]MOD") && strstr(screen, "[111,0]RATE"), "names in the label font, centred: %s", screen);
        sprites[38][1] = 0;                     /* a frame that is not a sane Bitmap: bars */
        nblit = 0;
        draw();
        CHECK(nblit < 8, "a bad frame: not blitted (%d: %d %d)", nblit, blit_frame[0], blit_frame[1]);
        host_atlas[0] = host_atlas[1] = 0;
    }

    /* the bytes are only the free ones of the persistent map */
    {
        int n, owned = 0, clash = 0;
        for (n = 0; n < 48; n++)
            if (n >= 28 && n % 6 >= 4) {
                owned++;
                if (n < 24)
                    clash = 1;
            }
        CHECK(owned == 8 && !clash, "8 bytes owned, none of Digi Matrix's, Digi Poly's or Digi EQ's (tracks 1-4)");
    }

    /* a sound loaded onto track 5 (whose sound holds tracks 1's and 5's bytes) keeps them */
    tick();
    memset(kit + 0x20 + 0xa2 * 4, 0, 0xa2);     /* the load: the whole sound, markers included */
    tick();
    CHECK(port_byte(0) == 45, "sound load on track 5: track 1's PORT kept (%d)", port_byte(0));
    /* a whole kit loaded: its bytes are the truth */
    for (t = 0; t < 8; t++)
        memset(kit + 0x20 + 0xa2 * t, 0, 0xa2);
    XB(kit, 6 * 6 + 5) = 12;                    /* the loaded kit's track 3 PORT */
    tick();
    CHECK(port_byte(2) == 12 && port_byte(0) == 0, "kit load: the kit's own PORTs (3: %d, 1: %d)", port_byte(2), port_byte(0));
    XB(kit, 6 * 4 + 5) = 45;
    tick();

    /* the voice a state belongs to, from the trigs, and its track's PORT reaching the engine */
    digi101_ext.init(st[0]);
    digi101_ext.init(st[2]);
    host_voice_start = 1 << 2;                  /* voice 3 alone */
    digi101_ext.trig(st[2]);
    digi101_ext.render(st[2], p, 0x01000000, out, 32);
    CHECK(((uint8_t *)st[2])[offsetof_port()] == 12, "voice 3 learnt, its PORT is track 3's (12)");
    host_voice_start = (1 << 0) | (1 << 2);     /* voices 1 and 3 together */
    digi101_ext.trig(st[0]);
    digi101_ext.trig(st[2]);
    digi101_ext.render(st[0], p, 0x01000000, out, 32);
    ok = ((uint8_t *)st[0])[offsetof_port()] == 45;
    CHECK(ok, "voice 1 learnt beside a known voice 3: PORT 45");
    {
        struct sh101_voice *v1 = (struct sh101_voice *)st[0], *v3 = (struct sh101_voice *)st[2];
        *(uint16_t *)(kit + 0x20 + 0x14 + 92) = 31 << 7 | 15 << 12;        /* as set above (the kit load since */
        *(uint16_t *)(kit + 0x20 + 0x14 + 94) = 3 | 4 << 5 | 1 << 10 | 2 << 12;   /* cleared them) */
        host_amp[1] = -0x40000000;                  /* voice 1's AMP envelope at half, either sign */
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        digi101_ext.render(st[2], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 127 && v1->f_res == 127 && v1->f_env == 12 && v1->f_mod == 16 && v1->f_kyb == 42 &&
              v1->f_vib == 127 && v1->f_rate == 102, "track 1's filter reaches voice 1 (%d %d %d %d %d %d %d)",
              v1->f_cut, v1->f_res, v1->f_env, v1->f_mod, v1->f_kyb, v1->f_vib, v1->f_rate);
        CHECK(v1->envq == 0x4000, "ENV follows the AMP envelope's level (%d)", v1->envq);
        CHECK(v3->f_cut == 127 && v3->f_res == 0 && v3->f_rate == 85, "track 3's own: open, off");
        {
            int b, quiet = 0, loud = 0, i;
            uint32_t ph;
            host_amp[1] = 0;                        /* the AMP envelope closed and idle */
            host_amp[0] = 0;
            host_voice_start = 1;
            digi101_ext.trig(st[0]);
            for (b = 0; b < 12; b++) {
                ph = v1->ph;
                digi101_ext.render(st[0], p, 0x01000000, out, 32);
                for (i = 0, ok = 0; i < 32; i++)
                    ok |= out[i] != 0;
                if (ok)
                    loud++;
                else if (v1->ph == ph + 32u * 0x01000000u)
                    quiet++;
            }
            CHECK(loud == 8 && quiet == 4, "AMP closed: 8 blocks rendered after the trig, then rest, in time (%d, %d)",
                  loud, quiet);
            host_amp[1] = 0x20000000;               /* sounding: a new note keeps the oscillator running */
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            ph = v1->ph;
            digi101_ext.trig(st[0]);
            CHECK(v1->ph == ph, "a trig over a sounding note: the phase runs on (%08x)", (unsigned)v1->ph);
            host_amp[1] = 0x00060000;               /* closed, the level resting just above zero */
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            digi101_ext.trig(st[0]);
            CHECK(v1->ph == 0x80000000u && v1->fade == 0, "from silence (a level left under -72 dB): restart, faded in");
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            CHECK(abs(out[0]) < 40 && v1->fade == 32, "the fade's first samples near zero (%d)", out[0]);
            host_amp[1] = 0x00100000;
            digi101_ext.render(st[0], p, 0x01000000, out, 32);
            for (i = 0, ok = 0; i < 32; i++)
                ok |= out[i] != 0;
            CHECK(ok, "AMP open again (-66 dB): rendered");
        }
    }

    /* the LFO page's destinations on a DH-101 track */
    {
        int d101_slot2id(uint32_t, int32_t, int32_t);
        extern const struct cm_machine digi101_m29;
        const struct cm_knob *kn = digi101_m29.ui->knob;
        static const int want[8] = {108, 110, 111, 112, 113, 115, 109, 114};
        struct sh101_voice *v1 = (struct sh101_voice *)st[0];
        int i, ok = 1;
        uint16_t *slot = (uint16_t *)(kit + 0x20 + 0x14);
        for (i = 0; i < 8; i++)
            ok &= d101_slot2id(0x400113be, 17 + i, 29) == want[i];
        CHECK(ok, "the list: TUNE SAW NOIS PULS PWM-Source SUB FREQ RESO");
        CHECK(d101_slot2id(0x40065d94, 18, 29) < 0 && d101_slot2id(0x400113be, 18, 3) < 0 &&
              d101_slot2id(0x400113be, 46, 29) < 0 && d101_slot2id(0x400113be, 25, 29) < 0,
              "only the list builder, only DH-101, only slots 17..24 (46.. are gone)");
        CHECK(digi101_m29.id == 29 && digi101_m29.tag == CM_UI_TAG && digi101_m29.ui->page_from == 3 &&
              !strcmp(digi101_m29.ui->group, "101") &&
              !strcmp(kn[1].lfo_long, "Filter Freq") && !strcmp(kn[1].lfo, "FREQ") &&
              !strcmp(kn[6].lfo_long, "Filter Reso") && !strcmp(kn[6].lfo, "RESO") &&
              !strcmp(kn[5].lfo_long, "PWM Source") && !strcmp(kn[5].lfo, "PWM") &&
              !kn[2].lfo && !kn[3].lfo && !kn[4].lfo && !kn[7].lfo && !kn[0].name,
              "the page names them for the LFO page (18 FREQ, 22 PWM Source, 23 RESO; the rest as the knobs)");
        CHECK(!strcmp(kn[1].name, "RNGE") && !strcmp(kn[2].name, "SAW") && !strcmp(kn[3].name, "NOIS") &&
              !strcmp(kn[4].name, "PULS") && !strcmp(kn[5].name, "PW") && !strcmp(kn[6].name, "PWM") &&
              !strcmp(kn[7].name, "SUB") && (kn[3].flags & CM_NOT_SAMPLE) && kn[1].def == 32 << 8 &&
              kn[2].def == 100 << 8 && kn[7].max == 127 << 8 && kn[1].fmt == digimono_fmt_1,
              "the SRC page: B RNGE C SAW D NOIS E PULS F PW G PWM H SUB, defaults 8' and saw");

        /* LFO1 on FREQ (slot 18) at full positive depth, LFO2 on RESO (slot 23) at half negative */
        memset(kit + 0x20 + 0x14 + 92, 0, 4);   /* track 1: page 2 FREQ 127, RESO 0 */
        slot[18] = 32 << 8; slot[22] = 50 << 8; slot[23] = 40 << 8;
        slot[4] = 18 << 8;
        slot[12] = 23 << 8;
        host_vwords[8] = 32767;                  /* DEP +64 */
        host_vwords[16] = 16384 - 8192;          /* DEP -32 */
        host_lfo[0] = -0x40000000;               /* LFO1 at -0.5: -16256 on the word */
        host_lfo[10] = -0x7fffffff;              /* LFO2 at -1, DEP -32: +16256 */
        host_vwords[18] = (32 << 8) - 16256 > 0 ? (32 << 8) - 16256 : 0;   /* the stage clamped RNGE at 0 */
        host_vwords[22] = 50 << 8;
        host_vwords[23] = (40 << 8) + 16256;     /* PWM moved by the stage, unclamped */
        host_amp[1] = 0x20000000;
        memset(p, 0, 7);
        p[SH_RNGE] = 0; p[SH_PW] = 50; p[SH_PWM] = 103;  /* as Digi Mono hands them, moved */
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 63 && v1->f_res == 64, "FREQ 127 - half range, RESO 0 + half range (%d %d)", v1->f_cut, v1->f_res);
        CHECK(host_q[SH_RNGE] == 32 && host_q[SH_PWM] == 40 && host_q[SH_PW] == 50,
              "RNGE (clamped: the kit's) and PWM (the word less the LFO) as set (%d %d %d)",
              host_q[SH_RNGE], host_q[SH_PWM], host_q[SH_PW]);
        host_lfo[0] = 0x7fffffff;
        host_vwords[18] = 32512;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 127 && host_q[SH_RNGE] == 32, "clamped at 127, RNGE still 32 (%d %d)", v1->f_cut, host_q[SH_RNGE]);

        /* LFO1 on PWM Source (slot 22): the PWM knob within 0..68 */
        slot[4] = 22 << 8; slot[12] = 0xff00;
        host_lfo[0] = 0x40000000;                /* +0.5 at full depth: +16256, half of 0..68 */
        host_vwords[22] = (50 << 8) + 16256;
        host_vwords[23] = 20 << 8;
        p[SH_PW] = 113; p[SH_PWM] = 20;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(host_q[SH_PW] == 50 && host_q[SH_PWM] == 20 + 34, "PW as set, PWM 20 + 34 (%d %d)", host_q[SH_PW], host_q[SH_PWM]);
        host_lfo[0] = 0x7fffffff;
        host_vwords[22] = 32512;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(host_q[SH_PWM] == 68 && host_q[SH_PW] == 50, "held at 68, short of ENV (%d %d)", host_q[SH_PWM], host_q[SH_PW]);
        host_lfo[0] = -0x7fffffff;
        host_vwords[22] = 0;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(host_q[SH_PWM] == 0, "and at 0 (%d)", host_q[SH_PWM]);
        host_vwords[23] = 100 << 8; p[SH_PWM] = 100; slot[23] = 100 << 8;
        host_lfo[0] = -0x40000000;
        host_vwords[22] = (50 << 8) - 16256;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(host_q[SH_PWM] == 100, "a knob in ENV is left alone (%d)", host_q[SH_PWM]);

        /* elsewhere: nothing moves */
        slot[4] = 0xff00; slot[12] = 26 << 8;   /* none; the stock filter's FREQ */
        p[SH_RNGE] = 77; p[SH_PW] = 9; p[SH_PWM] = 99;
        digi101_ext.render(st[0], p, 0x01000000, out, 32);
        CHECK(v1->f_cut == 127 && v1->f_res == 0 && host_q[SH_RNGE] == 77 && host_q[SH_PW] == 9 &&
              host_q[SH_PWM] == 99, "other destinations leave FREQ, RESO and the knobs alone");
    }

    printf("\n%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
