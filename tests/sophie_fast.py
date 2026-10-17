#!/usr/bin/env python3
"""SOPHIE as built here (tools/patches/digisophie-machine-pages.patch: its render lighter, its pages
machine-pages') against its author's: the same output.

    <python with digiemu's patched unicorn> tests/sophie_fast.py --digisophie <soejrd/digisophie checkout>
        --elekloader <elekloader checkout>

The checkout's sophie.c (as fetched) and a copy with the patch applied are built for the PC and for the
MCF54455 (m68k-linux-gnu-gcc, elekloader's flags). Checks:
  1. the patch applies to the checkout;
  2. random voices (every model, controls moving, SWEEP, re-triggers, the AMP gate putting voices to
     sleep and waking them, FOLD, block lengths 1..4096 with odd ones): every output sample and the
     whole voice state equal, block after block;
  3. the ColdFire builds: a voice's 32-frame block at the defaults and at full COLOR / METAL / FBK and
     SWEEP, in instructions and estimated cycles (tests/emu_macro.py's weights), before and after.
"""
import argparse, ctypes, os, random, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PATCH = os.path.join(HERE, "..", "tools", "patches", "digisophie-machine-pages.patch")
FAIL = []


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        FAIL.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--digisophie", required=True)
    ap.add_argument("--elekloader", required=True, help="for the SDK header digitakt.c includes since 1.2.0")
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="sophie_fast_")
    old, new = os.path.join(work, "old"), os.path.join(work, "new")
    for d in (old, new):                       # the whole checkout: the patch touches more than the engine
        shutil.copytree(a.digisophie, d, ignore=shutil.ignore_patterns(".git", "out", "release"))
    r = subprocess.run(["patch", "-p1", "-s", "-f", "-d", new], stdin=open(PATCH), capture_output=True, text=True)
    check(r.returncode == 0, "the patch applies to the checkout%s" % ("" if not r.returncode else ": " + r.stdout + r.stderr))
    if r.returncode:
        sys.exit(1)

    print("the same output")
    libs = []
    for d in (old, new):
        so = os.path.join(d, "s.so")
        subprocess.check_call(["gcc", "-std=c99", "-O2", "-shared", "-fPIC", "-I", d, os.path.join(d, "sophie.c"), "-o", so])
        libs.append(ctypes.CDLL(so))
    A, B = libs
    rnd = random.Random(2)
    blocks = diff = sdiff = 0
    for case in range(800):
        va, vb = ctypes.create_string_buffer(256), ctypes.create_string_buffer(256)
        A.ds_voice_init(va)
        B.ds_voice_init(vb)
        pr = [rnd.randrange(8, 3000), rnd.randrange(32), rnd.randrange(128), rnd.randrange(128),
              rnd.randrange(-64, 64), rnd.randrange(128), rnd.randrange(128), rnd.randrange(128)]
        for b in range(50):
            if rnd.random() < 0.2:
                pr[2], pr[3], pr[5] = rnd.randrange(128), rnd.randrange(128), rnd.randrange(128)
                pr[4], pr[0] = rnd.randrange(-64, 64), rnd.randrange(8, 20000)
            if rnd.random() < 0.05:
                pr[1] = rnd.randrange(32)
            p = ctypes.create_string_buffer(struct.pack("<HBBBbBBB", *pr), 9)   # struct ds_params
            trig = 1 if (b % 15 == 0 or rnd.random() < 0.03) else 0
            lvl = 0 if b > 25 and rnd.random() < 0.8 else 1 << 22
            ph = 0 if b > 25 else 2
            A.ds_voice_gate(va, lvl, ph)
            B.ds_voice_gate(vb, lvl, ph)
            n = 32 if case % 4 else rnd.choice([1, 2, 3, 7, 9, 31, 33, 64, 65, 100, 4096])
            oa, ob = (ctypes.c_int32 * n)(), (ctypes.c_int32 * n)()
            A.ds_voice_render(va, p, trig, oa, n)
            B.ds_voice_render(vb, p, trig, ob, n)
            A.ds_fold_block(oa, n, pr[7])
            B.ds_fold_block(ob, n, pr[7])
            blocks += 1
            diff += list(oa) != list(ob)
            sdiff += va.raw != vb.raw
    check(diff == 0 and sdiff == 0, "%d blocks: every sample and the voice state equal (%d / %d differ)"
          % (blocks, diff, sdiff))

    print("cost of a voice's 32-frame block, MCF54455 build (estimated cycles: tests/emu_macro.py)")
    sys.path.insert(0, HERE)
    import emu_macro as E
    rows = []
    for d in (old, new):
        obj, elf, binf = (os.path.join(d, n) for n in ("s.o", "s.elf", "s.bin"))
        subprocess.check_call(["m68k-linux-gnu-gcc"] + E.CFLAGS + ["-I", d, "-c", os.path.join(d, "sophie.c"), "-o", obj])
        subprocess.check_call(["m68k-linux-gnu-gcc"] + E.CFLAGS + ["-I", d, "-I", os.path.join(
            a.elekloader, "elekloader", "sdk", "include"), "-c", os.path.join(d, "digitakt.c"), "-o", obj + ".dt"]) \
            if d == new else None
        subprocess.check_call(["m68k-linux-gnu-ld", "-Ttext=0x%x" % E.TEXT, "-e", "ds_voice_render", "-o", elf, obj])
        subprocess.check_call(["m68k-linux-gnu-objcopy", "-O", "binary", elf, binf])
        syms = {}
        for line in subprocess.check_output(["m68k-linux-gnu-nm", elf], text=True).splitlines():
            ad, t, nm = line.split()
            syms[nm] = int(ad, 16)
        cf = E.CF(open(binf, "rb").read(), syms)
        row = {}
        for model, name in enumerate(["FUSE", "BOOM", "PIPE", "SHARD"]):
            for label, kw in (("default", (64, 64, 0, 64)), ("heavy", (127, 127, 40, 127))):
                cf.call("ds_voice_init", E.VOICE)
                cf.uc.mem_write(E.PRM, struct.pack(">HBBBbBBB", 200, model, kw[0], kw[1], kw[2], kw[3], 100, 0))
                cf.call("ds_voice_render", E.VOICE, E.PRM, 1, E.OUT, 32)
                for _ in range(4):
                    cf.call("ds_voice_render", E.VOICE, E.PRM, 0, E.OUT, 32)
                ins = cyc = 0
                for _ in range(16):
                    cf.call("ds_voice_render", E.VOICE, E.PRM, 0, E.OUT, 32, count=True)
                    ins += cf.count
                    cyc += cf.cycles
                row[(name, label)] = (ins / 16, cyc / 16)
        rows.append(row)
    check(True, "digitakt.c builds with m68k-linux-gnu-gcc -Werror (its s32 / u32 are int32_t / uint32_t)")
    for k in rows[0]:
        (i0, c0), (i1, c1) = rows[0][k], rows[1][k]
        print("  %-6s %-8s %6.0f -> %6.0f instructions, %6.0f -> %6.0f cycles (%+.0f %%)"
              % (k[0], k[1], i0, i1, c0, c1, 100.0 * (c1 - c0) / c0))
    check(all(rows[1][k][1] < rows[0][k][1] for k in rows[0]), "every case lighter")
    print()
    if FAIL:
        print("%d FAILED" % len(FAIL))
        sys.exit(1)
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
