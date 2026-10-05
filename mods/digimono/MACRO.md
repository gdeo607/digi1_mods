# MACRO: Plaits' engines as one Digi Mono machine

Status: **0.14: two engines (WSHAPE, 2OP FM), checked in emulation; not yet run on a unit.** More engines
are added one at a time with the same workflow (below).

MACRO is one more machine in Digi Mono's FUNC+SRC list (machine 27). Its sound comes from engines of
**Plaits**, the macro oscillator by Emilie Gillet (github.com/pichenettes/eurorack, MIT licence), restated
in 32-bit integer arithmetic for the ColdFire, which has no FPU. The licence text is at the top of
`macro.c` and `macro_tables.h`. Plaits is Mutable Instruments' product; this is an unofficial port of its
published code, not affiliated with it.

## The SRC page

| knob | name | what it does |
|---|---|---|
| A | TUNE | the track's TUNE, as on every machine |
| B | ENGN | the engine (its name shows as the value). Read at each note start: a p-lock on B changes the engine on that trig; turning B changes it on the next note |
| C | HARM | Plaits' HARMONICS |
| D | TIMB | Plaits' TIMBRE |
| E | MORP | Plaits' MORPH |
| F | AUX | crossfades Plaits' OUT (0) to its AUX output (127), its second, related sound |

The track's FLTR, AMP, LFO pages, p-locks and the sends work on MACRO as on any machine. The LFO page names
the knobs as above ("MACR:Harmonics"). A switch to MACRO sets ENGN to the first engine, HARM / TIMB /
MORP to 64 and AUX to 0.

## The engines

| engine | Plaits model | HARM | TIMB | MORP | AUX output |
|---|---|---|---|---|---|
| WSHAPE | waveshaping (green 2) | waveshaper curve | wavefolder amount | waveform asymmetry | a sine folded towards a second fold curve |
| 2OP FM | two-operator FM (green 3) | frequency ratio (quantized) | modulation index | feedback: phase feedback below 64, self-modulation above | a sub-oscillator one octave down, modulated |

### WSHAPE
`waveshaping_engine.cc`: a band-limited slope oscillator (Plaits' `Oscillator`, integrated polyBLEP)
through two of five waveshaper tables, crossfaded by HARM, then a wavefolder (Hermite-interpolated fold
tables) whose gain TIMB sets. Both "tame" their range as the pitch rises, as Plaits does.

### 2OP FM
`fm_engine.cc`: a sine modulator and a sine carrier, a quantized ratio table, an index that falls for high
modulators, and feedback through a one-pole low-pass of the carrier.
- **A note with feedback (MORP off its middle) runs 4x oversampled** with Plaits' own decimator, and
  matches Plaits. **A note without feedback runs 2x** with the half-band [-1 0 9 16 9 0 -1] / 32: its
  aliasing measured within 0.5 dB of Plaits' 4x at notes 84-96 and high indices, for half the cost; the
  top octave is a little brighter (-1.2 dB at 15 kHz where Plaits' decimator is at -2.8 dB).
- The rate is chosen at each note start, so turning MORP during a note never switches it (no click); a
  p-lock on MORP lands on a trig. (2x with feedback is not the same: the carrier's highest partials alias
  inside the feedback loop, measured up to +30 % level at full MORP. Hence 4x there.)

## How each engine is checked (the workflow)

1. **Reference:** Plaits' float engine compiled for the PC from its source, rendering a note with given
   knobs (a harness outside this repository, as it builds Plaits' code as it is).
2. **Comparison** (same note, knobs, OUT and AUX): the waveform's residual after aligning to a fraction of
   a sample, and the magnitude spectrum's difference; for FM, the partials' frequencies and levels.
3. **ColdFire:** `tests/emu_macro.py` builds `macro.c` with elekloader's flags for the DT1, runs it
   on an emulated ColdFire V4e, checks it needs no library code and equals the PC build sample for sample
   (1,200 random blocks), and measures its cost.
4. **Firmware:** `tests/digiemu_mono.py --machine MACRO` boots a core + digichain + digimono build in
   digiemu, puts MACRO on a track, turns knobs, plays trigs, and checks every block the voice hands on
   against the engine replayed on the PC from the same starts, notes and knob words.

Results:
- **WSHAPE:** spectrum within -45 to -80 dB of Plaits' for most knob settings and notes 36-96. The
  remainder is a jump Plaits' waveshaper makes when the slope reaches full scale (its table index wraps
  from 255 to 0); both make it, at slightly different sub-sample times.
- **2OP FM:** without modulation within -80 dB; with modulation the partials match Plaits' within about
  1 dB (normal settings) and levels within 0.002 (all feedback settings, 4x). Plaits computes pitches
  through float lookup tables, a fraction of a cent from the exact pitch the port uses, so its narrow FM
  sidebands land a few hertz apart; inaudible. At strong phase feedback (MORP near 0) Plaits itself is
  chaotic: its partials change by up to 10 dB if the pitch moves by 0.05 cent.

## Cost

Estimated ColdFire cycles a voice a 32-frame block (tests/emu_macro.py; the block is 166,667 cycles at
250 MHz, the stock render already uses about 80 % of it):

| engine | knobs at 64, AUX 0 | AUX blended, heavy knobs |
|---|---|---|
| WSHAPE | ~6,900 (4.1 %) | ~12,200 (7.3 %) |
| 2OP FM, no feedback (2x) | ~10,500 (6.3 %) | |
| 2OP FM, feedback (4x) | ~16,000 (9.6 %) | ~24,800 (14.9 %) |

A MACRO track not playing costs nothing. AUX at 0 (the default) or at 127 renders only that output. Keep
an eye on how many MACRO voices play at once: the free part of the CPU is roughly 20 % of a block, shared
with everything else the mods do.

## Next engines

From Plaits' list, in order of how light they are to port: noise (filtered noise), particle, the analog
bass drum, snare and hi-hat, grain (formant), then speech.
