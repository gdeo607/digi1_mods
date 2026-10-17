/* Digi Mono's machine slots for other mods (Digi Macro's MACRO is one).
 *
 * Digi Mono renders its machines' voices in the audio render, on a stack of its own, and gives each machine's
 * knobs their names, ranges and values on the SRC and LFO pages. A mod that adds a machine of the same kind
 * registers it twice:
 *   - in core 2.1's core_machines: its descriptor (an id 20..29, its name and icon; params and render
 *     ONESHOT), as Digi Mono's own machines are (the ids in 20..29 that Digi Mono does not use: 27);
 *   - in Digi Mono's collection digimono_ext: a pointer to a struct digimono_ext below.
 * Digi Mono then calls its init, trig and render for every voice that plays it, with the knobs' values,
 * and asks it for the knobs' value texts. A voice's state lives in Digi Mono's voice, in the memory its
 * own machines use for the chorus line (DIGIMONO_EXT_STATE bytes, 4-aligned): state_size at most that.
 *
 * Knob order everywhere: B C E F G H D (A is the track's TUNE). render() gets the seven parameters as
 * knob_p maps them: p[knob_p[k]] = knob k's value, 0..127; a knob with knob_p -1 is not on the page.
 *
 * This file is Digi Mono's (mods/digimono); a mod that uses it carries a copy in its own folder, so that the
 * folder builds on its own (Digi Macro: mods/digimacro). tools/build_elemods.py checks the copy matches.
 */
#ifndef DIGIMONO_EXT_H
#define DIGIMONO_EXT_H

#include <stdint.h>

#define DIGIMONO_EXT_KNOBS 7
#define DIGIMONO_EXT_STATE 1024

struct digimono_ext {
    uint32_t id;                                    /* its machine id (core_machines), 20..29           */
    uint32_t state_size;                            /* a voice's state, bytes (DIGIMONO_EXT_STATE max)  */
    const char *mshort;                             /* the LFO page's group name for its knobs ("MACR") */
    const int8_t *knob_p;                           /* [7] knob -> parameter index, or -1               */
    const uint8_t *def;                             /* [7] the knobs' values when a track turns to it   */
    const char *const *sname;                       /* [7] the knobs' short names ("-": none)           */
    const char *const *lname;                       /* [7] and long ones                                */
    /* knob k's value v (0..127) as text into buf (8 characters at most); 0: show the plain number */
    int (*text)(int k, int v, char *buf);
    void (*init)(void *state);                      /* the voice turns to this machine: a fresh state   */
    void (*trig)(void *state);                      /* a note starts                                    */
    /* n (<= 32) samples, 16-bit, of the voice at phase step inc (a 32-bit phase a 48 kHz sample) */
    void (*render)(void *state, const uint8_t *p, uint32_t inc, int16_t *out, int n);
};

/* 1 while the audio render is short of time (Digi Mono's overload guard): a machine may take a cheaper
 * path, as long as the change makes no click (faded, or from a note's start). Digi Mono 0.23 and later. */
extern uint8_t digimono_eco;

/* Digi Mono 0.24 and later: machine-pages (core 3.0) draws the machines' pages, each from its descriptor's
 * page (struct cm_ui, elekloader's digitakt-mk1/core3.h). A machine here describes its own page there (names,
 * ranges, defaults, which knobs are blank); for its value texts its knobs can name these, Digi Mono's: knob
 * i (B 1, C 2, D 3, E 4 .. H 7) of machine m, from its text() above, or its number. mshort, sname and lname
 * above are not read since 0.24 (the page has them). */
extern int32_t digimono_fmt_1(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_2(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_3(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_4(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_5(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_6(char *buf, int32_t value, int32_t ctx, int32_t m);
extern int32_t digimono_fmt_7(char *buf, int32_t value, int32_t ctx, int32_t m);

/* the collection: pointers to the machines' structs, a null one last (elekloader adds it) */
extern const struct digimono_ext *const digimono_ext[];

#endif
