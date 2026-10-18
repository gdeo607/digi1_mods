# DH-101

An SH-101-style oscillator section for the DT1 (OS 1.53 and 1.54), as an
[elekloader](https://github.com/irpina/elekloader) mod (id `digi101`). It adds **DH-101** to the FUNC+SRC machine list,
as one of [Digi Mono](../digimono)'s hosted machines (like Digi Macro's MACRO). Needs Digi Mono 0.24 or later and
elekloader's machine-pages 1.2 or later, which draws its two SRC pages and keeps page 2's parameters
(elekloader ticks both with it).

**0.25:** page 2 is a real SRC page of the firmware's, through machine-pages 1.2: every knob of both pages
takes **p-locks**, **both LFOs** reach every one of them, and they are saved with the sound. 0.23's own page,
its packed storage and its LFO rerouting are gone, and DH-101 patches no firmware place of its own. A sound
has room for seven new parameters, so page 2's knob E is the track's own **LFO 1 depth** where 0.23 had MOD:
aim LFO 1 at 101:Filter Freq for the 101's LFO on the cutoff (lockable, any waveform). Sounds saved by 0.23
load with page 2 at its defaults (their filter settings are not carried over).
By Robbie (designed and tested on a unit), MIT, as the rest of this repository.
The track's own FILTER, AMP, LFO and effects follow it, as they follow a sample.

> Unofficial and unsupported. Flashing modified firmware is at your own risk; the stock OS always recovers
> the unit (hold FUNC at power-on, TRIG 4, send the stock .syx).

## Controls (SRC page)

| knob | name | what it does |
|---|---|---|
| A | TUNE | the track's tune, as always |
| B | RNGE | range: **16' 8' 4' 2'** (8' plays the note as written) |
| C | SAW | saw level |
| E | PULS | pulse level |
| F | PW | pulse width: 50 % (square) at 0, narrowing to 5 %; in LFO / ENV mode, the modulation depth |
| G | PWM | PWM source: **MAN** (0-9), **LFO 1-59** (rate 0.1-30 Hz), **ENV 1-59** (decay 10 ms - 4 s; starts at each trig, narrow, back to square) |
| H | SUB | sub-oscillator: three zones pick the mode, the level rises across each: **-1** (1 oct down, square), **-2** (2 oct down, square), **-2P** (2 oct down, 25 % pulse) |
| D | NOIS | white noise level |

## SRC page 2 (press SRC again)

On a DH-101 track's SRC page, press **SRC** to flip between page 1 and page 2 (the screen's title says
which: DH-101 1/2, 2/2). Page 2 is drawn by machine-pages as any SRC page: the firmware's knobs, pop-ups
("Filter Freq=100"), p-locks (hold a trig, turn a knob), copy / paste / clear and randomize.

| knob | name | what it does |
|---|---|---|
| A | PORT | portamento: OFF, or 1..127 (2 ms .. 3 s) |
| B | FREQ | the 101 filter's cutoff (127 = open, the filter out of the way) |
| C | RESO | resonance; the top of its range self-oscillates |
| D | ENV | how far the track's AMP envelope opens the filter (up to 7 octaves) |
| E | DEP | the track's LFO 1 depth (the LFO page's own knob, shown here): LFO 1 aimed at 101:Filter Freq is the 101's MOD |
| F | KYB | the cutoff follows the note: OFF .. 100 % (100 %: a self-oscillating filter plays in tune) |
| G | VIB | the 101 LFO on the pitch (vibrato) |
| H | RATE | the 101 LFO's rate, 0.1 .. 30 Hz |

**LFO destinations.** On a DH-101 track the LFO page's DEST list names every knob of both pages, in the
group 101: Tune, Range, Saw Level, Noise Level, Pulse Level, Pulse Width, PWM Source, Sub Osc, then
Portamento, Filter Freq, Filter Reso, Env Depth, Keyboard Follow, Vibrato, LFO Rate (at the end of the list,
after the AMP's). DEST holds the parameter, saved as any other.

How it works: page 2's seven own knobs are machine-pages' parameters 8..14 (`struct cm_ui32`, `extra[7]` and
`pages[2]` in `digi101.c`), which machine-pages gives seven parameter ids of their own (164..170) and keeps
in the sound's free slots 46..52; the firmware's own lock, LFO and save code then handles them. Knob E names
a stock parameter (LFO 1's DEP, id 59, `CM_SRC_STOCK`), which is simply the firmware's knob shown on this page
too. In the render DH-101 reads each value as played (locks, smoothing and both LFOs applied) with
`cm_src_value(track, 8..14)`.


The filter is a 4-pole ladder after the 101's VCF (clean-room, calibrated by ear and measurement against
TAL-BassLine-101): it comes before the track's own FLTR page, which can filter it further. Page 2's
settings are part of the sound, so they go with the sound into kits, the project and the sound pool.

The glide is the 101's: an exponential slide in pitch, the same up and down; only note changes glide.
PORT is a parameter of the sound (0.23 kept it per pattern).

Saw, pulse and sub all come from one oscillator, so they are always in step, as on the original. Levels
whose peaks would add up past full scale are scaled back together, so the mix never clips.

**Using the rest of the unit as the 101's other half**

- **Filter cutoff / resonance / mod**: SRC page 2 (above), lockable; the FLTR page after it as well.
- **Envelope**: the track's AMP envelope, which ENV follows (one envelope does both, as on the 101).
- **Portamento**: PORT on SRC page 2 (above), lockable per step.
- **Bender / mod lever**: not built in. Map a controller's lever to TUNE by MIDI CC; the LFOs reach the
  destinations above.

**CPU.** While a track's AMP envelope is idle and its level under -72 dB (Digi Mono's own line for a
stopped voice), its DH-101 voice only keeps time (glide, LFOs, phase) and is not rendered; a trig renders
at least 5 ms. A note from silence fades in over 64 samples (1.3 ms); a note over a sounding one keeps
the oscillator running, as the 101's VCO does. On a unit (OS 1.54, FAST AUDIO on), three DH-101 tracks
with the filter running play without gaps.

**Messages.** 0.25 leaves TRK + a trig's message to the firmware and machine-pages (0.23 printed "DH-101").

## Files

| path | what |
|---|---|
| `mods/digi101/sh101.c`, `sh101.h` | the engine (plain C, 32-bit integer only); `sh101_tables.h` is generated |
| `mods/digi101/vcf.s` | the filter's per-sample loop in ColdFire assembly (EMAC), held to the C reference |
| `mods/digi101/digi101.c` | the machine for Digi Mono, its two SRC pages for machine-pages, page 2 read as played |
| `mods/digi101/digimono_ext.h` | Digi Mono's machine interface (a copy, as Digi Macro carries; `tools/build_elemods.py` checks they match) |
| `mods/digi101/mod.json` | the manifest (OS 1.53, with the OS 1.54 port): no firmware sites |
| `tests/digi101_page.c`, `digi101_host_fw.h` | the two pages as machine-pages checks them, page 2 reaching the engine, value texts, rest / fade, on the desktop |
| `tests/digi101_vcf.py`, `digi101_vcf_stub.s` | vcf.s against sh101.c in Unicorn's ColdFire V4e, within 1 LSB |
| `tests/digi101_fw.py` | linked into the real OS 1.53 / 1.54 with machine-pages and run in Unicorn: its pages accepted, the LFO page's names for both pages |
| `tests/digi101_engine.py` | the engine's behaviour through the desktop renderer |
| `tools/gen_digi101_tables.py`, `digi101_render.c` | the tables; a desktop renderer (the engine to a WAV, for `tests/digi101_engine.py`) |

## Build and test

```sh
python3 tools/gen_digi101_tables.py                       # mods/digi101/sh101_tables.h
ELK=out/dev/tools/elekloader                              # tools/dev.sh setup fetches it (machine-pages 1.2)
gcc -O1 -DHOST_TEST -I$ELK/elekloader/sdk/include -o out/digi101_page tests/digi101_page.c \
    mods/digi101/digi101.c mods/digi101/sh101.c -lm && out/digi101_page
gcc -O2 -o tools/digi101_render tools/digi101_render.c mods/digi101/sh101.c -lm && python3 tests/digi101_engine.py
# the OS file: core and machine-pages built from that checkout, with Digi Mono and DH-101 (elekloader.patch)
# Unicorn ones: digiemu's python (tools/dev.sh setup), STOCK = your official OS 1.53 or 1.54 file
python3 tests/digi101_vcf.py
python3 tests/digi101_fw.py $STOCK core.elemod machine-pages.elemod digimono.elemod digi101.elemod
```

Credits: designed and tested by Robbie; engine and firmware work written with Claude (Anthropic). The
band-limiting method (polyBLEP "ramps") and the per-pattern storage follow Digi Mono's and digi1_mods' by
gdeo607 (MIT). The filter was calibrated by ear and measurement against recordings of TAL-BassLine-101 (no
code from it); its design follows Vadim Zavalishin's and Andrew Simper's public papers on zero-delay-feedback
filters. Clean-room: no firmware, Roland or plug-in code.

**Trademarks.** Roland and SH-101 are trademarks of Roland Corporation; TAL-BassLine-101 is a product of TAL Software GmbH. DH-101 is not affiliated with or
endorsed by any of them: the names say what it was modelled on. No code, samples or data from them is
included (TAL-BassLine-101 was only listened to and measured, from recordings that are not part of this).
