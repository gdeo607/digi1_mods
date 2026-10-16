# Tone+FX for the DT1

**Tone+FX 3.5r** by Digital Alchemy DSP, ported from its original FM-groovebox
version to the DT1 (OS 1.53 and 1.54). The source is theirs
([DigiAlchemydsp/RingTone](https://github.com/DigiAlchemydsp/RingTone); the
release repo is [DigiAlchemydsp/Tone-FX](https://github.com/DigiAlchemydsp/Tone-FX)),
under GPL-2.0-or-later, so this port is GPL-2.0-or-later too (`LICENSE`). The
effects, pages, PERFORM pads, macros, store, CC handling and boot splash are
their code. What the DT1 needed is listed under "What the port changed".

Unofficial. Flash at your own risk and keep the official OS file for recovery.

## What it does

Master effects on every output (main, headphones and USB audio), in this order:
**ring → TILT (low/high shelf) → CRUSH → FOLD → WAVE (PITCH / STEREO) → output
ceiling → meter**. There are **eleven pages** after the stock master pages
(FUNC+LFO, then LEFT / RIGHT):

| page | knobs | |
|---|---|---|
| **DIGI RING** | A on/off · E depth · F frequency | ring modulator / tremolo |
| **DIGI FOLD** | A on/off · D fold type (CLEAN/MUD/DIST/TRSH) · E-H amount | wavefolder, with the L/R meter |
| **DIGI TILT** | A on/off · E low shelf · H high shelf (64 = flat) | |
| **DIGI CRUSH** | A on/off · D mode (BITS/SR/CRUNCH) · E amount · F tune | |
| **DIGI PITCH** | A on/off · D mode (WOB/SHFT) · E speed · F depth · G mix | |
| **DIGI STEREO** | A on/off · D mode (FLAN/PAN/WIDE) · E speed · F depth · G mix · H feedback | |
| **PERFORM** | A–H = one depth per engine | static; 16 momentary trig pads |
| **WASH · RADIO · DESTROY · SPACE** | A on/off · B–H amount | one-slider macros, each driving a bundle of engines |

**Page order (3.5r, two trees).** LEFT / RIGHT cycle the stock master pages
(Compressor, internal mixer, external mixer) then the FX pages in play order
**FOLD · CRUSH · RING · STEREO · PITCH · TILT**. **UP** (or **DOWN**) toggles the
macro tree **PERFORM · WASH · RADIO · DESTROY · SPACE**; within it LEFT/RIGHT
cycle the five, and UP/DOWN returns to the FX page you left. PERFORM is not in
the FX cycle.

The other controls:

- **PERFORM pads.** On the PERFORM page the 16 trigs are momentary pads, one
  block per engine, four increasing intensities: **1 2 9 10** ring, **3 4**
  fold, **11 12** crush, **5 6 13 14** pitch, **7 8 15 16** stereo. Holding a
  pad ramps its block up and switches it on; letting go ramps it back to the
  pattern's value. A pad never changes what is saved.
- **Values.** Every value runs 0-127, one step per knob notch.
- **Defaults.** Ring, TILT and fold start switched on, as on the original.
- **No boot splash** (3.2a3): the unit starts with its own intro. The splash
  code (`render.c`, `splash.s`) is kept here but not built.
- **MIDI CC.** All FX parameters answer incoming CC on the DT1's free numbers
  (below).

## What the port changed

- **Sound path.** On the original the effects process the codec's output
  buffer at the end of the render. On the DT1 that buffer feeds only the
  analog outputs, so here they run one step earlier, on the master mix, at the
  render stage that spreads it over the outputs (0x400721e6, Digi EQ's site).
  From there it reaches the main outputs, the headphones and the 12-channel bus
  the **USB audio** stream is built from.
  - `master.c` copies the block (0x8000ea70, 8 bits hotter than the outputs) as
    24-bit samples, as on the original, and runs the effects in their order.
  - It puts back only what they changed, so with every effect off the master
    is bit-exact, low bits included.
  - Every effect's code is unchanged.
- **The pages.** The DT1's master view shares one view class with the track
  pages (kinds at +124/+128, index +144; stock master kinds {11 COMP, 12
  internal mixer, 13 external mixer}). Its kinds 1..10 are real parameter pages,
  so the eleven pages are eleven **kind-0** ("None") entries in a row, told
  apart by their place in the vector. The navigation is **index-based** for
  that reason (a stock kind cannot be turned into an index). The title bar names
  each page, for example "DIGI RING (4/10)": the view's title getter (vtable
  slot 19) sets kind 0's names first. LEFT / RIGHT and UP / DOWN change pages
  the way the stock page key does (`dt_page_show`), so the title pops up as for
  a stock page.
- **Keys.** These are the DT1's ids: UP 14, DOWN 15, LEFT 16, RIGHT 17, trigs
  24..39, FUNC held = flags bit 1. The PERFORM pads ignore a trig while FUNC,
  TRK, PTN, BANK, SONG, GLOBAL, SAMPLE or TEMPO is held, so **PTN + trig still
  changes the pattern** on the PERFORM page.
- **Knobs.** `DC_PER_STEP = 4`, the same as digictl on the original: one step
  per notch (the 3.2a3 port used 16, which moved every knob four times too
  slowly).
- **MIDI CC.** The hook is the DT1's CC entry (0x400d4a90; 0x400d4cb8 on
  1.54). It runs once per incoming CC, on any channel, while MIDI CC receive is
  on, and every CC still reaches the stock handler too.
  - CC 20..23 are the DT1's SRC parameters and 24..31 its auto-channel FX,
    so **CRUSH and WAVE moved to 42..53**. The rest kept the original's
    numbers:

    | CC | | CC | |
    |---|---|---|---|
    | 8 | ring depth | 42 | CRUSH on/off |
    | 11 | ring on/off | 43 | CRUSH mode |
    | 36 | ring rate | 44 | CRUSH amount |
    | 37 | FOLD on/off | 45 | CRUSH tune |
    | 40 | FOLD amount | 46 | WAVE on/off |
    | 41 | FOLD type | 47 | PITCH mode (also selects PITCH) |
    | 67 | EQ on/off | 48 | WAVE speed |
    | 68 | meter on/off | 49 | WAVE depth |
    | 69 | EQ low shelf | 50 | WAVE mix |
    | 96 | EQ high shelf | 51 | WAVE feedback |
    | | | 52 | STEREO mode (also selects STEREO) |
    | | | 53 | WAVE group (>= 64 STEREO) |

  - None of these numbers is in the DT1's CC tables. They were read from
    the running firmware (tests/digiemu_tonefx.py).
- **Per-pattern settings.** The settings follow the pattern, as on the original.
  - 3.5r has **35 params** (the six FX, the meter, the four macros and the WAVE
    dry/wet). The kit's RAM-only spare is 34 bytes (kit + 0x530 .. 0x551), so
    they are stored **7 bits each, packed** after a 3-byte header ("DX",
    version 8): `ceil(35 * 7 / 8) = 31` bytes, 34 in all. Everything, the
    macros included, stays per-pattern.
  - Those bytes are **not written to the +Drive**. The settings are kept while
    the unit is on, but **not over a power cycle or a project load**, where a
    pattern starts from the live values.
  - The original keeps them in a saved part of its pattern. The DT1's
    saved spare bytes per pattern (48) are taken by Digi Matrix, Digi Poly and
    Digi EQ.
- **RAM.** All mods share 128 KB. Tone+FX 3.5r takes 36.9 KB in all (`.run`
  25.8 KB, `.bss` 10.8 KB).
- **Track-LFO bridge.** It is parked on the original (nothing sets it), and is
  off here.

## Using it with other mods

- **Digi EQ: not together.** Both take the master view's draw (vtable slot 4)
  and the render site 0x400721e6, and both use kind-0 pages.
- **Everything else: combines.** digichain, digiutils, digimatrix, digipoly,
  digimono, SOPHIE, DigiFilter and digihealth (elekloader's check, OS 1.54).
- **RAM is the limit.** Without Digi EQ and Digi Macro the rest fits with
  Tone+FX, with a few KB to spare.
- **CPU.** The cost is per audio block of 0.67 ms, measured in unicorn
  (tests/emu_tonefx.py). A very busy project with heavy settings could click;
  digihealth's FAST AUDIO gives the render room.

## Build and test

One source builds for **both** OS versions; the stock file picks the release
(the `ports` entry in `mod.json`). `--version` must be exactly 4 ASCII chars.

```sh
# OS 1.53 -> out/tonefx-3.5r.elemod
python -m elekloader.sdk.build mods/tonefx --stock Digitakt_OS1.53.syx

# OS 1.54 -> out/tonefx-3.5r-os1.54.elemod
python -m elekloader.sdk.build mods/tonefx --stock Digitakt_OS1.54.syx   # or the official .zip
```

- `tests/emu_tonefx.py` (unicorn) covers the CC entry, the chain (bit-exact with
  everything off) and the registers kept.
- `tests/digiemu_tonefx.py` (digiemu, the real firmware) covers the eleven pages
  and their titles, the knobs and modes, the PERFORM page and its pads, the
  output level through each effect, bit-exact output with everything off, the
  USB bus, and the per-pattern store.
- Both pass on OS 1.53 and 1.54 in the emulator. The 3.5r port has not run on a
  unit yet.

## Files

| | |
|---|---|
| `meter.c` `eq.c` `ring.c` `fold.c` `crush.c` `wave.c` | the effects (Digital Alchemy DSP's) |
| `master.c` `master_glue.s` | the chain on the master mix (main outputs, headphones, USB) |
| `digictl.c` | the pages, knobs, keys, PERFORM pads, macros, store and CC map (theirs, with the DT1 changes marked "DT1 port") |
| `cc_glue.s` | the DT1's CC entry |
| `page_glue.s` | the title getter and the stock page change |
| `render.c` `splash.s` | the boot splash: not built since 3.2a3 |
| `corea.h` `sin256.h` | the render helpers and the sine table |

---

# Releasing Tone+FX — cross-platform, multi-OS

Tone+FX ships from **two source trees** and covers **four stock OS files**:

| device | OS | source tree | build with | core |
|---|---|---|---|---|
| Digitone mk1 / Keys | **1.43** | `digitone_corea` | `mods/tonefx` (top level) | `core-dn1-4.0.elemod` |
| Digitone mk1 / Keys | **1.44** | `digitone_corea` | `mods/tonefx` (`ports.1.44`) | `core-dn1-4.0-os1.44.elemod` |
| Digitakt mk1 | **1.53** | this repo (`digi1_mods`) | `mods/tonefx` | `core-4.0.elemod` |
| Digitakt mk1 | **1.54** | this repo (`digi1_mods`) | `mods/tonefx` (`ports.1.54`) | `core-4.0-os1.54.elemod` |

The effect sources are the same; each tree carries that device's render path,
UI model, keys, CC map and per-pattern store. A release therefore has **four
`.elemod` files** plus each device's core.

## 1. Build the Digitakt side (this repo)

```sh
# from digi1_mods-main/, with m68k binutils on the PATH and elekloader 0.5.0+:
export ELEKLOADER_CROSS=m68k-elf-
export PYTHONPATH=<elekloader checkout>

python -m elekloader.sdk.build mods/tonefx --stock Digitakt_OS1.53.syx       # -> out/tonefx-<v>.elemod
python -m elekloader.sdk.build mods/tonefx --stock Digitakt_OS1.54.syx       # -> out/tonefx-<v>-os1.54.elemod
```

`tools/dev.sh mods` and `tools/build_elemods.py` do the same and refresh
`elemods/` (`tools/dev.sh publish`).

## 2. Build the Digitone side (`digitone_corea`)

See its `HANDOFF.md` and `mods/tonefx/README.md`:

```sh
python -m elekloader.sdk.build mods/tonefx --stock Digitone_and_Digitone_Keys_OS1.43.syx  # -> tonefx-<v>.elemod
python -m elekloader.sdk.build mods/tonefx --stock Digitone_and_Digitone_Keys_OS1.44.syx  # -> tonefx-<v>-os1.44.elemod
```

The DT1 and the Digitone **share the source**, so keep the effect code in step:
a change to `ring.c`/`eq.c`/`crush.c`/`fold.c`/`wave.c`/`meter.c` on the
Digitone side must be re-applied here (a 3-way merge: `ours`=this port,
`base`=the Digitone 3.2a the port forked from, `theirs`=the new Digitone), and
vice versa. The render plumbing (`master.c`, `corea.h`), the page model
(`digictl.c`) and the store are the DT1's own.

## 3. Verify before shipping

For each of the four builds:

```sh
python -m elekloader.sdk.build mods/tonefx --stock <stock>            # must print "imports nothing"
python -m elekloader.lint   out/tonefx-<v>[-os<OS>].elemod --stock <stock> --with <core>
python -m elekloader.patch  --stock <stock> --mod <core> --mod out/tonefx-<v>[-os<OS>].elemod --out /tmp/t.syx --version <v>
```

`lint` must say it links; `patch` must verify byte-for-byte. Run the emulator
tests (`tests/digiemu_tonefx.py`, `tests/emu_tonefx.py` here; the Digitone's
`digiemu_*` set there) on 1.53 and 1.54 (and the Digitone's on 1.43/1.44).

## 4. Name and package

- `tonefx-<v>.elemod` and `tonefx-<v>-os<OS>.elemod`, one pair per device; the
  `.elemod` is named `<id>-<version>[-os<OS>].elemod` by the SDK.
- Ship each device's **core** beside it (`core-dn1-4.0[-os1.44].elemod` for the
  Digitone, `core-4.0[-os1.54].elemod` for the DT1).
- `--version` must be exactly 4 ASCII characters (e.g. `3.5r`).
- `elemods/README.md` names the versions and licences; `CHANGELOG.md` records
  what changed; `COMPATIBILITY.txt` says which pairs combine.

## 5. What is NOT yet done (3.5r)

- The later Digitone **UI pass** (full-height sliders, the value centred on the
  middle digit / one-line caption, the meter moved left of the arrow) is **not**
  applied to the DT1; the port has the 3.5r UI plus DOWN navigation.
- `tests/digiemu_tonefx.py` was updated for the 3.5r order and 11 pages but not
  run to completion.
- No DT1 unit test yet for 3.5r; the packed store (`"DX"` v8) and the macros
  need a hardware check.
