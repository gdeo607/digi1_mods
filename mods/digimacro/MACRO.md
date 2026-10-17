# MACRO: Plaits' engines as one machine (the Digi Macro mod)

Status: **Digi Macro 1.1 (MACRO as in Digi Mono 0.18-0.21, and lighter paths under load): eight engines (WSHAPE, 2OP FM, NOISE, PARTCL, BDRUM, SNARE, HIHAT, GRAIN), Plaits' main
output (OUT) only; 1.0 played on a unit with no freeze.** More engines are added one at a time with the same workflow
(below).

MACRO is a machine in the FUNC+SRC list (machine 27), in its own elekloader mod, `digimacro`, since
Digi Mono 0.22. Digi Mono hosts it (mods/digimono/digimono_ext.h): it plays MACRO's voices in the audio render, on
its own stack and under its overload guard, keeps each voice's MACRO state in the voice, and draws the
knobs with the names and values Digi Macro gives (`digimacro.c`). Digi Macro imports Digi Mono's sine
table (`MACRO_SINE`, mods/digimono/sine.c) and, since 1.1, its overload guard's flag (`digimono_eco`). Its sound comes from engines of
**Plaits**, the macro oscillator by Emilie Gillet (github.com/pichenettes/eurorack, MIT licence), restated
in 32-bit integer arithmetic for the ColdFire, which has no FPU. The licence text is at the top of
`macro.c` and `macro_tables.h`. Plaits is Mutable Instruments' product; this is an unofficial port of its
published code, not affiliated with it.

## The SRC page

| knob | name | what it does |
|---|---|---|
| A | TUNE | the track's TUNE, as on every machine |
| B | ENGN | the engine (its name shows as the value), in 16 zones of 8 values (below). Read at each note start: a p-lock on B changes the engine on that trig; turning B changes it on the next note |
| C | HARM | Plaits' HARMONICS |
| D | TIMB | Plaits' TIMBRE |
| E | MORP | Plaits' MORPH |
| F, G, H | - | unused |

The track's FLTR, AMP, LFO pages, p-locks and the sends work on MACRO as on any machine. The LFO page names
the knobs as above ("MACR:Harmonics"). A switch to MACRO sets ENGN to the first engine and HARM / TIMB /
MORP to 64.

ENGN zones, 8 values each, so a saved p-lock keeps its engine when engines are added:

| B | 0-7 | 8-15 | 16-23 | 24-31 | 32-39 | 40-47 | 48-55 | 56-63 | 64-127 |
|---|---|---|---|---|---|---|---|---|---|
| engine | WSHAPE | 2OP FM | NOISE | PARTCL | BDRUM | SNARE | HIHAT | GRAIN | GRAIN (for now) |

## The engines

| engine | Plaits model | HARM | TIMB | MORP |
|---|---|---|---|---|
| WSHAPE | waveshaping | waveshaper curve | wavefolder amount | waveform asymmetry |
| 2OP FM | two-operator FM | frequency ratio (quantized) | modulation index | feedback: phase feedback below 64, self-modulation above |
| NOISE | filtered noise | filter response, low-pass to band-pass to high-pass | clock rate of the noise | resonance |
| PARTCL | particle noise | frequency spread of the resonators | density of the impulses | resonance |
| BDRUM | analog bass drum | attack FM, self FM and drive | tone | decay |
| SNARE | analog snare | snappy (noise against body) | tone | decay |
| HIHAT | 808-style metallic hat | noisiness | tone | decay |
| GRAIN | grainlets | formant ratio and carrier bleed | formant frequency | carrier shape |

Each engine plays Plaits' main output (OUT). Plaits' second output (AUX: the synthetic kick and snare, the
ring-modulated hat, the Z oscillator, FM's sub-oscillator, WSHAPE's second fold, NOISE's two band-passes,
PARTCL's raw impulses) was dropped in 0.18: it freed about 10.5 KB of the mods' shared memory and took the
sub-oscillator out of FM's feedback loop. Every OUT sample is the same as 0.17's.

The drums (BDRUM, SNARE, HIHAT) make their own envelope from each trig, with Plaits' accent fixed at 0.8:
set the track's AMP decay long and play them with trigs. A drum costs nothing once its sound has died
away (every state below -96 dB) until the next trig. NOISE and PARTCL take each trig as Plaits' trigger
input (the clocks restart). Plaits' reverb on the lower half of PARTCL's MORPH is left out (no memory for it).

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

### NOISE
`noise_engine.cc`: a clocked noise (sample-and-hold with band-limited steps) at TIMB's rate through a
state-variable filter at the note, its response swept by HARM, its resonance by MORP. The filters run on the CPU's multiply-accumulate unit in 24-bit
fractional arithmetic, rounded, with coefficients kept consistent so high resonance stays stable.

### PARTCL
`particle_engine.cc`: random impulses (density from TIMB) into six resonant band-passes, each retuned at
random within HARM's spread at its next impulse, then a low-pass at the note and Plaits' limiter.

### BDRUM, SNARE, HIHAT
`analog_bass_drum.h`, `analog_snare_drum.h`, `hi_hat.h` (SquareNoise): the trigger pulses, resonators, noise and VCAs as in Plaits. Two reductions keep them light:
the kick's resonator retunes every 8 samples once the attack is over (at every other during the attack
FM), and the hi-hat's filter coefficients are worked out again only when TIMB moves.

### GRAIN
`grain_engine.cc`, `grainlet_oscillator.h`, `z_oscillator.h`: two grainlets (a sine formant hard-synced to
a shaped carrier), the second HARM away (-2..+2 octaves), summed and DC-blocked.

## How each engine is checked (the workflow)

1. **Reference:** Plaits' float engine compiled for the PC from its source, rendering a note with given
   knobs (a harness outside this repository, as it builds Plaits' code as it is).
2. **Comparison** (same note and knobs; up to 0.17 also the AUX output): the waveform's residual after aligning to a fraction of
   a sample, and the magnitude spectrum's difference; for FM, the partials' frequencies and levels.
3. **ColdFire:** `tests/emu_macro.py` builds `macro.c` with elekloader's flags for the DT1, runs it
   on an emulated ColdFire V4e, checks it needs no library code and equals the PC build sample for sample
   (1,200 random blocks), and measures its cost.
4. **Firmware:** `tests/digiemu_mono.py --machine MACRO` boots a core + machine-pages + digimono build in
   digiemu, puts MACRO on a track, turns knobs, plays trigs, and checks every block the voice hands on
   against the engine replayed on the PC from the same starts, notes and knob words.

Results (the deterministic engines waveform for waveform; the noisy ones by spectrum and level, against
the spread of Plaits' own renders with different random seeds):
- **WSHAPE:** spectrum within -45 to -80 dB of Plaits' for most knob settings and notes 36-96. The
  remainder is a jump Plaits' waveshaper makes when the slope reaches full scale (its table index wraps
  from 255 to 0); both make it, at slightly different sub-sample times.
- **2OP FM:** without modulation within -80 dB; with modulation the partials match Plaits' within about
  1 dB (normal settings) and levels within 0.002 (all feedback settings, 4x). Plaits computes pitches
  through float lookup tables, a fraction of a cent from the exact pitch the port uses, so its narrow FM
  sidebands land a few hertz apart; inaudible. At strong phase feedback (MORP near 0) Plaits itself is
  chaotic: its partials change by up to 10 dB if the pitch moves by 0.05 cent.
- **NOISE, PARTCL:** spectra and levels within the variance of Plaits' own renders.
- **BDRUM, SNARE, HIHAT:** the deterministic parts (pulses, resonators, envelopes) match Plaits' waveform;
  with noise, spectrum and decay within Plaits' own variance.
- **GRAIN:** waveform and spectrum match Plaits'.
- 0.16's speed-ups changed no sample: its MACRO output is 0.15's, sample for sample.
- 0.18 (OUT only): 0.17 and 0.18 built for the PC from their sources, 2,400 random voices (every engine,
  knobs moving, pitch bends, re-trigs, drums dying away), F at 0 on 0.17: every sample equal.

## Cost

Estimated ColdFire cycles a voice a 32-frame block, as a share of the block (tests/emu_macro.py; the block
is 166,667 cycles at 250 MHz). "Played" is knobs at 64 with a trig every 16th note at 120 BPM; "heavy" is
HARM, TIMB and MORP at 127:

| engine | played | heavy knobs |
|---|---|---|
| WSHAPE | 4.4 % | 4.3 % |
| 2OP FM | 4.3 % (no feedback) | 10.0 % (feedback, 4x); 5.6 % under eco (2x) |
| NOISE | 4.9 % | 5.2 % |
| PARTCL | 5.4 % (7.8 % dense) | 9.7 %; 7.8 % under eco (four particles) |
| BDRUM | 6.0 % | 6.9 % |
| SNARE | 5.4 % | 7.6 % |
| HIHAT | 5.9 % | 5.8 % |
| GRAIN | 6.3 % | 4.9 % |

A MACRO track not playing costs nothing, and a drum costs nothing once it has died away. The stock render
uses about 80 % of each block, so roughly 20 % is left for all the mods; with digihealth's FAST AUDIO
about 28 %: three to five MACRO voices at once. If the voices ask for more than the block allows, Digi Mono
first has 2OP FM start its notes at 2x and PARTCL fire four of its six particles (digimono_eco, Digi
Mono 0.23 and Digi Macro 1.1), then rests the heaviest voice until there is room for it, rather than let
the unit lock up (mods/digimono/DESIGN.md, "The overload guard").

## Next engines

From Plaits' list: speech, virtual analog, the swarm, chords, wavetable, string and modal resonators. Each
needs code room as well as CPU: the mods' shared memory has about 12 KB left with every mod of this
repository in one build (0.18).
