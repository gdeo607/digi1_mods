#!/usr/bin/env python3
"""Tone+FX (DT1 port): the MIDI CC entry and the effect chain, in unicorn.

    <python with digiemu's patched unicorn> tests/emu_tonefx.py --stock <official .syx> --elekloader <checkout> \
        --mods <core.elemod> <tonefx.elemod>

Links the mods as elekloader does and runs the linked image in unicorn (ColdFire V4e):
  1. CC: the patched site 0x400d4a90 is entered as the firmware's MIDI task enters it (msg on the stack),
     and run to 0x400d4a98, where the stock handler goes on. Every CC of the map sets its parameter
     (on/off at 64), the other 128 - 22 numbers change nothing, a closed CC receive gate changes nothing,
     and the stack and every register the stock code relies on are as the two displaced instructions
     leave them.
  2. the chain: digictl_mod_in / the render_in handlers, then the render's master stage entered at the
     patched site 0x400721e6 (core 4.1's ev_render_master -> master.c), on random master blocks (8 bits hot, random low
     bits). All off: the master bit-exact. Each effect on: the block changes, stays within 24 bits, and
     every register the stage has live, the stack and d4 are as the replaced instruction leaves them.
"""
import argparse, os, random, struct, sys

ap = argparse.ArgumentParser()
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", required=True)
ap.add_argument("--mods", nargs="+", required=True)
a = ap.parse_args()
sys.path.insert(0, a.elekloader)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from elekloader import syx, devices, elemod, link
from port_os import fw as _fw
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UcError
from unicorn.m68k_const import *

st = syx.Syx.load(a.stock)
dev, rel = devices.identify(st.sha256)
OS = rel.version
fw = lambda x: _fw(x, OS)
L = link.link([elemod.load_any(p) for p in a.mods], st.section(dev.main_section))
MAP = L.map
img = L.image
ddr0, run_end = L.layout["ddr"]
bss0, bss1 = L.layout["bss"]
run = img[L.layout["run_load"] - 0x40000400: L.layout["run_load"] - 0x40000400 + run_end - ddr0]
sym = lambda n: MAP.get(n, MAP.get("tonefx:" + n))

uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
uc.mem_map(0x40000000, 0x00400000)
uc.mem_write(0x40000400, img[:0x3fc000])
uc.mem_map(0x47b00000, 0x00100000)                   # the mods' RAM image + bss, and a stack
uc.mem_map(0x41900000, 0x00200000)                   # the CC gate, the kits
uc.mem_map(0x4ba80000, 0x00020000)                   # the SSI output buffer
uc.mem_map(0xfc040000, 0x00010000)                   # the eDMA (channel 54's SADDR)
uc.mem_write(ddr0, run)
uc.mem_write(bss0, bytes(bss1 - bss0))
STACK, SENT, MSG = 0x47bf0000, 0x47b00100, 0x47b00200
uc.mem_write(SENT, b"\x4e\x71\x4e\x71")
fails = []


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        fails.append(what)


def rd(n):
    return struct.unpack(">i", uc.mem_read(sym(n), 4))[0]


def wr(n, v):
    uc.mem_write(sym(n), struct.pack(">i", v))


def call(fn, *args, stop=None):
    sp = STACK - 4 * len(args) - 4
    uc.mem_write(sp, struct.pack(">I", SENT) + b"".join(struct.pack(">I", x & 0xffffffff) for x in args))
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(fn, stop or SENT, count=5_000_000)
    return uc.reg_read(UC_M68K_REG_D0)


# ---- 1. CC ----
print("CC entry (0x400d4a90 -> the glue -> 0x400d4a98)")
CC = {11: "digiring_on", 8: "digiring_depth", 36: "digiring_freq", 37: "digifold_on", 40: "digifold_amount",
      41: "digifold_mode", 67: "digieq_on", 69: "digieq_lo", 96: "digieq_hi", 68: "digimeter_on",
      42: "digicrush_on", 43: "digicrush_mode", 44: "digicrush_amount", 45: "digicrush_tune", 46: "digiwave_on",
      47: "digiwave_pmode", 52: "digiwave_smode", 53: "digiwave_page", 48: "digiwave_speed", 49: "digiwave_depth",
      50: "digiwave_mix", 51: "digiwave_fb"}
ONOFF = {"digiring_on", "digifold_on", "digieq_on", "digimeter_on", "digicrush_on", "digiwave_on", "digiwave_page"}
NAMES = sorted(set(CC.values()))
GATE = fw(0x4197b77c)
SITE, CONT = fw(0x400d4a90), fw(0x400d4a98)


def send(cc, val, gate=1, ch=0):
    uc.mem_write(GATE, struct.pack(">I", gate))
    uc.mem_write(MSG, bytes([0xb0 | ch, cc, val]))
    regs = {r: random.getrandbits(32) for r in [UC_M68K_REG_D0 + i for i in range(2, 8)] +
            [UC_M68K_REG_A0 + i for i in range(2, 4)]}
    for r, v in regs.items():
        uc.reg_write(r, v)
    sp = STACK - 8
    uc.mem_write(sp, struct.pack(">II", SENT, MSG))
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(SITE, CONT, count=200000)
    nsp = uc.reg_read(UC_M68K_REG_A7)
    saved = struct.unpack(">8I", uc.mem_read(nsp, 32))
    order = [UC_M68K_REG_D0 + i for i in range(2, 8)] + [UC_M68K_REG_A0 + 2, UC_M68K_REG_A0 + 3]
    ok = nsp == sp - 32 and list(saved) == [regs[r] for r in order] and \
        struct.unpack(">I", uc.mem_read(nsp + 32, 4))[0] == SENT and uc.reg_read(UC_M68K_REG_PC) == CONT
    return ok


good = True
for cc, name in sorted(CC.items()):
    for val in (0, 63, 64, 100, 127):
        for n in NAMES:
            wr(n, 33)
        good &= send(cc, val, ch=random.randrange(16))
        exp = (1 if val >= 64 else 0) if name in ONOFF else val
        alsopage = {47: 0, 52: 1}.get(cc)              # a PITCH / STEREO mode CC also selects its group
        changed = [n for n in NAMES if rd(n) != 33 and not (n == "digiwave_page" and rd(n) == alsopage)]
        if rd(name) != exp or changed not in ([name], []) or (changed == [] and exp != 33):
            check(False, "CC %d value %d -> %s = %d (expected %d), changed %s" % (cc, val, name, rd(name), exp, changed))
            good = False
check(good, "the 22 CCs of the map set their parameters (on/off at 64; the PITCH/STEREO mode CCs also select their group), on any channel")
quiet = True
for cc in range(128):
    if cc in CC:
        continue
    for n in NAMES:
        wr(n, 33)
    quiet &= send(cc, 127)
    quiet &= all(rd(n) == 33 for n in NAMES)
check(quiet, "the other 106 CCs change nothing")
for n in NAMES:
    wr(n, 33)
ok = send(8, 100, gate=0)
check(ok and all(rd(n) == 33 for n in NAMES), "CC receive off (gate 0x4197b77c clear): nothing changes")
check(good and quiet, "stack, return address and d2-d7/a2-a3 as the displaced lea/movem leave them, PC at 0x400d4a98")
dirty = rd("dn_store_dirty")
check(dirty == 0 or dirty == 1, "the pattern store is marked dirty by a CC (dn_store_dirty %d)" % dirty)

# ---- 2. the chain ----
print("the effect chain, in the order of the render events")
order_in = ["digictl_mod_in", "digiring_render_in", "digieq_render_in", "digifold_render_in",
            "digimeter_render_in", "digicrush_render_in", "digiwave_render_in"]
MASTER, SITE_M, AFTER_M = 0x8000ea70, fw(0x400721e6), fw(0x400721e6) + 6
uc.mem_map(0x80000000, 0x00010000)                   # the SRAM: the master block
rng = random.Random(7)


def block(half, amp=1 << 21):
    """one render block: the ev_render_in handlers, then the render's master stage entered at the patched
    site (0x400721e6 -> core 4.1's core_render_master -> ev_render_master -> tonefx_master) with the master
    pair 8 bits hot, low bits random"""
    v = [(rng.randint(-amp, amp) << 8) | rng.randrange(256) for _ in range(64)]
    b = struct.pack(">64i", *v)
    uc.mem_write(MASTER, b)
    keep = {r: rng.getrandbits(32) for r in [UC_M68K_REG_D0 + i for i in range(2, 8)] +
            [UC_M68K_REG_A0 + i for i in range(2, 7)]}
    regsok = True
    for fn in order_in:
        for r, x in keep.items():
            uc.reg_write(r, x)
        call(sym(fn))
        regsok &= all(uc.reg_read(r) == x for r, x in keep.items())
    live = {r: rng.getrandbits(32) for r in [UC_M68K_REG_D0 + i for i in (0, 1, 2, 3, 5, 6, 7)] +
            [UC_M68K_REG_A0 + i for i in range(0, 7)]}
    for r, x in live.items():
        uc.reg_write(r, x)
    uc.reg_write(UC_M68K_REG_A7, STACK)
    uc.emu_start(SITE_M, AFTER_M, count=2_000_000)
    regsok &= all(uc.reg_read(r) == x for r, x in live.items()) and uc.reg_read(UC_M68K_REG_D4) == MASTER \
        and uc.reg_read(UC_M68K_REG_A7) == STACK
    o = bytes(uc.mem_read(MASTER, 256))
    return b, o, True, regsok


def settle(n=40):
    for i in range(n):
        block(i & 1)


for n in NAMES:
    wr(n, 0)
for n, v in (("digiwave_depth", 64), ("digieq_lo", 64), ("digieq_hi", 64), ("digiring_freq", 6)):
    wr(n, v)
settle()
same = all(b == o and oth for b, o, oth, _ in (block(i & 1) for i in range(200)))
check(same, "every effect off: 200 blocks of the master bit-exact (low bits included)")
for name, setup in (("RING", {"digiring_on": 1, "digiring_depth": 100, "digiring_freq": 30}),
                    ("TILT", {"digieq_on": 1, "digieq_lo": 20, "digieq_hi": 110}),
                    ("FOLD", {"digifold_on": 1, "digifold_amount": 90, "digifold_mode": 100}),
                    ("CRUSH bits", {"digicrush_on": 1, "digicrush_mode": 0, "digicrush_amount": 100}),
                    ("CRUSH sr", {"digicrush_on": 1, "digicrush_mode": 64, "digicrush_amount": 100}),
                    ("CRUSH crunch", {"digicrush_on": 1, "digicrush_mode": 120, "digicrush_amount": 100}),
                    ("WOB", {"digiwave_on": 1, "digiwave_page": 0, "digiwave_pmode": 0, "digiwave_depth": 90}),
                    ("SHFT", {"digiwave_on": 1, "digiwave_page": 0, "digiwave_pmode": 100, "digiwave_depth": 100}),
                    ("FLAN", {"digiwave_on": 1, "digiwave_page": 1, "digiwave_smode": 10, "digiwave_depth": 90,
                              "digiwave_fb": 127, "digiwave_mix": 127}),
                    ("PAN", {"digiwave_on": 1, "digiwave_page": 1, "digiwave_smode": 64, "digiwave_depth": 127}),
                    ("WIDE", {"digiwave_on": 1, "digiwave_page": 1, "digiwave_smode": 120, "digiwave_depth": 127})):
    for n in NAMES:
        wr(n, 0)
    for n, v in (("digiwave_depth", 64), ("digieq_lo", 64), ("digieq_hi", 64), ("digiring_freq", 6)):
        wr(n, v)
    settle()
    for n, v in setup.items():
        wr(n, v)
    settle()
    res = [block(i & 1, amp=1 << 23 if i % 3 == 0 else 1 << 21) for i in range(200)]
    diff = sum(b != o for b, o, _, _ in res)
    peak = max(max(abs(x >> 8) for x in struct.unpack(">64i", o)) for _, o, _, _ in res)
    check(diff > 150 and peak < (1 << 23) and all(oth and rk for _, _, oth, rk in res),
          "%-12s %d of 200 blocks changed, peak %d < 2^23 (24-bit), the stage's registers and d4 as stock"
          % (name, diff, peak))

# ---- 3. cost: instructions a block, every effect on at once (the heaviest modes), and the defaults ----
from unicorn import UC_HOOK_CODE
cnt = [0]
hk = uc.hook_add(UC_HOOK_CODE, lambda u, ad, sz, d: cnt.__setitem__(0, cnt[0] + 1), begin=ddr0, end=run_end - 1)
uc.ctl_remove_cache(ddr0, run_end)                     # code translated before the hook would not count
def cost(setup, label):
    for n in NAMES:
        wr(n, 0)
    for n, v in (("digiwave_depth", 64), ("digieq_lo", 64), ("digieq_hi", 64), ("digiring_freq", 6)):
        wr(n, v)
    for n, v in setup.items():
        wr(n, v)
    settle()
    cnt[0] = 0
    for i in range(100):
        block(i & 1)
    print("  cost  %-40s %5d instructions a block (all handlers)" % (label, cnt[0] // 100))
cost({}, "everything off")
cost({"digiring_on": 1, "digiring_depth": 51, "digieq_on": 1, "digifold_on": 1, "digimeter_on": 1}, "the defaults (ring, TILT, fold on; meter)")
cost({"digiring_on": 1, "digiring_depth": 100, "digieq_on": 1, "digieq_lo": 20, "digieq_hi": 110,
      "digifold_on": 1, "digifold_amount": 90, "digifold_mode": 127, "digicrush_on": 1, "digicrush_mode": 0,
      "digicrush_amount": 100, "digicrush_tune": 100, "digiwave_on": 1, "digiwave_page": 0, "digiwave_pmode": 100,
      "digiwave_depth": 100, "digimeter_on": 1}, "all on, SHFT + BITS with dither + TRSH")
uc.hook_del(hk)

print("ALL CHECKS PASSED" if not fails else "FAILED: %d" % len(fails))
sys.exit(1 if fails else 0)
