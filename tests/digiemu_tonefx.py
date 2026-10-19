#!/usr/bin/env python3
"""Tone+FX (DT1 port) in digiemu, headless: the seven master pages, their knobs, the PERFORM pads, the
per-pattern store and the audio path, with a test tone in place of the (sample-less) render output.

    python3 tests/digiemu_tonefx.py --digiemu <checkout, after --add of the build> --fw <firmware folder name>
        --stock <official .syx> --elekloader <checkout> --mods <core> <tonefx> [<others>] --png <dir>

Run with digiemu's interpreter (its .venv, patched Unicorn). The tone (INJECT Hz, AMPBITS) is written into the
block's output half at the start of the effect chain (digiring_render_out, the first of it) and measured at its
end (the entry of digimeter_render_out, after the output ceiling), so each effect's audible result is checked
against its settings. With every effect off the chain must give back the tone bit for bit (true bypass).
"""
import argparse, collections, math, os, struct, sys, time, types, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--digiemu", required=True)
ap.add_argument("--fw", required=True)
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", required=True)
ap.add_argument("--mods", nargs="+", required=True)
ap.add_argument("--png", required=True)
ap.add_argument("--wav", default=None, help="write the chain's output to this WAV")
a = ap.parse_args()

sys.path.insert(0, a.elekloader)
from elekloader import syx as _syx, devices as _dev, elemod as _em, link as _link
_st = _syx.Syx.load(a.stock)
_d, _r = _dev.identify(_st.sha256)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from port_os import fw as _fw
OS = _r.version
def fw(x):
    return _fw(x, OS)
LINKED = _link.link([_em.load_any(p) for p in a.mods], _st.section(_d.main_section))
MAP = LINKED.map

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
from unicorn.m68k_const import UC_M68K_REG_A7
import emu.gui as G

INJECT = float(os.environ.get("INJECT", "440"))
AMP = 1 << int(os.environ.get("AMPBITS", "21"))
SNAP = [os.path.join(dp, f) for dp, _, fs in os.walk(FW + "/snapshots") for f in fs if f == "gui.snap"][0]
os.makedirs(a.png, exist_ok=True)
log = []
fails = []
PARAMS = ["digiring_on", "digiring_depth", "digiring_freq", "digieq_on", "digieq_lo", "digieq_hi",
          "digifold_on", "digifold_amount", "digifold_mode", "digimeter_on", "digicrush_on", "digicrush_mode",
          "digicrush_amount", "digicrush_tune", "digiwave_on", "digiwave_pmode", "digiwave_smode", "digiwave_page",
          "digiwave_speed", "digiwave_depth", "digiwave_mix", "digiwave_fb", "digimeter_l", "digimeter_r"]
def sym(n):
    return MAP.get(n, MAP.get("tonefx:" + n))


def png(path, fb):
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(b"".join(big))) + chunk(b"IEND", b""))


# ---- the plan: (step, action) ----
PLAN = []
t = 600
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def tap(code, hold=20, gap=60):
    at(1, "press", code); at(hold, "release", code); at(gap, "wait")
def turn(k, n):                                   # n notches of knob k (0..7 = A..H, 8 = LEVEL), 4 counts a notch
    for _ in range(abs(n)):
        at(4, "encoder", 1 + k, 4 if n > 0 else -4)
    at(60, "wait")
def funclfo():
    at(1, "press", 1); at(20, "press", 23); at(20, "release", 23); at(5, "release", 1); at(120, "wait")
FUNC, LFO, UP, LEFT, RIGHT, NO, PLAY, STOP = 1, 23, 14, 16, 17, 13, 10, 11

at(0, "cctab", "")
if os.environ.get("ONLY_CCTAB"):
    PLAN[:] = PLAN[:1]; t = 602
at(0, "level", "defaults (ring, EQ, fold on)"); at(0, "bypass", "defaults")
at(0, "snap", "00_main")
funclfo(); at(0, "snap", "01_compressor"); at(0, "page", "compressor")
tap(RIGHT); tap(RIGHT); at(0, "snap", "02_ext_mixer")
tap(RIGHT, gap=120); at(0, "snap", "03_ring"); at(0, "page", "ring")
turn(0, 1); turn(4, 40); turn(5, 12); at(150, "params", "ring on, depth 40+, freq"); at(0, "snap", "04_ring_on")
at(0, "level", "ring_on"); at(0, "encid", "")
turn(0, -1); at(150, "params", "ring off"); at(0, "level", "ring_off"); at(0, "bypass", "ring off again")
tap(RIGHT, gap=120); at(0, "snap", "05_fold"); at(0, "page", "fold")
turn(0, 1); turn(4, 60); turn(3, 70); at(150, "params", "fold on"); at(0, "snap", "06_fold_on"); at(0, "level", "fold_on")
turn(0, -1); at(150, "wait")
tap(RIGHT, gap=120); at(0, "snap", "07_tilt"); at(0, "page", "tilt")
turn(0, 1); turn(4, -30); turn(7, 30); at(150, "params", "tilt"); at(0, "snap", "08_tilt_on"); at(0, "level", "tilt_lo-30_hi+30")
turn(0, -1); at(150, "wait")
tap(RIGHT, gap=120); at(0, "snap", "09_crush"); at(0, "page", "crush")
turn(0, 1); turn(4, 80); at(150, "params", "crush BITS"); at(0, "snap", "10_crush_bits"); at(0, "level", "crush_bits")
turn(3, 60); at(150, "params", "crush mode"); at(0, "snap", "11_crush_mode")
turn(0, -1); at(150, "wait")
tap(RIGHT, gap=120); at(0, "snap", "12_pitch"); at(0, "page", "pitch")
turn(0, 1); turn(5, 30); at(300, "params", "pitch WOB"); at(0, "snap", "13_pitch_wob"); at(0, "level", "pitch_wob")
turn(3, 1); at(300, "params", "pitch SHFT"); at(0, "snap", "14_pitch_shft"); at(0, "level", "pitch_shft")
tap(RIGHT, gap=120); at(0, "snap", "15_stereo"); at(0, "page", "stereo")
turn(3, 1); at(300, "params", "stereo PAN"); at(0, "snap", "16_stereo_pan"); at(0, "level", "stereo_pan")
turn(0, -1); at(300, "wait")
at(400, "wait"); at(0, "bypass", "reset"); at(300, "wait"); at(0, "bypass", "everything off: bit-exact")
tap(UP, gap=120); at(0, "snap", "17_perform"); at(0, "page", "perform"); at(0, "prepad", "")
at(1, "press", 33); at(400, "params", "pad trig 10 held (ring, full)"); at(0, "snap", "18_pad_held"); at(0, "level", "pad_held")
at(0, "expect", ("digiring_depth", 127)); at(0, "expect", ("digiring_on", 1))
at(1, "release", 33); at(400, "params", "pad released"); at(0, "expect", ("digiring_on", 0))
at(0, "expect", ("digiring_depth", None)); at(0, "snap", "19_pad_released")
tap(UP, gap=120); at(0, "snap", "20_back_from_perform"); at(0, "page", "back")
tap(RIGHT, gap=120); at(0, "snap", "21_wrap_compressor"); at(0, "page", "wrapped")
at(0, "store", "")
at(300, "wait"); at(0, "snap", "22_perform")


def ptn(k):                                       # PTN + trig k: that pattern (stopped: at once)
    at(1, "press", 3); at(20, "press", 23 + k); at(20, "release", 23 + k); at(5, "release", 3); at(200, "wait")


def store_part():
    # on the PERFORM page: PTN + trig changes the pattern (the trigs are pads only on their own)
    at(0, "mark", "a01"); ptn(2); at(0, "snap", "23_a02_from_perform")
    at(0, "params", "pattern A02 (new: adopts the live values)"); at(0, "store", "")
    turn(0, -10); at(150, "params", "A02: ring depth down (PERFORM knob A)"); at(0, "mark", "a02"); at(0, "store", "")
    ptn(1); at(0, "snap", "24_back_on_a01"); at(0, "params", "back on A01 (its own values)"); at(0, "store", "")
    at(0, "check_a01", "")
    ptn(2); at(0, "params", "A02 again"); at(0, "check_a02", "")


if os.environ.get("NO_ONLY"):                     # NO on a stock master page and on ours
    PLAN.clear(); t = 600
    funclfo(); at(300, "wait"); tap(NO, gap=200); at(0, "snap", "n1_comp_no")
    funclfo(); tap(LEFT, gap=300); at(0, "snap", "n2_perform"); tap(NO, gap=200); at(0, "snap", "n3_perform_no")
    funclfo(); tap(RIGHT); tap(RIGHT); tap(RIGHT, gap=300); at(0, "snap", "n4_ring"); tap(NO, gap=200)
    at(0, "snap", "n5_ring_no")
    funclfo(); tap(RIGHT, gap=10); tap(RIGHT, gap=10); tap(RIGHT, gap=10); tap(NO, gap=200)
    at(0, "snap", "n6_no_during_title")
elif os.environ.get("STORE_ONLY"):                # just the per-pattern store part
    PLAN.clear(); t = 600; funclfo(); tap(LEFT, gap=120)
    store_part()
else:
    store_part()
    tap(NO, gap=120); at(0, "snap", "25_closed")
    at(0, "cc", "")
STEPS = t + 2

state = {"n": 0, "ph": 0, "blocks": 0, "insn": 0, "sq_in": 0.0, "sq_out": 0.0, "diff": 0, "wav": []}
_spin = G.spin
lo_fx, hi_fx = sym("digimeter_render_in"), sym("digictl_mod_in")      # (incl. master.c)            # the effect stages, meter .. wave
lo_ctl, hi_ctl = sym("digictl_mod_in"), sym("dc_draw")         # mod_in + limit_out


def spin(m, pc, *args, **kw):
    uc = m.uc
    n = state["n"]
    if n == 0:
        def inject(u, ad, s, d):                  # the stage's entry: the tone replaces the master (8 bits hot)
            m_ = struct.unpack(">I", u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 4))[0]
            if "prev" in state:                   # the last block's USB bus (0x80002160, 48 bytes a frame, L R first)
                bus = u.mem_read(0x80002160, 48 * 32)
                lr = [struct.unpack(">2i", bus[48 * f: 48 * f + 8]) for f in range(32)]
                got = [x >> 8 for fr in lr for x in fr]
                state["usb_n"] = state.get("usb_n", 0) + 1
                if got == list(state["prev"]):
                    state["usb_ok"] = state.get("usb_ok", 0) + 1
            out, hot = bytearray(), bytearray()
            for i in range(32):
                ph = 2 * math.pi * INJECT * state["ph"] / 48000.0
                state["ph"] += 1
                l, r = int(AMP * math.sin(ph)), int(AMP * math.cos(ph))
                out += struct.pack(">ii", l, r)
                hot += struct.pack(">ii", l << 8, r << 8)
            u.mem_write(m_, bytes(hot))
            state["inbuf"] = bytes(out)
            state["buf"] = sym("tfx_blk")
            state["blocks"] += 1
        uc.hook_add(UC_HOOK_CODE, inject, begin=sym("tonefx_master"), end=sym("tonefx_master"))
        def measure(u, ad, s, d):                 # the last stage's entry: what the chain made of it
            if "buf" not in state:
                return
            o = bytes(u.mem_read(state["buf"], 256))
            v = struct.unpack(">64i", o)
            iv = struct.unpack(">64i", state["inbuf"])
            state["prev"] = v
            state["sq_out"] = state["sq_out"] * 0.98 + 0.02 * sum(x * x for x in v) / 64.0
            state["sq_in"] = state["sq_in"] * 0.98 + 0.02 * sum(x * x for x in iv) / 64.0
            if o != state["inbuf"]:
                state["diff"] += 1
            state["checked"] = state.get("checked", 0) + 1
            if a.wav:
                state["wav"].append(o)
        uc.hook_add(UC_HOOK_CODE, measure, begin=sym("digimeter_render_out"), end=sym("digimeter_render_out"))
        def count(u, ad, s, d):
            state["insn"] += 1
        uc.hook_add(UC_HOOK_CODE, count, begin=lo_fx, end=hi_fx - 1)
        uc.hook_add(UC_HOOK_CODE, count, begin=lo_ctl, end=hi_ctl - 1)
        def encid(u, ad, s, d):
            ev = struct.unpack(">I", u.mem_read(u.reg_read(UC_M68K_REG_A7) + 8, 4))[0]
            state.setdefault("encs", []).append(struct.unpack(">ii", u.mem_read(ev + 12, 8)))
        uc.hook_add(UC_HOOK_CODE, encid, begin=sym("digictl_enc"), end=sym("digictl_enc"))
        def keyid(u, ad, s, d):
            ev = struct.unpack(">I", u.mem_read(u.reg_read(UC_M68K_REG_A7) + 8, 4))[0]
            state.setdefault("keys", []).append(struct.unpack(">ii", u.mem_read(ev + 12, 8)))
        uc.hook_add(UC_HOOK_CODE, keyid, begin=sym("digictl_key"), end=sym("digictl_key"))
        if os.environ.get("TRACE"):
            def tr(name):
                def f(u, ad, s, d):
                    sp = u.reg_read(UC_M68K_REG_A7)
                    args = struct.unpack(">4I", u.mem_read(sp + 4, 16))
                    print("TRACE %s step %d args %s" % (name, state["n"], [hex(x) for x in args]), flush=True)
                return f
            for nm, ad in (("op_new", fw(0x400d4180)), ("mdraw", sym("dc_draw")), ("stock_draw", fw(0x400395dc)),
                           ("shown", sym("core_mpage_shown"))):
                uc.hook_add(UC_HOOK_CODE, tr(nm), begin=ad, end=ad)
    def rd(name):
        return struct.unpack(">i", uc.mem_read(sym(name), 4))[0]
    for when, act in PLAN:
        if when != n:
            continue
        k = act[0]
        if k in ("press", "release"):
            E.inbox.append((k, act[1], 0))
        elif k == "encoder":
            E.inbox.append(("encoder", act[1], act[2]))
        elif k == "snap":
            png(os.path.join(a.png, act[1] + ".png"), E.fb)
            log.append("snap %-22s step %d" % (act[1], n))
        elif k == "params":
            log.append("%-34s %s" % (act[1], " ".join("%s=%d" % (p.split("_", 1)[1], rd(p)) for p in PARAMS)))
        elif k == "level":
            gin = 10 * math.log10(max(state["sq_in"], 1e-9))
            gout = 10 * math.log10(max(state["sq_out"], 1e-9))
            log.append("%-34s out - in = %+6.2f dB   (blocks %d, chain %d instructions a block; USB bus = chain"
                       " output in %d of %d blocks)" % (
                act[1], gout - gin, state["blocks"], state["insn"] // max(1, state["blocks"]),
                state.get("usb_ok", 0), state.get("usb_n", 0)))
            if state.get("usb_n") and state.get("usb_ok", 0) < state["usb_n"] - 2:
                fails.append("USB bus: %s" % act[1])
            state["usb_ok"] = state["usb_n"] = 0
            state["insn"] = 0; state["blocks"] = 0
        elif k == "bypass":
            c = state.get("checked", 0)
            log.append("%-34s %d of %d recent blocks changed by the chain" % (act[1], state["diff"], c))
            if state["diff"] and act[1].startswith("everything off"):
                fails.append("bypass: " + act[1])
            state["diff"] = 0; state["checked"] = 0
        elif k == "encid":
            log.append("encoder events seen by digictl_enc (id, delta): %s" % (state.get("encs", [])[:60],))
        elif k == "page":
            v = struct.unpack(">I", uc.mem_read(sym("dc_view"), 4))[0]
            if v:
                vec = struct.unpack(">2I", uc.mem_read(v + 124, 8))
                nk = (vec[1] - vec[0]) // 4
                kinds = struct.unpack(">%di" % nk, uc.mem_read(vec[0], 4 * nk)) if 0 < nk < 32 else ()
                idx = struct.unpack(">i", uc.mem_read(v + 144, 4))[0]
                log.append("page %-10s view %08x kinds %s index %d" % (act[1], v, kinds, idx))
            else:
                log.append("page %-10s no master view drawn yet" % act[1])
        elif k == "store":
            kit = struct.unpack(">I", uc.mem_read(fw(0x4199dc44), 4))[0]
            blk = bytes(uc.mem_read(kit + 0x530, 34))
            log.append("kit %08x +0x530: %s" % (kit, blk.hex()))
        elif k == "cctab":                         # the firmware's CC -> parameter tables (built at boot, in RAM)
            for nm, ad, cnt in (("audio tracks", 0x4199f85c, 128), ("MIDI tracks", 0x4199f050, 128),
                                ("auto channel", 0x419a0068, 128)):
                tb = struct.unpack(">%di" % cnt, uc.mem_read(fw(ad), 4 * cnt))
                log.append("CC used by %-13s %s" % (nm, [i for i, v in enumerate(tb) if v]))
            tb = struct.unpack(">32i", uc.mem_read(fw(0x419a02e8), 128))
            log.append("CC 16..23 per machine (4 machines x 8): %s" % (tb,))
            ours = [8, 11, 36, 37, 40, 41, 67, 68, 69, 96] + list(range(20, 32))
            log.append("Tone+FX CCs: %s" % ours)
        elif k == "prepad":
            state["pre_pad"] = rd("digiring_depth")
        elif k == "expect":
            n_, v_ = act[1]
            if v_ is None:                        # back to what it was before the pad
                v_ = state.get("pre_pad", rd(n_))
            if rd(n_) != v_:
                fails.append("%s = %d, expected %d" % (n_, rd(n_), v_))
                log.append("FAIL %s = %d, expected %d" % (n_, rd(n_), v_))
        elif k == "mark":
            state["mark_" + act[1]] = rd("digiring_depth")
        elif k == "check_a01":
            state["a01"] = rd("digiring_depth")
        elif k == "check_a02":
            a01, a02 = state.get("a01"), rd("digiring_depth")
            m1, m2 = state.get("mark_a01"), state.get("mark_a02")
            ok = a01 == m1 and a02 == m2 and m1 != m2
            log.append("per-pattern store: ring depth set A01 %s / A02 %s, read back A01 %s / A02 %s: %s"
                       % (m1, m2, a01, a02, "ok" if ok else "FAIL"))
            if not ok:
                fails.append("per-pattern store")
        elif k == "cc":
            log.append("keys seen by digictl_key (id, flags): %s" % (state.get("keys", []),))
    state["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if state["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
t0 = time.time()
E.run()
print("error:", E.error, "steps:", state["n"], "%.0fs" % (time.time() - t0))
print("\n".join(log))
if a.wav and state["wav"]:
    import wave
    w = wave.open(a.wav, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes(b"".join(struct.pack("<%dh" % 64, *[max(-32768, min(32767, x >> 8)) for x in struct.unpack(">64i", o)])
                           for o in state["wav"]))
    w.close()
print("FAILED: " + "; ".join(fails) if fails or E.error else "ALL CHECKS PASSED")
