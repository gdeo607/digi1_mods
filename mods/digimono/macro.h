/* Digi Mono's MACRO machine: synthesis engines ported from Plaits (Emilie Gillet, MIT licence), in 32-bit
 * integer arithmetic for the ColdFire (no FPU, no 64-bit maths, no library calls). See macro.c for the
 * licence text and mods/digimono/MACRO.md for the engines and how each was checked against the original.
 *
 * One machine, several engines: knob B picks the engine. It is read at each note start (and while a voice
 * has not started yet), so a p-lock on B changes the engine on that trig and a turning knob on the next.
 */
#ifndef MACRO_H
#define MACRO_H

#include <stdint.h>

enum {
    MACRO_WSH = 0,          /* waveshaping: a slope oscillator through a waveshaper and a wavefolder */
    MACRO_FM  = 1,          /* 2-operator FM with feedback, 4x oversampled (2x without feedback)    */
    MACRO_ENGINES
};

/* The parameters, raw 0..127: */
enum {
    MACRO_P_ENGINE = 0,     /* B  ENGN  the engine (MACRO_ENGINES zones over 0..127)               */
    MACRO_P_HARM   = 1,     /* C  HARM  Plaits' HARMONICS                                          */
    MACRO_P_TIMB   = 2,     /* D  TIMB  Plaits' TIMBRE                                             */
    MACRO_P_MORPH  = 3,     /* E  MORP  Plaits' MORPH                                              */
    MACRO_P_AUX    = 4,     /* F  AUX   0 = Plaits' OUT .. 127 = its AUX, a crossfade              */
    MACRO_PARAMS   = 7
};

/* Every engine's state below is made of 32-bit words only (the tests turn a voice's state round between
 * the ColdFire's byte order and a PC's word by word). */

/* The slope oscillator (Plaits' Oscillator, shape SLOPE), Q16 samples. */
struct macro_slope {
    uint32_t phase;
    int32_t  next;                  /* the next sample's value so far, Q16 (0..1 = 0..65536)          */
    int32_t  high;                  /* 1 while on the rising slope                                   */
};

struct macro_wsh {
    struct macro_slope slope, tri;
    int32_t prev_shape, prev_gain, prev_overtone;   /* Q15, the values the last block ended on        */
};

struct macro_fm {
    uint32_t carrier, modulator, sub;               /* phases                                        */
    int32_t  prev;                                  /* the carrier, one-pole filtered (the feedback)  */
    int32_t  hc[6], hs[6];                          /* 2x: the last six samples of carrier and sub    */
    int32_t  car_fir, sub_fir;                      /* 4x: the downsamplers' carried half, Q30        */
    int32_t  prev_amount, prev_feedback;            /* Q15 (amount: Q14)                              */
    int32_t  os4;                                   /* this note runs 4x (it started with feedback)   */
};

struct macro_voice {
    uint8_t engine;                 /* the engine playing                                            */
    uint8_t latch;                  /* 1: take the engine from knob B at the next block              */
    uint8_t pad[2];                 /* (four bytes, then 32-bit words)                               */
    union {
        struct macro_wsh wsh;
        struct macro_fm fm;
    } e;
};

void macro_init(struct macro_voice *m);
void macro_trig(struct macro_voice *m);
/* Render n samples (n <= 32) of 16-bit mono with parameters p[0..6] at phase increment inc (a 32-bit
 * phase step per 48 kHz sample). */
void macro_render(struct macro_voice *m, const uint8_t *p, uint32_t inc, int16_t *out, int n);
/* The engine knob B's value -> its engine, and the engines' names as the knob shows them. */
int macro_engine_of(int b);
extern const char *const macro_engine_name[MACRO_ENGINES];

#endif
