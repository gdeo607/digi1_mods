# MACRO: Plaits' engines as one Digi Mono machine

Status: **0.16: eight engines (WSHAPE, 2OP FM, NOISE, PARTCL, BDRUM, SNARE, HIHAT, GRAIN), checked in
emulation; not yet run on a unit.** More engines are added one at a time with the same workflow (below).

MACRO is one more machine in Digi Mono's FUNC+SRC list (machine 27). Its sound comes from engines of
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
| F | AUX | 0-55 plays Plaits' OUT, 72-127 its AUX output (a second, related sound), 56-71 crossfades between them. Only in 56-71 are both computed (about twice the CPU) |

The track's FLTR, AMP, LFO pages, p-locks and the sends work on MACRO as on any machine. The LFO page names
the knobs as above ("MACR:Harmonics"). A switch to MACRO sets ENGN to the first engine, HARM / TIMB /
MORP to 64 and AUX to 0.

ENGN zones, 8 values each, so a saved p-lock keeps its engine when engines are added:

| B | 0-7 | 8-15 | 16-23 | 24-31 | 32-39 | 40-47 | 48-55 | 56-63 | 64-127 |
|---|---|---|---|---|---|---|---|---|---|
| engine | WSHAPE | 2OP FM | NOISE | PARTCL | BDRUM | SNARE | HIHAT | GRAIN | GRAIN (for now) |

## The engines

| engine | Plaits model | HARM | TIMB | MORP | AUX output |
|---|---|---|---|---|---|
| WSHAPE | waveshaping | waveshaper curve | wavefolder amount | waveform asymmetry | a sine folded towards a second fold curve |
| 2OP FM | two-operator FM | frequency ratio (quantized) | modulation index | feedback: phase feedback below 64, self-modulation above | a sub-oscillator one octave down, modulated |
| NOISE | filtered noise | filter response, low-pass to band-pass to high-pass (OUT); the second band's distance (AUX) | clock rate of the noise | resonance | two band-passes, at the note and at HARM's distance from it |
| PARTCL | particle noise | frequency spread of the resonators | density of the impulses | resonance | the raw impulses |
| BDRUM | analog bass drum (OUT) / synthetic bass drum (AUX) | attack FM, self FM and drive | tone | decay | the synthetic kick |
| SNARE | analog snare (OUT) / synthetic snare (AUX) | snappy (noise against body) | tone | decay | the synthetic snare |
| HIHAT | 808-style metallic hat (OUT) / ring-modulated hat (AUX) | noisiness | tone | decay | the ring-modulated hat |
| GRAIN | grainlets (OUT) / Z oscillator (AUX) | formant ratio and carrier bleed (OUT); discontinuity shape (AUX) | formant frequency | carrier shape | the Z oscillator |

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
`noise_engine.cc`: two clocked noises (sample-and-hold with band-limited steps) at TIMB's rate; OUT runs
the first through a state-variable filter at the note, its response swept by HARM, its resonance by MORP;
AUX runs both through band-passes. The filters run on the CPU's multiply-accumulate unit in 24-bit
fractional arithmetic, rounded, with coefficients kept consistent so high resonance stays stable.

### PARTCL
`particle_engine.cc`: random impulses (density from TIMB) into six resonant band-passes, each retuned at
random within HARM's spread at its next impulse, then a low-pass at the note and Plaits' limiter.

### BDRUM, SNARE, HIHAT
`analog_bass_drum.h`, `synthetic_bass_drum.h`, `analog_snare_drum.h`, `synthetic_snare_drum.h`,
`hi_hat.h`: the trigger pulses, resonators, noise and VCAs as in Plaits. Two reductions keep them light:
the kick's resonator retunes every 8 samples once the attack is over (at every other during the attack
FM), and the hi-hat's filter coefficients are worked out again only when TIMB moves.

### GRAIN
`grain_engine.cc`, `grainlet_oscillator.h`, `z_oscillator.h`: two grainlets (a sine formant hard-synced to
a shaped carrier), the second HARM away (-2..+2 octaves), summed and DC-blocked; AUX is the Z oscillator.

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
- **GRAIN:** waveform and spectrum match Plaits' for OUT and AUX.
- 0.16's speed-ups changed no sample: its MACRO output is 0.15's, sample for sample.

## Cost

Estimated ColdFire cycles a voice a 32-frame block, as a share of the block (tests/emu_macro.py; the block
is 166,667 cycles at 250 MHz). "Played" is knobs at 64 with a trig every 16th note at 120 BPM:

| engine | played, F at 0 | AUX only (F 127), heavy knobs | crossfade (F 56-71) |
|---|---|---|---|
| WSHAPE | 4.4 % | 6.3 % | 9.0 % |
| 2OP FM | 4.3 % (no feedback); 10.8 % with feedback | 15.5 % | 17.1 % |
| NOISE | 4.9 % | 6.3 % | 12.0 % |
| PARTCL | 5.4 % (7.8 % dense) | 6.7 % | 11.7 % |
| BDRUM | 6.0 % | 10.7 % | 18.7 % |
| SNARE | 5.4 % | 9.8 % | 18.6 % |
| HIHAT | 5.9 % | 8.1 % | 14.9 % |
| GRAIN | 6.3 % | 4.1 % | 10.0 % |

A MACRO track not playing costs nothing, and a drum costs nothing once it has died away. The stock render
uses about 80 % of each block, so roughly 20 % is left for all the mods; with digihealth's FAST AUDIO
about 28 %: three to five MACRO voices at once, keeping F out of 56-71 on tracks that play together.

## Next engines

From Plaits' list: speech, virtual analog, the swarm, chords, wavetable, string and modal resonators. Each
needs code room as well as CPU: the mods' shared memory has about 3 KB left with every mod of this
repository in one build.
