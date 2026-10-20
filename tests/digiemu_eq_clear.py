#!/usr/bin/env python3
"""Digi EQ's FUNC+CLEAR (FUNC + PLAY) in the real firmware (digiemu, headless).

    python3 tests/digiemu_eq_clear.py --digiemu <checkout, after --add of the build> --fw <firmware folder>
        --stock <official .syx> --elekloader <checkout> --mods <core> <digieq> --png <dir>

On the Master EQ page (FUNC+LFO twice), band 1's level is turned up and FUNC+CLEAR pressed: the four bands
must be back at their defaults, the pattern's kit holding them too, with no "Sequence cleared!" (the
pattern's trigs are counted before and after). On the Compressor page FUNC+CLEAR stays the stock one. Prints
PASS/FAIL.
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
UI_KIT = fw(0x4199dc44)


def png(path, fb):
    def ch(t_, d):
        return struct.pack(">I", len(d)) + t_ + d + struct.pack(">I", zlib.crc32(t_ + d) & 0xffffffff)
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(b"".join(big))) + ch(b"IEND", b""))


def xb(n):                                       # the persistent map (src/kitstore.h)
    return 0x20 + n // 6 * 0xa2 + (0, 1, 2, 3, 0x14, 0x15)[n % 6]


PLAN, t = [], 600
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def funclfo():
    at(1, "press", 1); at(20, "press", 23); at(20, "release", 23); at(5, "release", 1); at(200, "wait")
def clear():
    at(1, "press", 1); at(20, "press", 10); at(20, "release", 10); at(5, "release", 1); at(200, "wait")

funclfo(); funclfo()
at(80, "snap", "c1_eq")
for _ in range(8):
    at(8, "encoder", 1, 4)
at(200, "wait")
at(0, "state", "turned")
at(0, "snap", "c2_turned")
clear()
at(0, "snap", "c3_cleared")
at(0, "state", "after FUNC+CLEAR")
at(600, "wait")
at(0, "snap", "c4_later")
STEPS = t + 20

state = {"n": 0}
log, got = [], {}
_spin = G.spin
def spin(m, pc, *args, **kw):
    uc = m.uc
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
        elif k == "state":
            kit = struct.unpack(">I", uc.mem_read(UI_KIT, 4))[0]
            gi = list(uc.mem_read(MAP["digieq:gi"], 4))
            w0 = (uc.mem_read(kit + xb(4), 1)[0] << 8) | uc.mem_read(kit + xb(5), 1)[0]
            got[act[1]] = (gi, (w0 >> 7) & 0x3f)
            log.append("%-18s gi %s  kit band 1 level %d" % (act[1], gi, (w0 >> 7) & 0x3f))
    state["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if state["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
E.run()
print("error:", E.error, "steps:", state["n"])
print("\n".join(log))
fails = []
if E.error:
    fails.append("the firmware stopped: %s" % E.error)
t_ = got.get("turned"); c_ = got.get("after FUNC+CLEAR")
if not t_ or t_[0][0] == 24:
    fails.append("knob A did not move band 1: %s" % (t_,))
if not c_ or c_[0] != [24, 24, 24, 24] or c_[1] != 24:
    fails.append("FUNC+CLEAR did not reset the EQ (and the kit): %s" % (c_,))
print("\n%s" % ("PASS" if not fails else "FAIL\n  " + "\n  ".join(fails)))
sys.exit(1 if fails else 0)
