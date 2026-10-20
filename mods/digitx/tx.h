/* Digi TX: one machine, a whole drum kit. Knob B picks the drum, the other knobs shape it. The models follow
 * the TX-8 / TX-9 drum machines of hugz202's AE-analog-rytm-mods (MIT licence), whose design notes give
 * their pitches, sweeps and decay times as measured on the originals; the synthesis here is our own,
 * fully digital (the Rytm versions drive its analog voices). 32-bit integer arithmetic only: no FPU, no
 * 64-bit maths, no library calls.
 */
#ifndef TX_H
#define TX_H

#include <stdint.h>

enum {                      /* knob B: the drum (16 zones of 8 values; past the last zone, the last drum) */
    TX_BD9, TX_BD8,         /* kicks: 909-style (sweep, hold, click), 808-style (ringing body, sigh)      */
    TX_SD9, TX_SD8,         /* snares: two tuned bodies and a noise                                      */
    TX_LT9, TX_MT9, TX_HT9, /* 909-style toms: a bend and a partial at 2/3 of the pitch                  */
    TX_LT8, TX_MT8, TX_HT8, /* 808-style toms: decay shorter as the pitch goes up, a diode droop         */
    TX_DRUMS
};
#define TX_ZONE_SHIFT 3

enum {                      /* the parameters, 0..127 */
    TX_P_TYPE = 0,          /* B  TYPE                                                                  */
    TX_P_PTC  = 1,          /* C  PTC   pitch                                                           */
    TX_P_DEC  = 2,          /* D  DEC   decay                                                           */
    TX_P_TONE = 3,          /* E  TONE  kicks: sweep depth (9) / drive (8); snares: noise decay (9),
                                        body balance (8); toms: bend (9) / drive (8)                    */
    TX_P_ACC  = 4,          /* F  ACC   level                                                           */
    TX_P_SNAP = 5,          /* G  SNAP  kicks: click (9) / attack step (8); snares and toms: noise       */
    TX_P_CHAR = 6,          /* H  CHAR  BD9 hold, BD8 sigh, SD9/SD8 detune of the 2nd body, toms 9: the
                                        partial, toms 8: the droop                                      */
    TX_PARAMS = 7
};

struct tx_voice {
    int32_t  drum;                  /* the drum this note plays (read at the trig)                       */
    int32_t  blocks;                /* blocks since the trig                                             */
    int32_t  live;                  /* 1 from the trig until every part has died away                    */
    uint32_t ph1, ph2;              /* the bodies' phases                                                */
    int32_t  a1, a2;                /* the bodies' levels, Q16, at the end of the last block             */
    int32_t  an;                    /* the noise's level, Q16                                            */
    int32_t  ac;                    /* the click's level, Q16                                            */
    int32_t  sweep;                 /* a pitch envelope, Q16 (1 at the trig, falling)                    */
    uint32_t rnd;                   /* the noise generator                                               */
    int32_t  hp1, hp2, lp1, lpx;    /* the noise's filters, Q15 states                                   */
    int32_t  hx1, hx2, hy1, hy2;
    int32_t  step_left;             /* BD8: samples still at the attack step's pitch                     */
};

/* knob B's value (0..127) -> its drum */
int tx_drum_of(int v);
extern const char *const tx_drum_name[TX_DRUMS];

void tx_init(struct tx_voice *v);
void tx_trig(struct tx_voice *v, const uint8_t *p);
/* n (<= 32) samples, Q15, of the voice; inc is the note's phase step (note 60, TUNE 0: 261.63 Hz), which
 * transposes the drum's own pitch (C4 plays it as set). */
void tx_render(struct tx_voice *v, const uint8_t *p, uint32_t inc, int16_t *out, int n);

/* for the tests (tests/digitx_model.c): the drum's frequencies, Q4 Hz, and level, Q16, after b blocks */
int32_t tx_exp2_q16(int32_t x_q16);

#endif
