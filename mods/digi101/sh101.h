/* DH-101: an oscillator section after the Roland SH-101, for the DT1's audio tracks.
 *
 * A clean-room engine written from the SH-101's public description (its panel and owner's manual), not
 * from any of its circuitry data. It renders one voice's VCO + SUB + NOISE mix into 16-bit mono samples;
 * the unit's own filter, amp envelope, LFO and effects follow it, as with Digi Mono's machines
 * (gdeo607/digi1_mods, whose polyBLEP "ramp" method this engine borrows, MIT licence).
 *
 * Plain C, 32-bit integer arithmetic only: builds for the MCF54455 with elekloader's flags and for a PC.
 *
 * The seven parameters, 0..127, in Digi Mono's knob order (B C E F G H D):
 *
 *   p[0] B RNGE   range: 16' 8' 4' 2' (four zones of 32)
 *   p[1] C SAW    saw level
 *   p[2] E PULS   pulse level
 *   p[3] F PW     pulse width (MAN: 50 % at 0, narrower up to 5 %) or PWM depth (LFO, ENV)
 *   p[4] G PWM    PWM source: 0..9 MAN; 10..68 LFO, rate 0.1..30 Hz; 69..127 ENV, decay 10 ms..4 s
 *   p[5] H SUB    sub level; three zones pick its mode: 0..42 -1 oct square, 43..85 -2 oct square,
 *                 86..127 -2 oct 25 % pulse (level rises across each zone)
 *   p[6] D NOIS   white noise level
 */
#ifndef SH101_H
#define SH101_H

#include <stdint.h>

#define SH_PARAMS 7
#define SH_INC_MAX 0x40000000u      /* 12 kHz: the highest fundamental the oscillator plays */

enum { SH_RNGE = 0, SH_SAW, SH_PULS, SH_PW, SH_PWM, SH_SUB, SH_NOIS };

/* one voice: 92 bytes */
struct sh101_voice {
    uint32_t ph;                    /* the VCO's phase                                         */
    uint32_t lfo;                   /* the PWM LFO's phase (free-running, as the 101's)        */
    uint32_t rng;                   /* noise generator state, never 0                          */
    uint32_t env;                   /* the PWM envelope, Q16: 65535 at a note's start          */
    uint8_t  wraps;                 /* the VCO's wraps, mod 4: the sub-oscillator's phase      */
    uint8_t  eco;                   /* 1: this note takes the cheaper path (no treble lift)   */
    uint8_t  fresh;                 /* blocks after a trig that render even with the AMP closed */
    uint8_t  spare[1];
    int16_t  x1, x2;                /* the treble lift's last two inputs                       */
    int32_t  hx, hy;                /* the AC coupling high-pass: last input, output (Q8 more) */
    int32_t  gl;                    /* portamento: the pitch offset still to glide, Q24 octaves */
    uint32_t last;                  /* the last block's phase step (the glide starts from it)   */
    uint32_t tgt;                   /* the last block's pitch before the glide (its note's own) */
    uint8_t  retrig;                /* 1: a note has just started (the render opens the window) */
    uint8_t  win;                   /* blocks left in which a pitch change starts a glide       */
    uint8_t  since;                 /* blocks since a pitch jump with no trig yet (255: none)   */
    uint8_t  port;                  /* portamento time 0 (off) .. 127 (3 s): set before each render */
    uint32_t from;                  /* the pitch before that jump (a trig glides from it)       */
    /* page 2: the VCF and the modulation; set before each render (0 everywhere = the filter off) */
    uint8_t  f_cut;                 /* CUT 0..127 (127 = open, 20 kHz)                         */
    uint8_t  f_res;                 /* RES 0..127 (120: TAL's maximum, 127: self-oscillation)  */
    uint8_t  f_env;                 /* ENV: how far the AMP envelope opens it, 0..7 octaves    */
    uint8_t  f_mod;                 /* MOD: the LFO on the cutoff, 0..+-4 octaves              */
    uint8_t  f_kyb;                 /* KYB: cutoff follows the note, 0..100 %                  */
    uint8_t  f_vib;                 /* VIB: the LFO on the pitch, 0..+-1 octave                */
    uint8_t  f_rate;                /* RATE: the LFO, 0.1 .. 30 Hz                             */
    uint8_t  f_on;                  /* the filter is running (its state is live)               */
    uint16_t envq;                  /* the track's AMP envelope, 0..32767: set before each render */
    uint16_t fade;                  /* samples into a note's fade-in from silence (glue)        */
    uint32_t lfo2;                  /* the 101 LFO (VCF MOD, VIB): its phase                    */
    int32_t  s[4];                  /* the ladder's four stages                                */
};

/* 1 while the unit's render is short of time (digi101.c sets it from Digi Mono's guard); a note that
 * starts then leaves out the treble lift */
extern uint8_t sh101_eco;

void sh101_init(struct sh101_voice *v);

/* a note starts: the VCO restarts mid-cycle (the saw at its zero crossing), the PWM envelope at its top */
void sh101_trig(struct sh101_voice *v);

/* n (1..32) samples at phase step inc (a 32-bit phase a 48 kHz sample, the note at 8') */
void sh101_render(struct sh101_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n);
void sh101_rest(struct sh101_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n);

/* display helpers for the knobs (digi101.c) */
int sh101_range(int v);            /* 0..3: 16' 8' 4' 2'                    */
int sh101_pwm_mode(int v);         /* 0 MAN, 1 LFO, 2 ENV                   */
int sh101_pwm_amount(int v);       /* LFO rate / ENV decay step, 1..59      */
int sh101_sub_mode(int v);         /* 0 -1 oct, 1 -2 oct, 2 -2 oct 25 %     */
int sh101_sub_level(int v);        /* 0..127 within the zone                */
uint32_t sh101_lfo_rate(int v);    /* RATE 0..127 -> the LFO's phase step a sample (48 kHz) */

#endif
