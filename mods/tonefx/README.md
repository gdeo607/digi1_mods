# Tone+FX for the DT1

**Tone+FX 3.2a** by Digital Alchemy DSP, ported from its original FM-groovebox
version to the DT1 (OS 1.53 and 1.54). The source is theirs
([DigiAlchemydsp/RingTone](https://github.com/DigiAlchemydsp/RingTone), commit
`0310695`, the 3.2a release; the release repo is
[DigiAlchemydsp/Tone-FX](https://github.com/DigiAlchemydsp/Tone-FX)), under
GPL-2.0-or-later, so this port is GPL-2.0-or-later too (`LICENSE`). The effects,
pages, PERFORM pads, store, CC handling and boot splash are their code (the splash
is not built since 3.2a3). What the
DT1 needed is listed under "What the port changed".

Unofficial. Flash at your own risk and keep the official OS file for recovery.

## What it does

Master effects on every output (main, headphones and USB audio), in this order:
**ring → TILT (low/high shelf) → CRUSH → FOLD → WAVE (PITCH / STEREO) → output
ceiling → meter**. There are seven pages after the stock master pages
(FUNC+LFO, then LEFT / RIGHT):

| page | knobs | |
|---|---|---|
| **DIGI RING** | A on/off · E depth · F frequency | ring modulator / tremolo |
| **DIGI FOLD** | A on/off · D fold type (CLEAN/MUD/DIST/TRSH) · E-H amount | wavefolder, with the L/R meter |
| **DIGI TILT** | A on/off · E low shelf · H high shelf (64 = flat) | |
| **DIGI CRUSH** | A on/off · D mode (BITS/SR/CRUNCH) · E amount · F tune | |
| **DIGI PITCH** | A on/off · D mode (WOB/SHFT) · E speed · F depth | |
| **DIGI STEREO** | A on/off · D mode (FLAN/PAN/WIDE) · E speed · F depth · G mix · H feedback | |
| **PERFORM** | A ring depth · B fold · C crush · D pitch depth · E stereo depth · F EQ low · G EQ high · H wave speed | press **UP** on any of the six to get here, and UP again to go back |

The other controls:

- **PERFORM pads.** On the PERFORM page the 16 trigs are momentary pads. They
  come in four blocks of four increasing intensities: **1 2 9 10** ring,
  **3 4 11 12** fold + crush, **5 6 13 14** pitch, **7 8 15 16** pan. Holding
  a pad ramps its block up and switches it on. Letting go ramps it back to the
  pattern's value. A pad never changes what is saved.
- **Values.** Every value runs 0-127, one step per knob notch.
- **Defaults.** Ring, TILT and fold start switched on, as on the original.
- **No boot splash** (3.2a3): the unit starts with its own intro. Up to 3.2a2
  the TONE+FX wordmark played one of three animations at power-on; its code
  (`render.c`, `splash.s`) is kept here but not built.

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
- **The pages.**
  - The DT1's master view is the same class: kinds at +124/+128, index
    +144, stock kinds {11, 12, 13}.
  - Its kinds 1..10 are real parameter pages, so the seven pages are seven
    kind-0 ("None") entries, told apart by their place in the list.
  - The title bar names each page, for example "DIGI RING (4/10)". To do this,
    the view's title getter (vtable slot 19) sets kind 0's names first. LEFT /
    RIGHT and UP change pages the way the stock page key does, so the title
    pops up as for a stock page.
- **Keys.** These are the DT1's ids: UP 14, LEFT 16, RIGHT 17, trigs
  24..39, FUNC held = flags bit 1. The PERFORM pads ignore a trig while FUNC,
  TRK, PTN, BANK, SONG, GLOBAL, SAMPLE or TEMPO is held, so **PTN + trig still
  changes the pattern** on the PERFORM page.
- **Knobs.** The DT1 sends 16 counts per notch (the original sends 4), so
  it is still one step per notch.
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
    | | | 53 | WAVE group (≥ 64 STEREO) |

  - None of these numbers is in the DT1's CC tables. They were read from
    the running firmware (tests/digiemu_tonefx.py).
- **Per-pattern settings.** The settings follow the pattern, as on the original.
  - They live in the pattern's own kit, in 34 bytes the firmware neither uses
    nor saves (kit + 0x530). The same 24 params fit as one byte each: "DX",
    version 7, count 24, then the values.
  - Those bytes are **not written to the +Drive**. The settings are kept while
    the unit is on, but **not over a power cycle or a project load**, where a
    pattern starts from the live values.
  - The original keeps them in a saved part of its pattern. The DT1's
    saved spare bytes per pattern (48) are taken by Digi Matrix, Digi Poly and
    Digi EQ.
- **RAM.** All mods share 128 KB (256 KB with core 4.0). Tone+FX 3.2a3 takes 36.4 KB in all (.run
  19.8 KB, .bss 16.7 KB); without the splash it is 3.5 KB smaller than 3.2a2.
- **Track-LFO bridge.** It is parked on the original (nothing sets it), and is
  off here.

## Using it with other mods

- **Digi EQ: not together.** Both take the master view's draw (vtable slot 4)
  and the render site 0x400721e6, and both use kind-0 pages.
- **Everything else: combines.** digichain, digiutils, digimatrix, digipoly,
  digimono, SOPHIE, DigiFilter and digihealth (elekloader's check, OS 1.54).
- **RAM is the limit.** The 2.1j build (with Digi EQ and MACRO) has 2.5 KB left,
  so Tone+FX does not fit next to all of it. Without Digi EQ and Digi Macro the
  rest fits with Tone+FX, with 4.2 KB to spare.
- **CPU.** The cost is per audio block of 0.67 ms, measured in unicorn
  (tests/emu_tonefx.py):

  | settings | instructions a block |
  |---|---|
  | everything off | ~2,050 (the copy in and out, and the output ceiling) |
  | the defaults | ~5,400 (about Digi EQ's) |
  | everything on at the heaviest modes | ~16,600 |

  A very busy project with heavy settings could click. digihealth's FAST AUDIO
  gives the render room, and its SYSTEM INFO shows the load.

## Build and test

```sh
STOCK=DT1_OS1.54.syx VERSION=TFX1 tools/dev.sh build tonefx      # core + Tone+FX
python -m elekloader.sdk.build mods/tonefx --stock DT1_OS1.54.syx  # the .elemod alone
```

- `tests/emu_tonefx.py` (unicorn) covers:
  - the CC entry: all 22 CCs, the other 106, the receive gate, and the stack
    and registers left for the stock handler;
  - the chain, entered at the patched render site with the master 8 bits hot:
    with every effect off the master is bit-exact, and each mode changes the
    signal within 24 bits;
  - the registers kept, and the cost.
- `tests/digiemu_tonefx.py` (digiemu, the real firmware) covers:
  - the seven pages, their titles, knobs and modes;
  - the PERFORM page and its pads;
  - the output level of a test tone through each effect, and bit-exact output
    with everything off;
  - the USB bus (0x80002160) equal to the chain's output in every block;
  - the per-pattern store across pattern changes, and PTN + trig on the PERFORM
    page;
  - the firmware's CC tables.
- All of it passed on OS 1.53 and 1.54. It has not run on a unit yet.

## Files

| | |
|---|---|
| `meter.c` `eq.c` `ring.c` `fold.c` `crush.c` `wave.c` | the effects (Digital Alchemy DSP's; `wave.c`'s delay line was also the splash's scratch up to 3.2a2) |
| `master.c` `master_glue.s` | the chain on the master mix (main outputs, headphones, USB) |
| `digictl.c` | the pages, knobs, keys, PERFORM pads, store and CC map (theirs, with the DT1 changes marked "DT1 port") |
| `cc_glue.s` | the DT1's CC entry |
| `page_glue.s` | the title getter and the stock page change |
| `render.c` `splash.s` | the boot splash, on the DT1's intro presents: not built since 3.2a3 (add both to `sources` and the seven `tonesplash_*` sites to bring it back) |
| `corea.h` `sin256.h` | the render helpers and the sine table |
