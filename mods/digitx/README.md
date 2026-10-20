# Digi TX: TX KIT

A whole drum kit as **one machine** in the FUNC+SRC list. Knob **B (TYPE)** picks the drum, and a p-lock on B
plays another drum on that step, so one track can play kick, snare and toms.

| B TYPE | drum |
|---|---|
| BD9, BD8 | kicks: 909-style (pitch sweep, hold, click), 808-style (ringing body, sigh, attack step) |
| SD9, SD8 | snares: two tuned bodies and a filtered noise |
| LT9 MT9 HT9 | 909-style toms: a bend, and a partial at 2/3 of the pitch |
| LT8 MT8 HT8 | 808-style toms: the decay shortens as the pitch rises; a droop at the start |

| knob | does |
|---|---|
| A TUNE | transposes the drum (C4 plays it as set) |
| C PTC | pitch |
| D DEC | decay |
| E TONE | kicks: sweep depth (BD9) / drive (BD8); snares: noise decay (SD9) / body balance (SD8); toms: bend (9) / drive (8) |
| F ACC | level |
| G SNAP | kicks: click (BD9) / attack step (BD8); snares and toms: noise |
| H CHAR | BD9 hold, BD8 sigh, snares: the second body's tuning, toms 9: the partial, toms 8: the droop |

**Keep the AMP decay long** (or HOLD up): the machine shapes the sound itself, and Digi Mono stops a voice
once its AMP envelope is idle.

## Where the sounds come from

The drums follow the TX-8 and TX-9 machines of hugz202's
[AE-analog-rytm-mods](https://github.com/hugz202/AE-analog-rytm-mods) (MIT licence), whose design notes give
the pitches, sweeps and decay times measured on the originals (`mods/0009-tx-machines/design.md`). On that
project most of the drums drive the Rytm's analog voices; here every part is synthesised digitally, by our own
code (`tx.c`), from those measurements. No code or data of the originals is used.

Checked: `tests/digitx_model.c` (each drum's pitch and decay against the measurements: the BD9 sweep
125 > 86 > 66 > 58 > 56 Hz cycle by cycle against 122 > 83 > 64 > 58 > 56, its decay, BD8 25 / 49.7 / 98 Hz, the
toms' pitches), `tests/digiemu_tx.py` (the machine in the list, its SRC page, knob B changing the drum while
a pattern plays, track 1's output).

Needs Digi Mono 0.25 or later (it renders the voices; 0.25 opens machine ids 30 and 31 to other mods) and
machine-pages. Machine id 30.
