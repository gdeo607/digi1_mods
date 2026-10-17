# Digi Mono: Monomachine-style synth machines for the Digitakt mk1

Status: **built, checked in emulation (digiemu, the real OS 1.53 firmware); not yet run on a unit.**
An elekloader mod (`digimono`, 0.6) that needs **core 2.1**. It shares a build with digisophie.

## What it is

Five new SRC machines for the audio tracks. They make their sound with oscillators instead of samples,
after the Monomachine's GND and SWAVE machines:

| machine (menu) | after | what it plays |
|---|---|---|
| `MONO SIN`   | GND-SIN    | a sine |
| `MONO NOISE` | GND-NOIS   | noise: sample and hold (ST), darker (RED), pitched (STON) |
| `MONO SAW`   | SWAVE-SAW  | band-limited saw, 1-3 detuned unison saws, two sub-oscillators (square..saw) |
| `MONO PULSE` | SWAVE-PULS | band-limited pulse with PWM, 2 detuned unison pulses, a square sub |
| `MONO ENS`   | SWAVE-ENS  | four oscillators at set intervals, saw..pulse, with a chorus |
| `MONO VO`    | VO-6       | a formant voice: vowel 1 gliding to vowel 2, consonants at the note's start |
| `POLY SIN`   | (GND-SIN)  | three sines at set notes (a chord on one track), with a pitch envelope |
| `CHORDS`     | (SWAVE-ENS) | four oscillators as a chord: type, inversion, spread; sines or ENS's saw..pulse and chorus |
| `MACRO`      | (Plaits)   | the Digi Macro mod's (since 0.22), hosted here: mods/digimacro/MACRO.md (not clean-room: MIT-licensed code) |

They are picked like any machine: FUNC+SRC, after SLICE. A Digi Mono track needs no sample. Its trigs,
note locks, TUNE, the FLTR, AMP and LFO pages, the sends, p-locks and the track level work on it as on a
sample: the engine's output goes to the voice where the resampled sample would have been.

## Why a rewrite and not a port

The Monomachine's machines are DSP56300 assembly inside the Monomachine OS file. Monomodule
(github.com/shnolk/monomodule) runs that code in a DSP56300 emulator, at about 21 million DSP
instructions a second for one voice. The Digitakt mk1 has no DSP. Its render runs on the main ColdFire,
about 80 % loaded as it is (72 % with digihealth's FAST AUDIO, measured on a unit). So running the
original code even for one voice would cost several times the CPU there is. It would also mean
distributing, or building from the user's file, code derived from the original's DSP program.

This engine is written from scratch in plain C. Its only source is the Monomachine manual: the machine
names, the parameter names and what each is described to do. It contains no code or data of the original, and it
is not sample-exact to a Monomachine. Where the manual leaves a parameter's behaviour open, the choices
below are this engine's own. Check them by ear against a Monomachine, or against Monomodule running your
own Monomachine OS file (`tools/mono_render.py` has the A/B commands).

## The engine (`mono.h`, `mono.c`, `mono_tables.h`)

- **API:** `mono_init`, `mono_trig` (note start), `mono_render(voice, machine, p[7], inc, out, n)` and
  `mono_pitch_inc(pitch)`. `struct mono_voice` is 1,072 bytes, 1 KB of it the ENS chorus delay line.
- **Arithmetic:** 32-bit integers only: no 64-bit maths, no floating point, no library calls. It builds
  with elekloader's Digitakt mk1 flags and for a PC, and the two builds give the same samples.
- **Band-limiting:** polyBLEP. Each jump of a saw, square or pulse is smoothed by a two-sample polynomial.
- **Tables:** `tools/gen_mono_tables.py`, plain maths.
- **Levels:** oscillator levels whose sum is over 1 are scaled back to 1 per block. The output saturates
  to 16 bits.

### The parameters and the SRC page

The engine takes seven parameters, `p[0..6]`: the Monomachine SYN page's first seven knobs, raw 0..127.
TUNE (its eighth) is the Digitakt's own SRC TUNE.

On the Digitakt, knob A stays TUNE. B to H carry the parameters, 0..127, under the machine's own names;
a knob the machine does not have is blank (no name, no knob, turns nothing). Every knob is drawn as a plain
round knob and its value reads in its units (0.7):

| machine | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|
| SIN   | - | - | - | - | - | - | - |
| NOISE | ST | RED | - | STON | - | - | - |
| SAW   | UNIL | UNIW | - | UNIX (`1 SAW`..`3 SAWS`) | SUBX (%) | SUB1 | SUB2 |
| PULSE | UNIL | UNIW | SUB2 | SUB1 | PW (duty %) | PWAD | PWRS |
| ENS   | PCH2 (`+7st`) | PCH3 | PW (duty %, 0 = square) | PCH4 | WAVE (%) | CHRL | CHRW |
| VO    | VOC1 (vowel) | VOC2 | VOIC (Breath) | V-SW | CONS (`S`, `SH`..) | CLEN (ms) | CVOL |
| PSIN  | NOT1 (`+4st`) | NOT2 | NOT3 | EDEP (`+12st`) | ESPD (`+16`, `OFF`) | - | - |

D is the sample slot underneath (SAMP) and H the sample level (LEV); neither means anything for these
machines, so both carry parameters:

- **D:** turning SAMP opens the sample list; on a Digi Mono page it doesn't (two compares in the knob
  handler answer "not SAMP" there), and D turns like any knob.
- **H:** the firmware's voice gain is LEV^2 x the track's level, and 0.6 used "gain 0" as "the voice is not
  playing", so H at 0 cut the voice and anything else played it at full level. 0.7 asks the amp envelope
  instead (`0x4199df54` phase, `0x4199df58` level, + 12 v, as digisophie does): a voice sleeps after 32
  blocks of a silent envelope. Its level is the track's LEVEL and the AMP page, as for any track.

A switch to a Digi Mono machine sets that machine's defaults. For ENS these are the Monomachine's: PCH2-4
at 63, the same pitch.

What each parameter does, as this engine reads the manual:

- **UNIL** is the level of the unison oscillators, and **UNIW** their detune, 0..50 cents (curve
  (v/127)^2, fine at the bottom).
- **UNIX** (SAW) sets how many unison saws play: 0-42 one (+w), 43-85 two (+w, -w), 86-127 three
  (+w, -w, +w/2).
- **SUB1 / SUB2** (PULSE: **SUB**) are the levels of the sub-oscillators one and two octaves down,
  locked to the main oscillator. **SUBX** (SAW) fades them from square to a falling saw.
- **PW** is the pulse width: 64 = 50 %, range 1.2 %..98.4 %. **PWAD** is the PWM depth and **PWRS** the
  PWM rate, 0.05..20 Hz (triangle).
- **PCH2..4** (ENS) put oscillators 2-4 in semitones from oscillator 1: 63 = the same pitch, range
  -36..+36. The oscillators also carry a fixed spread of +4, -4 and +7 cents. Without it, four
  oscillators at one pitch lock into a comb whose timbre depends on their random start phases, and a note
  can lose its fundamental.
- **WAVE** (ENS) fades from saw (0) to pulse (127). **CHRL / CHRW** are the chorus level and width: a
  7 ms delay swung by up to +-2.5 ms at 0.6 Hz.
- **CHORDS** (0.19) is MONO ENS's four oscillators (or four sines) set to a chord on the note:
  - **TYPE** picks one of 24 chords of four notes, 5 or 6 values each (MAJ 0 4 7 12, MIN 0 3 7 12, SUS2,
    SUS4, DIM, AUG, MAJ6, MIN6, MAJ7 0 4 7 11, MIN7, DOM7, MMAJ7, M7B5, DIM7, 7SUS4, ADD9 0 4 7 14, MADD9,
    MAJ9 0 4 11 14, MIN9, DOM9, QUART 0 5 10 15, FIFTH 0 7 12 19, OCT 0 0 12 12, UNI 0 0 0 0). A triad
    doubles its root an octave up.
  - **INV** (32 values a step): that many of the lowest notes go up an octave, ROOT to 3RD.
  - **SPRD** (32 values a step): the voicing opens. From the lowest note up, the notes are raised by
    0 0 0 0 (CLOSE), 0 0 1 1 (OPEN), 0 1 1 2 (WIDE) or 0 1 2 3 (XWIDE) octaves.
  - **WAVE** 0-15 plays four sines (0.20: by a recurrence on the EMAC, restarted from the sine table every
    chunk), 16-127 ENS's saw (0 %) to pulse (100 %); PW, CHRL,
    CHRW and ENS's fixed +4 / -4 / +7 cent spread are ENS's. With MAJ, CLOSE and a saw, CHORDS plays what
    ENS plays at PCH +4 +7 +12.
  - **Cost** (0.20, tests/emu_mono.py): saws about 1,240 instructions a voice a block, sines 2,320, the
    chorus about 1,000 more; the notes' steps are kept in the voice while the note and chord stay.
  - **Checks** (tests/mono_signal.py): every TYPE x INV x SPRD gives its notes; sine chords put partials
    at them with no harmonics; WAVE saw / square; the level of ENS. ENS itself is 0.18's, sample for sample.
- **POLY SIN** (0.13) is MONO SIN three times, in one voice (paraphonic: one filter and amp envelope):
  - **NOT1..NOT3** put the three sines in semitones from the note, 63 = the note, -36..+36 (ENS's PCH).
    At their defaults (all the note) the three are in phase and the machine is MONO SIN, sample for sample
    within 1 of 32767. Each is a third of full scale, so a chord never clips.
  - **EDEP** is a pitch envelope's depth over all three, -64..+63 semitones (64 = none). **ESPD** its speed
    and direction (64 = off): above 64 the notes start EDEP away and glide back to their own (a decay),
    below 64 they start at their own and glide EDEP away (a rise). Further from 64 is faster: a time
    constant of 2 s next to 64 down to 1 ms at the ends (exponential; tools/gen_mono_tables.py, PENV).
  - **Checks** (tests/mono_signal.py): MONO SIN's samples at the defaults; a 0 / +4 / +7 chord gives three
    partials within 0.03 cent at one level, no harmonics (-118 dB); NOT at 27 / 99 is three octaves down /
    up; EDEP +12 starts an octave up and glides to the note, -12 an octave down; ESPD below 64 rises an
    octave; ESPD 64 holds the note; further from 64 settles sooner.
  - **Cost:** about 3,000 instructions a block (three interpolated sines a sample), 3.6 % of the render.
- **VO** is a glottal source through three vowel resonators:
  - **The source:** a band-limited saw through a one-pole low-pass; VOIC mixes in breath noise.
  - **The vowels:** the resonators sit at the vowel's first three formants, from published averages of
    measured male vowels (Peterson and Barney, 1952). VOC1 and VOC2 pick vowels along a continuum OO, U,
    AW, AH, UH, AE, EH, IH, EE, ER, and their values show as those names.
  - **The glide:** V-SW glides from VOC1 to VOC2 after the note starts, 5 ms..2 s; 0 keeps VOC1.
  - **The consonant:** CONS picks one of 8 zones: none, S, SH, F, H, T, K, P. Each is a band of noise at
    the note's start that decays over CLEN (2..400 ms; the plosives T, K and P at most 30-40 ms) at level
    CVOL. The vowel fades in under it.
  - **Checks** (tests/mono_signal.py):
    - the formants of OO, AH and EE land within 20 % of the table: OO 327 / 916 Hz, AH 719 / 1112 Hz,
      EE 262 / 2289 Hz;
    - OO -> EE raises F2 over the glide;
    - an S puts a burst 45 dB above the held vowel's above 4 kHz;
    - the fundamental is exact;
    - the level is about 9 dB under a plain saw.
- **ST** (NOISE) is sample and hold: 0 = white; up = fewer new values a second, about 20 kHz down to
  65 Hz. **RED** is a one-pole low-pass (about 60 Hz at 127) with make-up gain. **STON** mixes in a sample
  and hold clocked at twice the note's pitch, which makes the noise pitched.

## How it sits in the firmware (OS 1.53; details in docs/TECHNICAL_NOTES.md)

- **Machines:** eight descriptors in core 2.1's `core_machines`, ids 20..26 and 28 (POLY SIN 26, CHORDS 28),
  and other mods' machines in 20..29 hosted through the collection `digimono_ext` (mods/digimono/digimono_ext.h:
  Digi Macro's MACRO, 27): Digi Mono renders their voices (their state in the voice's chorus-line memory)
  and draws their knobs from their tables. Clear of NEIGHBOR (4), POLY (6),
  DIGISLICER (5) and SOPHIE (7). They have `params` = ONESHOT and `render` = ONESHOT, and digimono
  recognises its voices through core's `core_track_machine`.
- **Stack (0.21):** the render is an interrupt (level 5) on the interrupted task's stack; `digimono_rblock`
  switches to a 4 KB stack of Digi Mono's own (`digimono_stack`) for `digimono_blocks`, whose deepest
  calls take about 1.9 KB (MACRO's drums), and back.
- **Audio:** the voice loop `0x400757fe` resamples each voice's sample, 64 samples at 96 kHz. A half-band
  filter brings them to 48 kHz as 32 Q31 samples at `0x8000eb70`, and the filter stage `0x400761b8`
  reads them from there. Between the two, at `0x4007606e`, `digimono_rblock` overwrites the block of a
  Digi Mono voice with the engine's output (sample << 16: a full-scale 16-bit sample's level).
  - **Note starts** come from bit v of `0x80001228`.
  - **Pitch** is the note word `0x80001f28[v]` (MIDI note << 16) plus TUNE, `(word(17) - 16384) << 8`,
    in Q16 semitones. The render computes a sample's rate from the same inputs, so note locks, TUNE locks
    and pitch LFOs work. The knob words are the smoothed ones after the LFO stage
    (`0x80002772 + 106 v`), so LFOs and p-locks reach them too.
  - **A voice not playing** (no start, and its amp envelope idle and below 2^19 for 32 blocks) is not
    rendered (0.7; 0.6 used the voice gain, which knob H, LEV underneath, sets to 0).
- **SRC and LFO pages (0.24):** drawn by elekloader's machine-pages mod (core 3.0) from each machine's
  description: its descriptor's tail (`"UI30"` and a `struct cm_ui`, digitakt-mk1/core3.h), `digimono_ui_*`
  in digimono.c. SLICE's page (parameter ids 0x84..0x8b) with the knobs a machine does not have blank, each
  knob it has with its short and long name, drawn as BR's (a plain knob, 0..127), a 0..127 range and the
  machine's default, its value text from `digimono_fmt_1..7` (the machine's units: semitones, %, ms,
  vowels, chords), and D an engine knob (no sample list). The LFO page names the same knobs (ONESHOT's
  parameters, ids 0x6c..0x73, which the machines take) in the machine's group (MSAW...), "Unused" / "-"
  for one it does not have. Digi Mono patches no firmware place of its own any more; up to 0.23 it hooked
  12 (names, ranges, value texts, the SAMP compares, the LFO box) and digichain's 18 (mods/digichain, in
  this repository's history).
- **Render (0.24):** machine-pages' `ev_render_voices` at order 30 (up to 0.23 a jsr at `0x40077fc2`): the
  same point, after playback, before the filter.
- **Defaults:** `ev_tick` watches the UI kit for a switch.

## Verified

- **`tests/mono_signal.py`** (the engine, PC build) measures every machine and parameter:
  - **pitch:** within 0.35 cent over notes 24..108;
  - **SIN:** harmonics below -118 dB;
  - **aliasing:** against a naive oscillator, SAW -31 / -28 / -26 dB and PULSE -32 / -32 / -30 dB at notes
    84 / 96 / 103, where the naive one gives -16 / -13 / -11;
  - **subs:** at 1/2 and 1/4 of the pitch;
  - **unison:** partials at the detune UNIW asks for;
  - **PULSE:** duty within 1 % of PW, and a PWM swing;
  - **ENS:** the intervals and the chorus;
  - **NOISE:** the RED, ST and STON behaviour;
  - **random settings:** bounded, with no DC.
- **`tests/emu_mono.py`** runs the engine built for the ColdFire on an emulated ColdFire V4e. It equals the
  PC build bit for bit (80 voices, 1,920 blocks), keeps the C calling convention and needs no library code.
- **`tests/digiemu_mono.py`** boots a core 2.1 + digimono build of the real OS 1.53 in digiemu, puts each
  machine on track 1 of an empty pattern, turns knobs, records trigs and plays. For all five machines:
  - **machine:** the track takes it;
  - **defaults and ranges:** the defaults land, and turned knobs move over 0..127;
  - **starts:** the voice starts at every trig;
  - **samples:** every block the voice hands on (over 21,000 a run) equals the engine's own, replayed on
    the PC from the same starts, notes and knob words, bit for bit; a voice not playing is silence;
  - **master:** the master mix has the note (262 Hz at C4) or, for ENS, the note and its PCH intervals.

## Cost

Measured in the real render: instructions a block in the mods' code (core + digimono) while track 1
plays. The stock render is about 84,000 a block.

| | instructions a block |
|---|---|
| MONO SAW at its defaults | ~2,400 |
| MONO SAW, every oscillator on | ~5,900 |
| MONO ENS with the chorus | ~6,600 |
| MONO ENS (0.9), at its defaults | ~1,800 (0.8: ~3,000) |
| MONO VO (0.9), a vowel | ~2,400 (0.7: ~4,400 instructions, ~12,300 cycles with its divides) |
| MONO VO (0.9), while a consonant sounds | ~4,200 |
| a Digi Mono track not playing | ~100 |

**MONO VO (0.8)** was the heaviest by far in cycles: two divides a sample (about 35 cycles each on the
ColdFire) for the consonant's fade-in and decay, and its three formant filters' state loaded and stored
through the voice on every sample. Now the vowel (source, breath, formants) runs at 24 kHz, every other
sample, with the output interpolated between (everything it makes is under 4 kHz; above 12 kHz the output
is at most 48 dB under the voice); the consonant (S at 6 kHz) still runs at 48 kHz, only while it lasts;
the ramps are running sums; the state is in locals for the block. An estimate of ColdFire cycles a voice
a block (instruction kinds weighted: divide 35, multiply 4, load 2): a vowel ~4,400 (was ~12,300).

**0.9** goes further on the two heaviest:

- **ENS:** its four saws were four passes over a buffer, each sample loaded, a saw with its blep tests
  worked out and stored four times. Now the four (and with WAVE the four a duty later) are one ramp: the
  sum of their phases steps by the sum of their increments, drops by a cycle where one wraps, and gets the
  blep only on the samples beside a wrap, found from one wrap to the next. Within 3 (16-bit) of 0.8's
  samples. ~2,700 estimated cycles at the defaults (0.8: ~4,200), ~5,200 at the heaviest (~8,200).
- **VO:** the consonant's noise band runs at 24 kHz for SH, H, T, K and P (all at 4 kHz or under, halfway
  values between); S and F keep 48 kHz. The consonant is added over the vowel in a second loop, so neither
  loop has more values than the CPU has registers; the vowel alone runs a pair of samples a pass. ~3,700
  estimated cycles for a vowel, ~6,400 while the consonant sounds (0.8: ~4,400 and ~7,100; 0.7: ~12,300).

**0.10, VO again.** Its vowel ran the source and the three formant resonators in one loop: twelve
values for three filters and more for the source, so gcc kept several on the stack and loaded and
stored them every sample. Now the vowel runs in passes over up to 16 of its 24 kHz samples: the source
into a buffer, then each resonator on its own over it (its four values in registers), then the output
pairs. The consonant phase makes its vowel the same way. Each resonator takes one multiply less
(`(s * q - q * bp) >> 14` is now `((s - bp) * q) >> 14`, and the consonant's band likewise), and the
consonant's level is a running ramp rather than a multiply a sample. Within rounding of 0.9's samples
(the difference is 60-67 dB under the voice). Estimated cycles a voice a block, the same model before
and after (`divide 35, multiply 4, load 2, store 1, branch 2, other 1`, tests/emu_mono.py's build):

| VO (0.10) | 0.9 | 0.10 |
|---|---|---|
| a vowel | 3,900 | 3,280 (-16 %) |
| a vowel with breath (VOIC 60) | 4,330 | 3,570 (-18 %) |
| while SH sounds | 6,680 | 5,490 (-18 %) |
| while S sounds (48 kHz) | 7,800 | 6,300 (-19 %) |

**0.11, ENS.** Its sample loop checked WAVE and the chorus on every sample and, with both in one loop,
kept its values on the stack. Now there is one copy of the loop for each of WAVE on / off and chorus on /
off (one inline function, its two flags constant at each call), so each copy holds only its own work; the
two buffers WAVE needs are cleared only when it is on; without WAVE the level is a shift (its scale is
1 then); a block's chorus glide step is a shift, and the semitone interval's octave (u / 12) a multiply.
The output is the same as 0.10's, bit for bit. Estimated cycles a voice a block, the same model:

| ENS (0.11) | 0.10 | 0.11 |
|---|---|---|
| defaults (WAVE and the chorus off: CHRL 0) | 2,950 | 2,100 (-29 %) |
| the chorus on (CHRL 127), WAVE off | 4,450 | 3,550 (-20 %) |
| WAVE and chorus on (heaviest) | 5,710 | 5,620 (-2 %) |

The heaviest case keeps its five multiplies a sample (the second ramp's level, the scale, the chorus's
interpolation, level and mix) and the oscillators' wrap search.

A table for the resonators' 1 / Q (instead of three divides a block) was tried and left out: it saved
about 30 cycles and moved the bandwidth up to 9 % between vowels. Running the loops from on-chip SRAM
(`.fast`) was left out too: it needs digihealth in the build, and the VO voices render one after another,
so their loops stay in the instruction cache after the first.

Each playing Digi Mono voice adds 3-8 % to the render. The render already runs at about 80 % (72 % with
FAST AUDIO), so **two to four Digi Mono tracks playing at once is the safe range until it is measured on
a unit** with digihealth's SYSTEM INFO. Eight heavy ones would overload it. Ways to make it cheaper:

- **hand-written inner loops:** gcc's saw loop is about 19 instructions a sample, and a hand-written
  ColdFire loop should need about 9;
- **the `.fast` section:** placing the inner loops in on-chip SRAM, as FAST AUDIO does with the stock
  render;
- **skipping silent voices** (done in 0.7): not rendering a voice whose amp envelope has closed (the envelope is applied
  after the filter stage and has not been located yet).

## The overload guard (0.17; in steps since 0.23)

Each render is timed (core's ev_render_in / ev_render_out, DTIM0 at the 132 MHz bus clock: 88,000 ticks a
32-frame period), and so is each voice's block. 0.17 rested every Digi Mono voice for 16 ms whenever three
renders in a row went over 94 % of the period. Under a load just over that line it did so again and again:
the voices sounded for a few blocks out of every 27 (heard on a unit with two Digi Mono tracks, a MACRO
track and samples). 0.23 steps through cheaper ways first:

| step | when | what |
|---|---|---|
| eco (`digimono_eco`) | a render over 7/8 of the period; until 2 s of renders under 5/8 | ENS and CHORDS fade their chorus out over 8 blocks (the line is still written and the LFO moves, so it fades back in without a jump; the dry part keeps its level); MACRO's 2OP FM starts its notes at 2x instead of 4x (a note keeps its rate); PARTCL fires four of its six particles |
| one voice | 3 renders in a row over 94 % | the voice whose block took longest rests: 32 ms, doubled each time again until the render is calm (up to 1 s), then until the render has room for the block it took (so a voice that does not fit does not come back for blips) |
| every voice | 12 renders in a row over 94 % | 16 ms, as 0.17: the load is not Digi Mono's |

With eco off every machine's output is 0.22's, bit for bit. Saved by eco, a voice a block (instructions,
tests/emu_mono.py and emu_macro.py; heavy settings): ENS 3,186 -> 2,321, CHORDS sines 3,355 -> 2,457,
CHORDS saw 3,425 -> 2,570; MACRO 2OP FM with feedback 16,700 -> 9,300 cycles (10.0 -> 5.6 % of a block),
PARTCL 16,200 -> 12,900 (9.7 -> 7.8 %). tests/guard_sim.py runs the guard's own code under made-up loads.

## The Digitakt's pages on a Digi Mono track (0.5, tests/digiemu_mono_fx.py)

0.3 wrote the engine's block right after the resampler (0x4007606e). Measured at the codec output
in digiemu, that version ignored the AMP envelope and VOL. 0.5 renders the machines as ONESHOT, picks
Digi Mono voices by core's `core_track_machine`, and writes into `0x80001a18 + 128 v` after the voice
loop (0x40077fba), which is also where digisophie injects. The stages after that point work on the
block in place: 0x40072478, 0x40073280, 0x40073100, 0x40072300, then the mixer.

What works on a Digi Mono track, measured in digiemu on the firmware's own codec output (0.11, all seven
checks of tests/digiemu_mono_fx.py pass):
- **FLTR:** FREQ all the way down takes the output above 1 kHz down 46 dB; RESO raises a harmonic 51 dB.
- **AMP:** the envelope (HOLD and DEC down: the voice sounds 2 % of the time instead of 73 %); VOL at 0
  silences the output completely; PAN full left (sends off) leaves the right channel silent.
- **LFO:** an LFO on FREQ swings the voice's FREQ 0..126 where the filter reads it; one on a Digi Mono knob
  (PULSE PW) swings the pulse width 0.48..0.89.

**No leak (resolved 2026-10-04).** Up to here this section reported that part of the voice reached the
output unaffected by FREQ, VOL and the sends. There is no such route. digiemu's project is the factory
one, and none of its patterns is empty: A16, the one the test plays, already has trigs on track 1, and one
of them is a lock trig that holds VOL 84, DEL and REV 0, FREQ 86 and seven more parameters for several steps
(the render applies a trig's lock list at 0x40074b0a; locked parameters go back at 0x40074a40). A p-lock
overrides the knob, so in those steps the voice was as loud and bright as the locks say: the "leak". The
mixer's own gain for voice 0 is exactly 0 at VOL 0 in every other block, and the sends are post-VOL.
Without the pattern's locks, VOL 0 gives digital silence and FREQ works on the whole voice. Two checks
also measured the wrong thing: a C4 note's spectral centroid cannot fall below its 262 Hz fundamental, so
"the centroid falls to a quarter" could not pass; and the delay and reverb tails, and the track's other
notes, are in the output. The test now drops the pattern's p-locks on track 1, measures FREQ above 1 kHz,
and reads the envelope and the LFO on the voice itself.

## Sharing a build with digisophie (0.6)

digisophie (github.com/soejrd/digisophie, MIT) adds SOPHIE, a metallic percussion machine, on the same
framework. 0.5 could not share a build with it: both claimed machine 7, and both patched four of the same
places. 0.6 moves or re-routes all five clashes:

| clash | 0.5 | 0.6 |
|---|---|---|
| machine id | 7 (MONO NOISE) | ids 20..25 |
| render hook | 0x40077fba, the same as digisophie's | 0x40077fc2 (`pea 0x80001a18`, done in the hook), one instruction later, so both run |
| short name | the entry 0x4000fe8a | its only caller, 0x40030daa (`keep2`), then on to 0x4000fe8a |
| long name | the entry 0x4000feac | its only caller, 0x40032d36 |
| value text | the entry 0x400657ee | its five callers (0x40032d16, 0x40038714, 0x40038854, 0x40039142, 0x400392ea) |

The range hook stays at the entry of 0x40078f0c; digisophie and digislicer wrap its callers, so their
wrappers reach it. Checked:

- **elekloader:** `elekloader.patch --check` accepts core + digimono + digisophie, and with digiutils,
  digimatrix and digieq added; core + digimono + digislicer also combine. digisophie and digislicer do
  not combine with each other: they wrap the same two range callers.
- **digiemu, the combined build:** MONO SAW and MONO VO pass tests/digiemu_mono.py bit for bit (with
  `--menu-before 1`, as SOPHIE is listed first), SOPHIE's own SRC page shows its knobs, and a SOPHIE
  track sounds.

## Other mods (checked with `elekloader.patch --check`, 2026-10-01; `tools/dev.sh elemods` redoes it)

| mod | what | with Digi Mono |
|---|---|---|
| digisophie 0.1.7 | SOPHIE percussion machine (id 7) | combines; checked in digiemu |
| digineighbor 0.6 (irpina) | NEIGHBOR machine (id 4) | combines; does not combine with digisophie (same SRC page entries); combines with digislicer |
| DigiFilter 1.0i (DigiAlchemydsp) | filter TYPEs BP, BP2, COMB, TRASH | combines, also with digisophie or digineighbor |
| digislicer 2.0 | DIGISLICER machine (id 5) | combines; does not combine with digisophie (same range callers) |
| digiutils, digimatrix, digieq (this repo) | | combine |
| Digi Poly 1.0f (this repo) | POLY (id 4, core 2.0a) | does not combine (core 2.1's machine sites) |
| RingTone (DigiAlchemydsp) | master FX for the **Digitone** mk1 | another device: not applicable |

DigiFilter's notes place the stock per-voice filter: `0x40072844(params, buffer, active, voice)`, called
at 0x400780c4 for each voice on `0x80001a18 + 128 v` in place, after Digi Mono's block is written. So
DigiFilter's types should also act on Digi Mono tracks. The filter envelope's level per voice is at
0x4199df58 + 12 v (the stage 0x40073304); FREQ is params + 2.

## Known limits

- **Not run on a unit.** Only emulation so far (digiemu).
- **Needs core 2.1.** Digi Poly 1.0f patches six of the sites core 2.1 owns for its machine slots, so Digi
  Mono and Digi Poly 1.0f cannot be in one build until Digi Poly adds POLY through `core_machines`. Digi
  Mono combines with digiutils, digimatrix, digieq and digislicer (`elekloader.patch --check`).
- **No dials on the SRC page.** The SRC page draws no knob dials for these machines (as for a SLICE track
  without a sample); a knob's value shows while it turns.
- **Two parameters have no knob.** PULSE's SUB2 and ENS's PW are fixed.
- **The stock OS falls back.** A project saved with Digi Mono machines loads them as ONESHOT on the
  stock OS, or on a build without digimono.

## Files

| file | what |
|---|---|
| `mods/digimono/mod.json`, `glue.s` | the elekloader mod: machines, the render hook, the SRC page hooks |
| `tests/digiemu_mono_fx.py` | the FLTR, AMP and LFO pages on a Digi Mono track, in digiemu |
| `mods/digimono/digimono.c` | the render side, the SRC page's names, ranges and values, defaults on a switch |
| `mods/digimono/mono.h`, `mono.c` | the engine |
| `mods/digimono/mono_tables.h` | generated by `tools/gen_mono_tables.py` (`--check` verifies it is current) |
| `tests/mono_lib.py` | the PC build of the engine, loaded with ctypes |
| `tests/mono_signal.py` | what each machine and parameter does, measured |
| `tests/emu_mono.py` | the ColdFire build: bit-exact against the PC build, and its cost |
| `tests/digiemu_mono.py` | the mod in the real firmware (digiemu) |
| `tools/mono_render.py` | render the engine to WAV |
