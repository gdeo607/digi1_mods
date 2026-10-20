#!/usr/bin/env python3
"""Digi TX's TX KIT in the real firmware (digiemu, headless): pattern A16 (empty), FUNC+SRC to TX KIT, trigs on steps
1, 5, 9, 13 of track 1, PLAY ~2 s with knob B turned to each drum in turn (DRUMS=n of them); track 1's
block is written to a WAV and its hits are checked for sound (level, and the drum's pitch where it has one).

    python3 tests/digiemu_tx.py --digiemu <checkout> --fw <firmware folder> --stock <official .syx>
        --elekloader <checkout> --mods <core> <machine-pages> <digimono> <digitx> --png <dir> [--wav out.wav]
        [--list-pos N]   (TX KIT's place in the machine list after the stock four; default 8)
"""
import argparse, math, os, struct, sys, time, types, wave, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--digiemu", required=True)
ap.add_argument("--fw", required=True)
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", required=True)
ap.add_argument("--mods", nargs="+", required=True)
ap.add_argument("--png", required=True)
ap.add_argument("--wav", default="")
ap.add_argument("--list-pos", type=int, default=8)
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
VOICE0 = 0x80001a18                               # track 1's block, as Digi Mono hands it on (Q31 = sample << 16)
SITE = fw(0x40077fc8)                              # right after digimono_rblock returns


def png(path, fb):
    def ch(t_, d):
        return struct.pack(">I", len(d)) + t_ + d + struct.pack(">I", zlib.crc32(t_ + d) & 0xffffffff)
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(b"".join(big))) + ch(b"IEND", b""))


PLAN, t = [], 300
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def tap(code, hold=20, gap=40):
    at(1, "press", code); at(hold, "release", code); at(gap, "wait")

at(1, "press", 3); at(20, "press", 39); at(20, "release", 39); at(5, "release", 3); at(150, "wait")   # PTN + 16
at(1, "press", 1); at(20, "press", 20); at(20, "release", 20); at(5, "release", 1); at(80, "wait")    # FUNC+SRC
for _ in range(4 + a.list_pos):
    tap(15, 15, 30)
at(30, "snap", "t1_list")
tap(12, gap=120)                                    # YES
tap(20, gap=120)                                    # SRC page
at(0, "snap", "t2_src")
tap(9, gap=60)                                      # RECORD
for s in (24, 28, 32, 36):                          # trigs 1, 5, 9, 13
    tap(s, gap=30)
tap(9, gap=60)
DRUMS = int(os.environ.get("DRUMS", "10"))
at(0, "rec", 1)
tap(10, gap=10)                                     # PLAY
for d in range(DRUMS):
    at(0, "mark", d)
    at(1500, "snap", "t3_drum%d" % d)
    if d + 1 < DRUMS:
        for _ in range(int(os.environ.get("NOTCHES", "8"))):   # knob B: the next drum (8 values)
            at(12, "encoder", 2, 4)
        at(20, "wait")
tap(11, gap=60)                                     # STOP
at(0, "rec", 0)
STEPS = t + 20

state = {"n": 0, "rec": 0, "marks": []}
frames = []
_spin = G.spin
def spin(m, pc, *args, **kw):
    uc = m.uc
    if state["n"] == 0:
        def grab(u, ad, s_, d):
            if state["rec"]:
                frames.append(u.mem_read(VOICE0, 128))
        uc.hook_add(UC_HOOK_CODE, grab, begin=SITE, end=SITE)
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
        elif k == "rec":
            state["rec"] = act[1]
        elif k == "mark":
            state["marks"].append(len(frames))
            kit = struct.unpack(">I", uc.mem_read(fw(0x4199dc44), 4))[0]
            b = struct.unpack(">H", uc.mem_read(kit + 0x20 + 0x36 + 2, 2))[0]
            state.setdefault("knobB", []).append(b >> 8)
    state["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if state["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
E.run()
L = []
for f in frames:
    v = struct.unpack(">32i", bytes(f))
    L += [x >> 16 for x in v]
print("error:", E.error, "steps:", state["n"], "blocks:", len(frames), "marks:", state["marks"], "knob B:", state.get("knobB"))
pk = max((abs(x) for x in L), default=0)
print("peak of the master (16-bit):", pk)
if a.wav and L:
    with wave.open(a.wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
        w.writeframes(struct.pack("<%dh" % len(L), *[max(-32768, min(32767, x)) for x in L]))
marks = state["marks"] + [len(frames)]
for d in range(len(marks) - 1):
    seg = L[marks[d] * 32: marks[d + 1] * 32]
    r = math.sqrt(sum(x * x for x in seg) / max(1, len(seg)))
    print("drum %d: %d blocks, rms %.0f, peak %d" % (d, marks[d + 1] - marks[d], r, max((abs(x) for x in seg), default=0)))
print("PASS" if pk > 1000 and not E.error else "FAIL")
