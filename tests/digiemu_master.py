#!/usr/bin/env python3
"""Core 4.1's master output and master pages in the real firmware (digiemu, headless), with elekloader's
examples/master-mono, or with the core alone.

    python3 tests/digiemu_master.py --digiemu <checkout, after --add of the build> --fw <firmware folder>
        --stock <official .syx> --elekloader <checkout> --mods <core> [<master-mono>] --png <dir>

With master-mono: FUNC+LFO three times shows its page third of four ("Master mono (3/4)", after the internal
mixer), knob A steps it STEREO -> MONO -> SWAP, and a tone put into the left of the master pair as core's
ev_render_master starts is measured in the 12-channel bus the USB stream is built from: left only, then both at
half, then right only. With the core alone: the master pages are the stock three ("Compressor (1/3)") and the
tone reaches the bus unchanged. Prints a report and PASS/FAIL.
"""
import argparse, math, os, struct, sys, time, types, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--digiemu", required=True)
ap.add_argument("--fw", required=True)
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", required=True)
ap.add_argument("--mods", nargs="+", required=True)
ap.add_argument("--png", required=True)
a = ap.parse_args()

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

sys.path.insert(0, a.elekloader)
from elekloader import syx as _syx, devices as _dev, elemod as _em, link as _link
_st = _syx.Syx.load(a.stock)
_d, _r = _dev.identify(_st.sha256)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from port_os import fw as _fw
OS = _r.version                                    # the firmware addresses here are 1.53's; fw() moves them
def fw(x):
    return _fw(x, OS)
MAP = _link.link([_em.load_any(p) for p in a.mods], _st.section(_d.main_section)).map

FW = os.path.join(a.digiemu, "portable", "firmware", a.fw)
sys.path.insert(0, a.digiemu)
syxname = [f for f in os.listdir(FW) if f.endswith(".syx")][0]
os.environ.update({
    "DT2_SYX": os.path.join(FW, syxname), "DT2_SECTIONS": FW + "/sections", "DT2_SNAPSHOTS": FW + "/snapshots",
    "DT2_PLUSDRIVE": FW + "/plusdrive.img", "DT2_MAIN_IMG": FW + "/sections/section_3_MAIN_OS.bin",
    "DT2_DEVICES": FW + "/devices" if os.path.isdir(FW + "/devices") else os.path.join(a.digiemu, "devices"),
    "DIGIKIT_PIT3_PROBE": "1"})
os.chdir(FW)
_tk = types.ModuleType("tkinter")
_tk.Frame = type("Frame", (), {}); _tk.Tk = type("Tk", (), {})
sys.modules["tkinter"] = _tk
for _n in ("ttk", "messagebox", "filedialog", "font"):
    sys.modules["tkinter." + _n] = types.ModuleType("tkinter." + _n)
from unicorn import UC_HOOK_CODE
import emu.gui as G

SNAP = [os.path.join(dp, f) for dp, _, fs in os.walk(FW + "/snapshots") for f in fs if f == "gui.snap"][0]
os.makedirs(a.png, exist_ok=True)
MASTER = 0x8000ea70                    # the master pair: 32 frames of {L, R}, 32-bit
BUS = 0x80002160                       # the 12-channel bus the USB stream is built from, 48 bytes a frame
BUSFILL = fw(0x4007227c)               # where the master is copied into it
TONE_HZ = 220.0
AMP = 1 << 29
MONO = "master-mono:mode" in MAP


def png(path, fb):
    def ch(t_, d):
        return struct.pack(">I", len(d)) + t_ + d + struct.pack(">I", zlib.crc32(t_ + d) & 0xffffffff)
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(b"".join(big))) + ch(b"IEND", b""))


PLAN, t = [], 600
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def funclfo():
    at(1, "press", 1); at(20, "press", 23); at(20, "release", 23); at(5, "release", 1); at(200, "wait")
def turn(enc, n):
    for _ in range(abs(n)):
        at(8, "encoder", enc, 4 if n > 0 else -4)
    at(200, "wait")

funclfo()
at(80, "snap", "m1_first")
if MONO:
    funclfo(); funclfo()
    at(80, "snap", "m2_mono_page")
at(0, "settle", None)
at(400, "measure", "stereo")
if MONO:
    for name in ("mono", "swap"):
        turn(1, 1)
        at(0, "mode", name)
        at(0, "settle", None)
        at(400, "measure", name)
        at(0, "snap", "m3_" + name)
    funclfo()
    at(80, "snap", "m4_next")
STEPS = t + 20

state = {"n": 0, "ph": 0, "l": 0.0, "r": 0.0, "cnt": 0, "blocks": 0}
log, fails, levels, modes = [], [], [], []

_spin = G.spin
def spin(m, pc, *args, **kw):
    uc = m.uc
    if state["n"] == 0:
        def inject(u, ad, s_, d):                  # a tone into the left of the master, before the handlers
            out = bytearray()
            for i in range(32):
                v = int(AMP * math.sin(2 * math.pi * TONE_HZ * state["ph"] / 48000.0))
                state["ph"] += 1
                out += struct.pack(">ii", v, 0)
            u.mem_write(MASTER, bytes(out))
        uc.hook_add(UC_HOOK_CODE, inject, begin=MAP["core_render_master"], end=MAP["core_render_master"])

        def measure(u, ad, s_, d):
            v = [struct.unpack(">ii", u.mem_read(BUS + 48 * i, 8)) for i in range(32)]
            state["l"] += sum(x * x for x, _ in v) / 32.0
            state["r"] += sum(y * y for _, y in v) / 32.0
            state["cnt"] += 1
            state["blocks"] += 1
        uc.hook_add(UC_HOOK_CODE, measure, begin=BUSFILL, end=BUSFILL)
        uc.ctl_flush_tb()
    for when, act in PLAN:
        if when != state["n"]:
            continue
        k = act[0]
        if k in ("press", "release"):
            E.inbox.append((k, act[1], 0))
        elif k == "encoder":
            E.inbox.append(("encoder", act[1], act[2]))
        elif k == "snap":
            png(os.path.join(a.png, act[1] + ".png"), E.fb)
            log.append("snap %s" % act[1])
        elif k == "settle":
            state["l"] = state["r"] = 0.0
            state["cnt"] = 0
        elif k == "mode":
            modes.append((act[1], struct.unpack(">i", uc.mem_read(MAP["master-mono:mode"], 4))[0]))
        elif k == "measure":
            ref = AMP * AMP / 2.0
            db = lambda e: 10 * math.log10(max(e / max(state["cnt"], 1), 1e-9) / ref)
            levels.append((act[1], db(state["l"]), db(state["r"])))
            log.append("%-8s USB bus  L %7.2f dB   R %7.2f dB" % levels[-1])
            state["l"] = state["r"] = 0.0
            state["cnt"] = 0
    state["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if state["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
t0 = time.time()
E.run()
print("error:", E.error, "steps:", state["n"], "blocks measured:", state["blocks"], "%.0fs" % (time.time() - t0))
print("\n".join(log))
if E.error:
    fails.append("the firmware stopped: %s" % E.error)
if state["blocks"] < 100:
    fails.append("the 12-channel bus was never filled (%d blocks)" % state["blocks"])
want = {"stereo": (0.0, None), "mono": (-6.02, -6.02), "swap": (None, 0.0)}
for name, l, r in levels:
    for got, w, side in ((l, want[name][0], "L"), (r, want[name][1], "R")):
        if w is None:
            if got > -60:
                fails.append("%s: %s is %.2f dB, should be silent" % (name, side, got))
        elif abs(got - w) > 0.3:
            fails.append("%s: %s is %.2f dB, should be %.2f dB" % (name, side, got, w))
if len(levels) != (3 if MONO else 1):
    fails.append("only %d level measurements" % len(levels))
if MONO:
    print("modes: %s" % (modes,))
    if [v for _, v in modes] != [1, 2]:
        fails.append("knob A did not step the mode: %s" % (modes,))
print("\n%s" % ("PASS" if not fails else "FAIL\n  " + "\n  ".join(fails)))
sys.exit(1 if fails else 0)
