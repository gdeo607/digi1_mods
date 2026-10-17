# digi1_mods

*(formerly DT1_8_POLY_OSC)*

Unofficial, community-made mods for the 8-track MK1 sampler groovebox ("DT1"), **OS 1.53 and OS 1.54**: synth machines, an
LFO modulation matrix, scope / spectrum / tuner pages, a master EQ and polyphony. Each is an
[elekloader](https://github.com/irpina/elekloader) mod: you tick the ones you want in elekloader and it builds
your OS file from your own official one.

> **Unofficial and unsupported. Not affiliated with, endorsed by or supported by the hardware's manufacturer.
> Flashing modified firmware is at your own risk.** Read [DISCLAIMER.md](DISCLAIMER.md) and [RISKS.md](RISKS.md) first.

## At a glance

- **Digi Poly**: chords on any audio track, from its own trigs, by borrowing other tracks' voices.
- **Digi Mono**: seven synth machines in the FUNC+SRC list (sine, noise, saw, pulse, an ensemble, a formant
  voice, and POLY SIN: three sines, a chord on one track), each with its own icon and its own SRC page,
  values in their units; and **CHORDS**: four oscillators as a chord, with chord type, inversion and
  spread on their own knobs.
- **Digi Macro**: **MACRO**, eight engines ported from Plaits (waveshaping, 2-op FM, noise, particles,
  kick, snare, hi-hat, grains), the engine on knob B, so p-locks can change it per step. A mod of its own
  (it needs Digi Mono, which plays its voices).
- **Digi Matrix**: any track's LFO to any parameter of any track, 8 slots, per pattern.
- **Digi Utilities**: waveform, spectrum and X-Y scope, a tuner and track activity, on a held "...".
- **Digi EQ**: a 4-band master EQ on every output, main outs, headphones and USB.
- **Tone+FX**: Digital Alchemy DSP's master effects suite, ported: ring modulator, wavefolder, tilt EQ,
  bit crusher, pitch and stereo modes and a PERFORM page of momentary pads, on every output.
- **Ready to use:** the `.elemod` files of every mod, the other authors' too, are in [elemods/](elemods/),
  built with the latest elekloader: add them in elekloader with your own official OS 1.53 or 1.54 file and
  build (the `-os1.54` files are for 1.54).

<table>
<tr><td align="center"><img src="docs/img/digimono_list.png" width="384" alt="FUNC+SRC list with the Digi Mono machines and their icons"><br><sub>Digi Mono: the machines, with their icons</sub></td>
<td align="center"><img src="docs/img/digimono_vo.png" width="384" alt="MONO VO SRC page"><br><sub>MONO VO: vowel AH, consonant K, 40 ms</sub></td></tr>
<tr><td align="center"><img src="docs/img/digimono_ens.png" width="384" alt="MONO ENS SRC page"><br><sub>MONO ENS: oscillator 2 at +5 semitones</sub></td>
<td align="center"><img src="docs/img/digimatrix_page.png" width="384" alt="MOD MATRIX page"><br><sub>Digi Matrix: two LFO routings</sub></td></tr>
<tr><td align="center"><img src="docs/img/scope.png" width="384" alt="scope page"><br><sub>Digi Utilities: the scope, tuner and track activity</sub></td>
<td align="center"><img src="docs/img/digieq_page.png" width="384" alt="Master EQ page"><br><sub>Digi EQ: four bands and the response curve</sub></td></tr>
</table>

## The mods

| mod | what it adds | version | hardware |
|---|---|---|---|
| [Digi Mono](#digi-mono) (`digimono`) | synth machines: MONO SIN, NOISE, SAW, PULSE, ENS, VO, POLY SIN and CHORDS | 0.24 | not yet tested (0.22 **confirmed**) |
| [Digi Macro](#digi-macro) (`digimacro`) | MACRO: Plaits' engines as a machine (needs Digi Mono) | 1.2 | not yet tested (1.0 **confirmed**: no freezes) |
| [Digi Matrix](#digi-matrix) (`digimatrix`) | an LFO modulation matrix, 8 cross-track slots | 1.0c | **confirmed** |
| [Digi Utilities](#digi-utilities) (`digiutils`) | waveform, spectrum and X-Y pages, tuner, track activity | 1.9a | **confirmed** |
| [Digi EQ](#digi-eq) (`digieq`) | a 4-band master EQ on every output | 1.0c | **confirmed** |
| [Digi Poly](#digi-poly) (`digipoly`) | POLY: chords from a track's own trigs, borrowing other tracks' voices | 2.1 | not yet tested (2.0a **confirmed**) |
| [Tone+FX](#tonefx) (`tonefx`) | Digital Alchemy DSP's master effects (ring, fold, tilt EQ, crush, pitch / stereo, PERFORM pads), ported; GPL-2.0 | 3.2a3 | not yet tested |

They all need core 2.1 or later (elekloader brings its newest, core 4.0, which gives the mods 256 KB of RAM
instead of 128 KB) and combine with each other and with the
[other mods kept up to date](#other-mods-kept-up-to-date) here (digihealth, SOPHIE, NEIGHBOR, DIGISLICER,
DigiFilter): every pair, checked by `tools/dev.sh elemods` (COMPATIBILITY.txt). With core 4.0 they fit in one
build (checked on OS 1.54), with Tone+FX or Digi EQ: Tone+FX replaces Digi EQ (the two use the same master
page and render site).

**The SRC machines** (Digi Mono, Digi Macro, Digi Poly, DIGISLICER 2.3, NEIGHBOR 0.7, OB-1) describe their pages
for elekloader's [machine-pages](#machine-pages-elekloaders) mod, so they share a build (checked: all of them
with Utilities, Matrix, Digi EQ, digihealth and DigiFilter, on OS 1.53 and 1.54). SOPHIE 1.1.13 still patches
those places itself, so it does not combine with them until its author publishes a machine-pages build.

**OS 1.54:** every mod here is built for both 1.53 and 1.54, and on 1.54 they pass the same emulator tests
(Digi Mono, Digi Poly, Digi Matrix, Digi EQ, Digi Utilities' pages). Of the other
mods, digihealth, NEIGHBOR and DIGISLICER have 1.54 versions; SOPHIE and DigiFilter do not yet, so
`tools/dev.sh` ports the copy it builds of them with `tools/port_os.py`, which did ours (docs/DEVELOPING.md).

**Getting them:** download them from [elemods/](elemods/) and add them in elekloader (it brings the core
mod). Or build them yourself: `tools/dev.sh mods` fetches elekloader and the other mods and builds every mod
as an `.elemod` from your own official file (`tools/dev.sh publish` refreshes elemods/); on macOS the elekloader app (`tools/macos/install_app.sh`) does it from
a double-click and checks for updates. Steps: [docs/DEVELOPING.md](docs/DEVELOPING.md). Full controls:
[docs/USAGE.md](docs/USAGE.md). Version history: [CHANGELOG.md](CHANGELOG.md).

<sub>Screens are the unit's 128 x 64 display, captured in an emulator running the mods and scaled 4x; the
pattern is renamed "DEMO". Only screenshots are published here - no firmware, and nothing derived from it.</sub>

## Digi Mono

**Digi Mono** (elekloader mod `digimono`, 0.24, needs core 3.0 or later and machine-pages) - seven synth machines after the Monomachine, and CHORDS
- **Seven new machines in the FUNC+SRC list**, each with its own icon: **MONO SIN**, **MONO NOISE** (sample and
  hold, red noise), **MONO SAW** (unison, two sub-oscillators), **MONO PULSE** (PWM, unison, subs), **MONO ENS**
  (four oscillators at set intervals, saw to pulse, chorus), **MONO VO** (a formant voice: vowel to vowel,
  consonants) and **POLY SIN** (three sines at set notes - a chord on one track - with a pitch envelope). They need no sample; the track's filter, amp, LFOs, sends and p-locks work on them as on a
  sample.
- **MACRO** is now its own mod, [Digi Macro](#digi-macro) (0.22). Digi Mono still plays its voices: it
  hosts machines of other mods (mods/digimono/digimono_ext.h), on its render stack and with its overload guard.
- **CHORDS** (0.19): a chord on one track. **B TYPE** picks one of 24 chords (MAJ, MIN, SUS2, SUS4, DIM,
  AUG, MAJ6, MIN6, MAJ7, MIN7, DOM7, MMAJ7, M7B5, DIM7, 7SUS4, ADD9, MADD9, MAJ9, MIN9, DOM9, QUART, FIFTH,
  OCT, UNI), **C INV** its inversion (ROOT, 1ST, 2ND, 3RD), **E SPRD** its spread (CLOSE, OPEN, WIDE,
  XWIDE: the voicing opened over up to three octaves). **F WAVE** is sines (0-15) or saw to pulse, and
  D / G / H are pulse width and chorus, as on MONO ENS. All three chord knobs can be p-locked per step.
- **Their own SRC page**: knobs B-H are the machine's parameters, named and shown in their own units
  (semitones, %, ms, vowels, consonants). A stays TUNE; D is a parameter too (no sample list), and the
  volume is the track's LEVEL and AMP page.
- **Tick it in elekloader** and machine-pages is ticked with it, which draws its pages (0.24; up to 0.23,
  digichain did). It combines with every other mod here, Digi Poly included, and with DIGISLICER, NEIGHBOR
  and OB-1.
- **Under load, lighter sound before any gap** (0.23): when the render runs short of time, the machines take
  their cheaper paths (ENS and CHORDS fade their chorus out, MACRO's 2OP FM plays new notes at 2x, PARTCL
  fires four of its six particles) until it has been calm for 2 s. Only if that is not enough does one
  voice rest, the heaviest, until there is room for it; every voice rests (a short dropout) only when the
  load is not Digi Mono's. Never a lock-up (0.17).
- **Light enough for a few tracks at once**: 0.16 made SAW and PULSE about three times lighter at full unison, ENS
  and POLY SIN 12-28 % and the MACRO engines 5-25 %; 0.10 and 0.11 made VO 16-19 % and ENS 20-29 % lighter (ENS starts with its chorus
  off, CHRL 0: the chorus is its costliest part). On a unit
  the stock render already takes about 80 % of each block, so keep to a few playing Digi Mono tracks
  (VO, ENS and PULSE cost the most) and check with digihealth's SYSTEM INFO; [docs/USAGE.md](docs/USAGE.md)
  has the knobs and a load test.
- A clean-room engine: no Monomachine code or data. Checked in emulation on the real firmware, bit for bit
  (tests/digiemu_mono.py); **not yet on a unit**. Details: [mods/digimono/DESIGN.md](mods/digimono/DESIGN.md).

<table><tr><td align="center"><img src="docs/img/digimono_vo.png" width="384" alt="MONO VO SRC page"><br><sub>A MONO VO track's SRC page: each knob shows its value in place, the top bar its full name</sub></td><td align="center"><img src="docs/img/digimono_ens.png" width="384" alt="MONO ENS SRC page"><br><sub>MONO ENS: PCH2 at +5 semitones, CHRL at 5</sub></td></tr></table>


## Digi Macro

**Digi Macro** (elekloader mod `digimacro`, 1.1, needs Digi Mono 0.23, which elekloader ticks with it) - MACRO
- **MACRO** in the FUNC+SRC list (after POLY SIN): engines from **Plaits** (Emilie Gillet, MIT licence),
  ported to integer arithmetic: WSHAPE, 2OP FM, NOISE, PARTCL, BDRUM, SNARE, HIHAT and GRAIN. Knob B picks
  the engine in zones of 8 (p-lockable: it switches on the trig), C / D / E are HARMONICS / TIMBRE /
  MORPH; each engine plays Plaits' main output. Each engine is checked against Plaits' own code:
  [mods/digimacro/MACRO.md](mods/digimacro/MACRO.md).
- **Why a mod of its own:** a build without MACRO saves its 32 KB of code. MACRO was part of Digi Mono
  up to 0.21; its sounds keep their machine (27) and knobs.

## Digi Matrix

**Digi Matrix** (elekloader mod `digimatrix`, 1.0c) - a modulation matrix for the LFOs
- **SETTINGS > MOD MATRIX** opens a page with **8 routing slots**. Each one sends **any track's LFO1 or LFO2**
  to **any parameter of any track**, with **its own depth** (-64..+64) - so one LFO can drive several
  parameters across several tracks, each by a different amount, while its own DEP keeps controlling only its
  own track.
- Per slot, **OWN** says whether that LFO still modulates its own track's DEST as well, or only what the
  matrix routes it to.
- UP/DOWN choose a slot, **YES** turns it on and off, **NO** leaves; knobs A-F edit the slot under the cursor
  (source track, source LFO, destination track, destination parameter, depth, OWN).
- The matrix lives in the pattern's kit, so **each pattern has its own**; it is saved with the project and
  (since 2.0d) survives a power cycle.

<table><tr><td align="center"><img src="docs/img/digimatrix_page.png" width="384" alt="MOD MATRIX page"><br><sub>SETTINGS &gt; MOD MATRIX: two slots routed, the second under the cursor</sub></td></tr></table>

## Digi Utilities

**Digi Utilities** (elekloader mod `digiutils`, 1.9a) - the utility page, on a **held "..."** (three dots) key:
waveform -> spectrum -> X-Y; Song mode is kept.

![scope](docs/img/scope.png)

- *Scope*: live waveform. *Spectrum*: 30 Hz..20 kHz analyser (128 log columns, 60 dB, falling peaks;
  bass from a 170 ms window, highs from a 21 ms window).
- Both pages:
  - main view -> X-Y (stereo goniometer) -> close, with the same key. **YES** = fullscreen, **NO** = close.
  - bottom-right: activity boxes for all 8 audio tracks (flash on trig; POLY voices stay lit while held).
  - bottom-left: **tuner** (note + cents, ~12 Hz..3 kHz).
  - the top bar (pattern, name, tempo), mutes, pattern/bank change, page keys and **knobs** all keep working
    while the page is open, so you can tweak a sound and watch it change.

## Digi EQ

**Digi EQ** (elekloader mod `digieq`, 1.0c) - a 4-band master EQ, on **every output**
- A master page (FUNC+LFO, after Compressor): 4 bands, each with level/Q (knobs A-D, press to switch) and
  frequency/type (knobs E-H, press to switch: HP, low shelf, bell, notch, band pass, high shelf, LP), with the
  response curve drawn above the knobs.
- It runs on the **master mix**, before the render hands it to the analog outputs and to the USB stream, so
  main outs, headphones and USB audio all carry it - as the compressor does. (Up to 1.8a it sat on the
  codec's transmit buffer, which is the analog path only, so USB audio came out unequalized.)
- **Per pattern**: the settings live in the pattern's kit, so every pattern has its own EQ, saved with the
  project and kept over a power-off (1.0c: before, unsaved EQ changes were lost at power-off).
- **SETTINGS > GLOBAL FX/MIX > MASTER EQ**, beside the firmware's own entries: with it on, the EQ you can hear
  overrides every pattern's own; with it off, each pattern goes back to its own.

<table><tr><td align="center"><img src="docs/img/digieq_page.png" width="384" alt="Master EQ page"><br><sub>The master EQ page: four bands and the response curve</sub></td><td align="center"><img src="docs/img/digieq_global.png" width="384" alt="GLOBAL FX/MIX with MASTER EQ"><br><sub>SETTINGS &gt; GLOBAL FX/MIX: the MASTER EQ entry, on</sub></td></tr></table>

## Digi Poly

**Digi Poly** (elekloader mod `digipoly`, 2.1, needs core 3.0 or later and machine-pages) - the POLY machine, redesigned.
2.0 runs on core 2.1, so it shares a build with every other mod here, Digi Mono included. POLY is now machine
6 (it was 4, which core 2.1 gave NEIGHBOR): a POLY track saved with 1.0f loads as NEIGHBOR or ONESHOT, so
choose POLY on it again.
- Any audio track set to POLY plays **chords from its own trigs**: its TRIG page becomes the MIDI tracks' page
  (NOT1-NOT4 piano roll, VEL, LEN, PROB, LFO.T, with the track's LEV fader); SRC, FLTR, AMP and LFO are as usual.
- Each extra note **borrows the voice of another track** (the one idle longest; never a muted track, a track
  that trigs at the same moment, or a track you took out of the pool). No MIDI track or loopback is needed.
- **Pressing the track's key plays the chord**; notes on the POLY track's **own MIDI channel** play
  polyphonically, one voice per note, and **recording them writes the chord into the piano roll** - up to
  four notes into the step's NOT1-NOT4, where the stock OS would record a bare trig with no note.
- On the POLY track's TRIG page the **LEVEL knob sets the track level** and the **LEV fader** beside NOT1
  shows it, the same size and in the same place as on the audio pages.
- The POLY track's **level and knobs apply to the whole chord**, borrowed voices included.
- **SETTINGS > POLY** chooses which tracks lend their voice. It lives in the pattern's kit, so each pattern has
  its own voice allocation and it is saved with the project.
- Several tracks can be POLY. POLY survives kit/project reload, and since 2.0d the pool survives a power cycle.

<table><tr><td align="center"><img src="docs/img/digipoly_list.png" width="384" alt="FUNC+SRC list with POLY"><br><sub>FUNC+SRC: POLY after SLICE, with its icon, then Digi Mono</sub></td><td align="center"><img src="docs/img/digipoly_trig.png" width="384" alt="POLY track TRIG page"><br><sub>A POLY track's TRIG page: NOT1-NOT4 piano roll and the LEV fader</sub></td></tr><tr><td align="center"><img src="docs/img/digipoly_pool.png" width="384" alt="SETTINGS > POLY"><br><sub>SETTINGS &gt; POLY: track 2 taken out of this pattern's voice pool</sub></td></tr></table>

**POLY in the stand-alone builds** (and the earlier `dt8poly` mod, built only on request) - a 5th sample machine
in the FUNC+SRC list
- Tracks set to POLY share one voice pool; the lowest POLY track is the *control* track (its sound is used).
- Sequencer trigs on the control track rotate across the POLY tracks.
- *MIDI cable*: notes and chords from a MIDI track (e.g. recorded from an external sequencer) play the POLY
  tracks whose MIDI receive channel equals the MIDI track's output channel - free voice first, else oldest.
- CCs from that MIDI track reach the POLY tracks (filter/amp sequencing); SRC TUNE and LFOs stay per voice.
- POLY survives kit/project reload.

## machine-pages (elekloader's)

Digi Mono, Digi Macro and Digi Poly describe their SRC and LFO pages for **machine-pages**, elekloader's mod
that owns the page's firmware places once and draws each added machine's page from its description (core
3.0 and later), as DIGISLICER, NEIGHBOR, OB-1 and DT-FM do. So they all share a build. elekloader ticks it
with them. (Up to Digi Mono 0.23 and Digi Poly 2.0a this repository's own **digichain** did that job for
them; it is retired.)

## Tone+FX

**Tone+FX** (elekloader mod `tonefx`, 3.2a3) - Digital Alchemy DSP's Tone+FX 3.2a, ported (GPL-2.0-or-later)
- Master effects on **every output** (main, headphones, USB audio): ring modulator, TILT EQ, CRUSH (bits,
  sample rate, crunch), a wavefolder, PITCH (WOB, SHFT) and STEREO (FLAN, PAN, WIDE) modes, an output meter
  and a soft ceiling.
- Seven pages after the stock master pages (FUNC+LFO, then LEFT/RIGHT): DIGI RING, FOLD, TILT, CRUSH, PITCH,
  STEREO, and PERFORM (UP), whose 16 trigs are momentary pads. MIDI CC controls every parameter.
- Settings follow the pattern while the unit is on; they are not saved over a power cycle.
- **Not with Digi EQ** (the same master page and render site). 36.4 KB of the mods' RAM (256 KB with core 4.0).
- No boot splash since 3.2a3. Details and what the port changed: [mods/tonefx/README.md](mods/tonefx/README.md).

## Other mods kept up to date

`tools/dev.sh mods` (and the elekloader app's update button) also fetches and builds these, by other authors,
from their own repositories, so one update brings everything: **digihealth** (FAST AUDIO, SYSTEM INFO CPU/DSP
readout), **SOPHIE** (digisophie), **NEIGHBOR** (digineighbor), **DIGISLICER** (digislicer) and **DigiFilter**.
SOPHIE is built with its render made 7-12 % lighter, the same sound sample for sample
(tools/patches/digisophie-render.patch, offered to its author; the build's version ends in "f").
`COMPATIBILITY.txt`, written next to the `.elemod` files, says which pairs combine.

## Stand-alone builds

Before elekloader, this project was a patch set built straight onto the official file, POLY with one utility
page per build:

| page on the "..." key | release | unit shows | hardware |
|---|---|---|---|
| Scope (waveform) | v3p | 1.5b | not yet tested |
| Spectrum | v3q-spectrum | 1.5c | not yet tested |
| All three views (waveform, spectrum, X-Y) | v3r-all | 1.5d | **confirmed** |

**Song mode** is disabled in the stand-alone builds (its code space is reused); chains still work. Digi
Utilities keeps Song mode: the page opens on a held "..." instead.

**Build it yourself** from your own copy of the official OS 1.53 file (recommended; identical result):

```sh
# get the MIT-licensed .syx container tool by mischa85 (GitHub) and build it with `make`, then:
python3 tools/build.py --official <official OS 1.53 .syx> --tool <path to the container tool> --page scope
# -> out/digi1_mods_v3p_1.5b.syx            SHA-256 f8cd0d721c265a07077078a5aae95908c265ce3fadd0c83cbbb377916b51b8b3
python3 tools/build.py --official <official OS 1.53 .syx> --tool <path to the container tool> --page spectrum
# -> out/digi1_mods_v3q-spectrum_1.5c.syx   SHA-256 3aeb2bb8c4a79c7a807d4ee62336ba8e045078d6ae3d122d64e53a0c8851564a
```

The build refuses any input that is not the exact official OS 1.53 file and checks every patched byte.
Flashing and **reverting**: [docs/INSTALL.md](docs/INSTALL.md).

## Developing

`tools/dev.sh` runs the whole loop on your own computer:

- **setup** (once): elekloader, the digiemu emulator and the pinned helper mods;
- **test**: the synth engine;
- **build**: your OS file from your own official OS;
- **emu, emutest**: boot the build in digiemu and check every Digi Mono machine there, bit for bit;
- **play**: open digiemu's window to play the build yourself.

Steps, per operating system: [docs/DEVELOPING.md](docs/DEVELOPING.md).

## Building as elekloader mods (by hand)

`tools/dev.sh mods` is the easy path. By hand:

```sh
python3 tools/build_elemods.py --stock <official OS 1.53 .syx> --elekloader <elekloader checkout>
# -> out/elk/{digipoly,digiutils,digimatrix,digieq}/out/*.elemod
```

Then add them in elekloader's window (with its core mod), or on the command line:
`python -m elekloader.patch --stock <official .syx> --mod core-4.0.elemod --mod machine-pages-1.1.elemod --mod digipoly-2.1.elemod --mod digimatrix-1.0c.elemod --mod digieq-1.0c.elemod --out custom.syx --version 2.0e`.
Add `--mod digiutils-1.9a.elemod` for the "..." utility pages.

Building needs an m68k cross toolchain (binutils and gcc), which the SDK looks up as `m68k-linux-gnu-*`:

| platform | install |
|---|---|
| Linux (Debian/Ubuntu) | `sudo apt install binutils-m68k-linux-gnu gcc-m68k-linux-gnu` |
| macOS (Homebrew) | `brew install m68k-elf-gcc m68k-elf-binutils` |

`tools/build_elemods.py` falls back to Homebrew's `m68k-elf-` prefix on its own when `m68k-linux-gnu-gcc` is absent.
To use another toolchain, set `ELEKLOADER_CROSS` to its prefix (for example `ELEKLOADER_CROSS=m68k-elf-`) for the build.

Every mod's `.elemod` file is also in [elemods/](elemods/) (`tools/dev.sh publish`): they hold the mods' code, and refer to your own official file for anything from the firmware. The other authors' are built from their repositories (elemods/README.md names the commits; their licenses are in elemods/licenses/).

The master EQ is in the elekloader build only; since 1.0a it is its own mod, `digieq`
([docs/USAGE.md](docs/USAGE.md)).

Checked in emulation (hardware: **not yet** for Digi Poly): the linked Digi utilities / dt8poly code is
instruction-for-instruction the stand-alone code (`tests/elk_equiv.py`); the mods lint and link alone, together,
and with digihealth + digislicer; a full walk-through boots in the digiemu emulator with FAST AUDIO on
(`tests/digiemu_scenario.py`); Digi Poly is checked twice: `tests/emu_poly.py` runs its own code in unicorn
without booting the firmware (voice choice, the settings row, the chord's messages, the knob and level mirroring,
the TRIG page's fader - seconds), and `tests/digiemu_poly.py` boots the firmware for what only it can show (the
page, the key's chord, chords while the sequencer runs, per-pattern pools). Digi Matrix is checked the same
way: `tests/emu_matrix.py` runs both engine passes, the clamps and the whole page in unicorn in seconds, and
`tests/digiemu_matrix.py` boots the firmware to show the SETTINGS row, the page, what its keys and knobs write
into the pattern's kit, and one track's LFO actually moving another track's parameter. Digi EQ likewise:
`tests/emu_eq.py` checks the knobs against the design model, the audio bit for bit against it, and the
settings in the kit and the global override; `tests/digiemu_eq.py` boots the firmware and measures a test
tone put into the master mix again in the bus the USB stream is built from, against what the model says the
settings should do. Differences from the stand-alone
build: the code runs from the loader's RAM area instead of reclaimed song-mode code, and the audio tap sits one
instruction later because FAST AUDIO owns the output-write call (see docs/TECHNICAL_NOTES.md).

## Repository layout

| path | contents |
|---|---|
| `src/` | assembly for every hook (ColdFire, GNU as `-mcpu=5475`) |
| `bin/` | the assembled hooks (committed; `make` rebuilds them) |
| `tools/build.py` | official .syx -> patched .syx, fully verified |
| `tools/patch_section3.py` | applies all patches to the MAIN OS section |
| `elemods/` | every mod as a ready-made `.elemod` file, for elekloader, with the other authors' licenses |
| `mods/`, `tools/build_elemods.py` | the elekloader mods (mod.json + mod-only sources; shared code from `src/`) |
| `tests/` | emulator tests: the real firmware code runs under unicorn with the patches (`tests/run_tests.sh <official s3> <patched s3> scope|spectrum`) |
| `docs/` | install/revert, usage, technical notes (reverse-engineering log) |

No firmware from the manufacturer is stored or distributed here; see [DISCLAIMER.md](DISCLAIMER.md).

## License

Our own code and docs: MIT ([LICENSE](LICENSE)). This does **not** cover the manufacturer's firmware.
mods/tonefx is a port of Digital Alchemy DSP's Tone+FX and stays under its GPL-2.0-or-later
([mods/tonefx/LICENSE](mods/tonefx/LICENSE)).
