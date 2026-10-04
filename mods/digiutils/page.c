/* Digi utilities: turn one SongEditView into the utility page (see page.s).
 *
 * The object keeps everything a SongEditView has (so the firmware treats it as one), but its primary vtable
 * and the two listener vtables that matter point at copies in RAM: draw, key, tick (src/scope.s), the knob
 * listener (eq.c: knobs are the EQ's in the EQ view, else they pass to the screen below) and the LED
 * listener (empty: the page claims no LEDs). The copies are made from the firmware's own table the first
 * time, so nothing of it is stored here.
 */

/* ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ---- */
#ifdef OS154
#define F_400c96be 0x400c98e6
#define F_401b3748 0x401b3ac8
#define F_401b3750 0x401b3ad0
#define F_401b3758 0x401b3ad8
#define F_401b3760 0x401b3ae0
#define F_401b377c 0x401b3afc
#define F_401b3794 0x401b3b14
#define F_401b3798 0x401b3b18
#define F_401b37a4 0x401b3b24
#define F_401b37ac 0x401b3b2c
#define F_401b3804 0x401b3b84
#define F_401b380c 0x401b3b8c
#else
#define F_400c96be 0x400c96be
#define F_401b3748 0x401b3748
#define F_401b3750 0x401b3750
#define F_401b3758 0x401b3758
#define F_401b3760 0x401b3760
#define F_401b377c 0x401b377c
#define F_401b3794 0x401b3794
#define F_401b3798 0x401b3798
#define F_401b37a4 0x401b37a4
#define F_401b37ac 0x401b37ac
#define F_401b3804 0x401b3804
#define F_401b380c 0x401b380c
#endif
/* ---- end of the moved addresses ---- */


typedef unsigned int u32;

#define VT_BASE   ((unsigned)F_401b3748)              /* SongEditView vtable group: offset, typeinfo, slots ... */
#define VT_WORDS  50                       /* .. 0x401b3810 */
#define VT_PRIM   ((unsigned)F_401b3750)              /* object +0 */
#define I(a)      (((a) - VT_BASE) / 4)
#define EMPTY     ((unsigned)F_400c96be)              /* stock empty method (rts) */

extern void digiutils_draw(void), digiutils_key(void), digiutils_tick(void);

/* the page has no EQ view any more: its knobs always go to the screen below */
int digiutils_enc(void *self, void *ev)
{
    (void)self;
    (void)ev;
    return 0;
}

static u32 vt[VT_WORDS];
u32 *digiutils_vtprim;                     /* page.s: "is this the page?" */

void digiutils_adopt(u32 *obj)
{
    int i;
    if (!obj || obj[0] != VT_PRIM)
        return;                            /* not a fresh SongEditView: leave it alone */
    if (!digiutils_vtprim) {
        const u32 *src = (const u32 *)VT_BASE;
        for (i = 0; i < VT_WORDS; i++)
            vt[i] = src[i];
        vt[I(F_401b3758)] = (u32)digiutils_key;   /* slot 2  consumeKeyEvent */
        vt[I(F_401b3760)] = (u32)digiutils_draw;  /* slot 4  drawView */
        vt[I(F_401b377c)] = (u32)digiutils_tick;  /* slot 11 tick */
        vt[I(F_401b3794)] = (u32)digiutils_enc;   /* slot 20 knob listener */
        vt[I(F_401b3798)] = EMPTY;                /* slot 21 LED listener */
        vt[I(F_401b37ac)] = (u32)digiutils_enc;   /* +4 subobject: knob listener entry */
        vt[I(F_401b380c)] = EMPTY;                /* +104 subobject: LED listener entry */
        digiutils_vtprim = &vt[I(VT_PRIM)];
    }
    obj[0] = (u32)&vt[I(VT_PRIM)];
    obj[1] = (u32)&vt[I(F_401b37a4)];
    obj[26] = (u32)&vt[I(F_401b3804)];
}
