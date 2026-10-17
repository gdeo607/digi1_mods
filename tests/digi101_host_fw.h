/* Stand-ins for the firmware, so that digi101.c's UI, storage and voice logic run on a PC
 * (tests/digi101_page.c). Every "address" is a host object's. */
#include <stdint.h>
#include <stddef.h>
extern uint8_t *host_ui_kit, *host_engine_kit;
extern uint32_t host_active, host_voice_start;
extern void host_setparam(int32_t, int32_t, int32_t);
extern void host_invalidate(void *);
extern void host_text(void *, const void *, int, int, int, const char *, ...);
extern void host_fillrect(void *, int, int, int, int, int);
extern uint8_t host_font[1];
extern int32_t host_amp[25];          /* voice v's AMP level at [3 v] (12 bytes a voice) */
#define BMP_VT        0u
#define CTRL_FIRST    40     /* past the redraw flag at +32: PC pointers are 8 bytes */
#define F_UI_KIT      ((uintptr_t)&host_ui_kit)
#define F_ENGINE_KIT  ((uintptr_t)&host_engine_kit)
#define F_ACTIVE      ((uintptr_t)&host_active)
#define F_AMP_LEVEL   ((uintptr_t)(host_amp + 1))   /* the phase at - 4, as on the unit */
extern uintptr_t host_atlas[2];       /* the knob sprites: a vector's begin and end */
extern void host_blit(void *, const void *, int, int, int);
#define F_KNOB_ATLAS  ((uintptr_t)host_atlas)
#define F_BLIT        ((uintptr_t)&host_blit)
extern void host_text2(void *, const void *, int, int, int, int, const char *, const char *, ...);
extern void host_popup(void *, const char *, ...);
#define F_TEXT2       ((uintptr_t)&host_text2)
#define F_LABEL_FONT  ((uintptr_t)host_font)
#define F_POPUP       ((uintptr_t)&host_popup)
extern int32_t host_lfo[8 * 20];       /* the LFO stage's state, 80 bytes a voice */
extern uint8_t host_q[7];                /* the engine's parameters, as the render hands them on */
extern int16_t host_vwords[8 * 53];     /* the smoothed parameter words, 106 bytes a voice */
#define F_LFO_VAL     ((uintptr_t)host_lfo)
#define F_VOICE_WORDS ((uintptr_t)host_vwords)
#define F_VOICE_START ((uintptr_t)&host_voice_start)
#define F_SETPARAM    ((uintptr_t)&host_setparam)
#define F_INVALIDATE  ((uintptr_t)&host_invalidate)
#define F_TEXT        ((uintptr_t)&host_text)
#define F_FILLRECT    ((uintptr_t)&host_fillrect)
#define F_FONT5       ((uintptr_t)host_font)
extern void host_page_switch(void *, int, int);
#define MAIN_VPTR     0x40183268u
#define F_PAGE_SWITCH ((uintptr_t)&host_page_switch)
