# DH-101

An SH-101-style oscillator section for the DT1 (OS 1.54; the OS 1.53 build is untested), as an
[elekloader](https://github.com/irpina/elekloader) mod (id `digi101`). It adds **DH-101** to the FUNC+SRC machine list,
as one of [Digi Mono](../digimono)'s hosted machines (like Digi Macro's MACRO). Needs Digi Mono 0.24 or later and
elekloader's machine-pages (core 3.0), which draws its SRC and LFO pages; elekloader ticks both with it.
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

On a DH-101 track's SRC page, press **SRC** again for the DH-101 page (in place of the sample waveform).
**SRC** again, **NO** or any page key goes back. Any screen or message that opens over it (Imp/Exp,
Save Project, Direct, Tap Tempo, copy and paste) takes the screen, keys and knobs until it closes.

| knob | name | what it does |
|---|---|---|
| A | PORT | portamento: OFF, or 1..127 (2 ms .. 3 s). Per track, saved with the pattern, kept when a sound is loaded |
| B | FREQ | the 101 filter's cutoff (127 = open, the filter out of the way) |
| C | RESO | resonance, 32 steps; the top steps self-oscillate |
| D | ENV | how far the track's AMP envelope opens the filter (up to 7 octaves) |
| E | MOD | the 101 LFO on the cutoff |
| F | KYB | the cutoff follows the note: OFF, 33 %, 67 %, 100 % (100 %: a self-oscillating filter plays in tune) |
| G | VIB | the 101 LFO on the pitch (vibrato) |
| H | RATE | the 101 LFO's rate, 0.1 .. 30 Hz (16 steps) |

The page draws the firmware's own knob graphics (OS 1.54) where the SRC page has its knobs; the top bar and
the SMP / LEV column stay the firmware's (its messages, octave and the rest, show as usual). A knob
turned shows its full name and value in the firmware's own message ("Filter Frequency=100"), and the
names use the SRC page's own font.

**LFO destinations (OS 1.54).** On a DH-101 track the LFO page's DEST list shows the SRC knobs, with
three of them standing for something else, and in this order: 101:Tune, Saw Level, Noise Level, Pulse
Level, **PWM Source**, Sub Osc, then **Filter Freq** and **Filter Reso**, just above the FILTER page's
own. Range and Pulse Width are not destinations, nor are PORT, ENV, MOD, KYB, VIB and RATE.
- *Filter Freq* is the RNGE knob's slot (18), *Filter Reso* the PWM knob's (23) and *PWM Source* the PW
  knob's (22), so DEST holds a stock slot and saves as one. The list is built slot by slot
  (0x40011348); asked from that call alone (`digi101_slot2id`), slots 17..24 answer their parameter ids
  in the list's order. Every other caller sees the stock answers, so the DEST box, the knob's steps
  (which find the current id in the same list) and the save all work as for any destination.
- The firmware's LFO stage adds the LFO into those slots' smoothed words, which Digi Mono hands the engine
  as RNGE / PW / PWM. `lfo_route` (digi101.c) takes it back out (the word less the LFO, the stage's own
  formula; the kit's value where the stage clamped at 0 or 127) and sends it to page 2's FREQ / RESO, or to
  the PWM knob within 0..68 (MAN and LFO; full depth sweeps that span; a knob set to ENV is left alone).
- The names are the machine's page's (`lfo` and `lfo_long` of knobs B, F and G, digi101.c), which
  machine-pages shows on the LFO page (0.22 named them through Digi Mono 0.23.1's `digimono_dests`, which
  Digi Mono 0.24 does not have: machine-pages names every machine's destinations from its page).

The filter is a 4-pole ladder after the 101's VCF (clean-room, calibrated by ear and measurement against
TAL-BassLine-101): it comes before the track's own FLTR page, which can filter it further. Its seven
settings are part of the sound (parameter slots 46 and 47, which `glue.s` makes the firmware save and
load), so they go with the sound into kits, the project and the sound pool. They cannot be p-locked (the LFOs can reach FREQ and RESO: above).

The glide is the 101's: an exponential slide in pitch, the same up and down; only note changes glide.
PORT is kept in free bytes of the mods' persistent kit map (digi1_mods `src/kitstore.h`: bytes 28 29 34
35 40 41 46 47, which Digi EQ leaves unused) with RAM marker slot 48.

Saw, pulse and sub all come from one oscillator, so they are always in step, as on the original. Levels
whose peaks would add up past full scale are scaled back together, so the mix never clips.

**Using the rest of the unit as the 101's other half**

- **Filter cutoff / resonance / mod**: SRC page 2 (above); FLTR page after it as well.
- **Envelope**: the track's AMP envelope, which ENV follows (one envelope does both, as on the 101).
- **Portamento**: PORT on SRC page 2 (above).
- **Bender / mod lever**: not built in. Map a controller's lever to TUNE by MIDI CC; the LFOs reach the
  destinations above.

**CPU.** While a track's AMP envelope is idle and its level under -72 dB (Digi Mono's own line for a
stopped voice), its DH-101 voice only keeps time (glide, LFOs, phase) and is not rendered; a trig renders
at least 5 ms. A note from silence fades in over 64 samples (1.3 ms); a note over a sounding one keeps
the oscillator running, as the 101's VCO does. On a unit (OS 1.54, FAST AUDIO on), three DH-101 tracks
with the filter running play without gaps.

**Messages.** TRK + a trig shows "DH-101" (OS 1.54), in place of "101: OFF".

## Files

| path | what |
|---|---|
| `mods/digi101/sh101.c`, `sh101.h` | the engine (plain C, 32-bit integer only); `sh101_tables.h` is generated |
| `mods/digi101/vcf.s` | the filter's per-sample loop in ColdFire assembly (EMAC), held to the C reference |
| `mods/digi101/digi101.c` | the machine for Digi Mono, its page for machine-pages, SRC page 2, its storage, the LFO destinations, messages |
| `mods/digi101/glue.s` | the firmware hooks: sound save / load, the DEST list, the TRK message |
| `mods/digi101/digimono_ext.h` | Digi Mono's machine interface (a copy, as Digi Macro carries; `tools/build_elemods.py` checks they match) |
| `mods/digi101/mod.json` | the manifest (OS 1.53, with the OS 1.54 port); `src/kitstore.h` is added by `tools/build_elemods.py` |
| `tests/digi101_page.c`, `digi101_host_fw.h` | SRC page 2, storage, rest / fade, the LFO routing, on the desktop |
| `tests/digi101_vcf.py`, `digi101_vcf_stub.s` | vcf.s against sh101.c in Unicorn's ColdFire V4e, within 1 LSB |
| `tests/digi101_hooks_fw.py` | the hooks and the LFO page's names (machine-pages, from the page), linked into the real OS 1.54 and run in Unicorn |
| `tests/digi101_engine.py` | the engine's behaviour through the desktop renderer |
| `tools/gen_digi101_tables.py`, `digi101_render.c` | the tables; a desktop renderer (the engine to a WAV, for `tests/digi101_engine.py`) |

## Build and test

```sh
python3 tools/gen_digi101_tables.py                       # mods/digi101/sh101_tables.h
ELK=out/dev/tools/elekloader                              # tools/dev.sh setup fetches it
gcc -O1 -DHOST_TEST -Isrc -I$ELK/elekloader/sdk/include -o out/digi101_page tests/digi101_page.c \
    mods/digi101/digi101.c mods/digi101/sh101.c -lm && out/digi101_page
gcc -O2 -o tools/digi101_render tools/digi101_render.c mods/digi101/sh101.c -lm && python3 tests/digi101_engine.py
tools/dev.sh build digimono digi101                       # with core and machine-pages: an OS file
# Unicorn ones: digiemu's python (tools/dev.sh setup), STOCK = your official OS 1.54 file
python3 tests/digi101_vcf.py
python3 tests/digi101_hooks_fw.py $STOCK core.elemod machine-pages.elemod digimono.elemod digi101.elemod
```

Credits: designed and tested by Robbie; engine and firmware work written with Claude (Anthropic). The
band-limiting method (polyBLEP "ramps") and the per-pattern storage follow Digi Mono's and digi1_mods' by
gdeo607 (MIT). The filter was calibrated by ear and measurement against recordings of TAL-BassLine-101 (no
code from it); its design follows Vadim Zavalishin's and Andrew Simper's public papers on zero-delay-feedback
filters. Clean-room: no firmware, Roland or plug-in code.

**Trademarks.** Roland and SH-101 are trademarks of Roland Corporation; TAL-BassLine-101 is a product of TAL Software GmbH. DH-101 is not affiliated with or
endorsed by any of them: the names say what it was modelled on. No code, samples or data from them is
included (TAL-BassLine-101 was only listened to and measured, from recordings that are not part of this).
