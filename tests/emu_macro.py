#!/usr/bin/env python3
"""Digi Mono's MACRO engines (mods/digimono/macro.c) built for the DT1's CPU, run on an emulated
ColdFire V4e.
    python3 tests/emu_macro.py            (needs m68k-linux-gnu-gcc/ld/nm, gcc, unicorn, capstone)
   The unicorn must emulate the EMAC as the ColdFire manual describes (digiemu's build does; a stock
   unicorn 2.1.4 does not, and every MACRO filter engine then differs).
1. Builds macro.c with elekloader's flags for the DT1 and checks the object needs nothing from
   outside (no libgcc, no memset).
2. Bit-exact: random voices (engine, knobs, pitch, re-trigs) rendered block by block on the emulated
   ColdFire and by a PC build of the same file: every sample must be the same. So what
   tests/macro_vs_plaits.py measures on the PC is what the unit plays.
3. Cost per 32-frame block and voice: instructions, and estimated cycles (divide 35, multiply 4, an
   instruction that reads or writes memory 2, a taken branch 2, others 1). The stock render is about
   84,000 instructions a block; a block is 166,667 cycles at 250 MHz.
"""
import ctypes, os, random, struct, subprocess, sys, tempfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import *
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "mods", "digimono", "macro.c")
INC = os.path.join(HERE, "..", "mods", "digimono")
CFLAGS = ["-mcpu=54455", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib", "-fno-pic", "-fno-pie",
          "-fomit-frame-pointer", "-Wall", "-Werror"]
TEXT = 0x40100000
RAM, RAMSZ = 0x47b00000, 0x00100000
STACK = RAM + 0xf0000
SENT = RAM + 0x100
VOICE, PRM, OUT = RAM + 0x1000, RAM + 0x4000, RAM + 0x5000
VOICE_BYTES = 0x3000
FAIL = []
ENGINES = ["WSH", "FM", "NOISE", "PART", "BD", "SD", "HH"]  # macro.h order


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        FAIL.append(what)


def build_cf():
    work = tempfile.mkdtemp(prefix="macro_cf_")
    obj, elf, binf = (os.path.join(work, n) for n in ("macro.o", "macro.elf", "macro.bin"))
    subprocess.check_call(["m68k-linux-gnu-gcc"] + CFLAGS + ["-I", INC, "-c", SRC, "-o", obj])
    und = subprocess.check_output(["m68k-linux-gnu-nm", "-u", obj], text=True).split()
    check(not und, "the ColdFire object needs nothing from outside (undefined: %s)" % (und or "none"))
    subprocess.check_call(["m68k-linux-gnu-ld", "-Ttext=0x%x" % TEXT, "-e", "macro_render", "-o", elf, obj])
    subprocess.check_call(["m68k-linux-gnu-objcopy", "-O", "binary", elf, binf])
    syms = {}
    for line in subprocess.check_output(["m68k-linux-gnu-nm", elf], text=True).splitlines():
        a, t, n = line.split()
        syms[n] = int(a, 16)
    print("  code + tables: %d bytes" % os.path.getsize(binf))
    return open(binf, "rb").read(), syms


def build_pc():
    so = os.path.join(tempfile.mkdtemp(prefix="macro_pc_"), "macro.so")
    subprocess.check_call(["gcc", "-O2", "-shared", "-fPIC", "-I", INC, SRC, "-o", so])
    return ctypes.CDLL(so)


class CF:
    def __init__(self, image, syms):
        self.uc = uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        uc.mem_map(TEXT, (len(image) + 0xffff) & ~0xffff)
        uc.mem_write(TEXT, image)
        uc.mem_map(RAM, RAMSZ)
        uc.mem_write(SENT, b"\x4e\x71")
        self.syms = syms
        self.counting = False
        self.count = self.cycles = 0
        self.cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
        self.cost = {}
        self.image = image
        self.last = None
        uc.hook_add(UC_HOOK_CODE, self._hook)

    def _weight(self, addr):
        w = self.cost.get(addr)
        if w is None:
            off = addr - TEXT
            ins = next(self.cs.disasm(self.image[off:off + 10], addr), None)
            m = ins.mnemonic if ins else ""
            ops = ins.op_str if ins else ""
            if m.startswith("div") or m.startswith("rem"):
                w = 35
            elif m.startswith("mul") or m.startswith("mac"):
                w = 4
            elif "(" in ops or m.startswith("movem"):
                w = 2
            else:
                w = 1
            self.cost[addr] = w
        return w

    def _hook(self, uc, addr, size, data):
        if self.counting:
            self.count += 1
            self.cycles += self._weight(addr)
            if self.last is not None and addr != self.last[0] + self.last[1]:
                self.cycles += 1              # a taken branch
            self.last = (addr, size)

    def call(self, fn, *args, count=False):
        uc = self.uc
        sp = STACK - 4 * (len(args) + 1)
        uc.mem_write(sp, struct.pack(">I", SENT) + b"".join(struct.pack(">I", a & 0xffffffff) for a in args))
        uc.reg_write(UC_M68K_REG_A7, sp)
        self.count = self.cycles = 0
        self.last = None
        self.counting = count
        uc.emu_start(self.syms[fn], SENT)
        self.counting = False
        return uc.reg_read(UC_M68K_REG_D0)

    def render(self, prm, inc, n, count=False):
        self.uc.mem_write(PRM, bytes(prm) + b"\0")
        self.call("macro_render", VOICE, PRM, inc, OUT, n, count=count)
        return bytes(self.uc.mem_read(OUT, 2 * n))


def knob_of(engine):
    return engine * 8 + 4                # knob B: 16 zones of 8 values


def pitch_inc(note):
    return int(440.0 * 2 ** ((note - 69) / 12.0) / 48000.0 * 2 ** 32) & 0xffffffff


def main():
    print("build")
    image, syms = build_cf()
    pc = build_pc()
    cf = CF(image, syms)

    print("bit-exact against the PC build")
    rnd = random.Random(11)
    same, blocks = True, 0
    for case in range(60):
        hv = ctypes.create_string_buffer(VOICE_BYTES)
        pc.macro_init(hv)
        cf.call("macro_init", VOICE)
        prm = [knob_of(rnd.randrange(len(ENGINES)))] + [rnd.randrange(128) for _ in range(6)]
        for b in range(20):
            if b % 7 == 0:
                pc.macro_trig(hv)
                cf.call("macro_trig", VOICE)
            if rnd.random() < 0.3:
                prm[1 + rnd.randrange(4)] = rnd.randrange(128)
            inc = pitch_inc(rnd.uniform(12, 120)) if rnd.random() < 0.9 else rnd.getrandbits(32)
            n = 32 if rnd.random() < 0.8 else rnd.randrange(1, 33)
            a = cf.render(prm, inc, n)
            out = (ctypes.c_int16 * n)()
            pc.macro_render(hv, (ctypes.c_uint8 * 7)(*prm), ctypes.c_uint32(inc), out, n)
            h = b"".join(struct.pack(">h", x) for x in out)
            if a != h and same:
                print("  first difference: case %d block %d prm %s inc %#x n %d" % (case, b, prm, inc, n))
                same = False
            blocks += 1
    check(same, "%d blocks: every sample equal to the PC build's" % blocks)

    print("cost per voice and 32-frame block (note 48; the average of 16 blocks after 4)")
    print("  %-6s %22s %22s" % ("", "light (knobs at 64)", "heavy"))
    worst = 0
    for e, name in enumerate(ENGINES):
        row = []
        for prm in ([knob_of(e), 64, 64, 64, 0, 0, 0], [knob_of(e), 127, 127, 127, 64, 0, 0]):
            cf.call("macro_init", VOICE)
            cf.call("macro_trig", VOICE)
            for _ in range(4):
                cf.render(prm, pitch_inc(48), 32)
            ins = cyc = 0
            for _ in range(16):
                cf.render(prm, pitch_inc(48), 32, count=True)
                ins += cf.count
                cyc += cf.cycles
            row.append((ins / 16, cyc / 16))
        worst = max(worst, row[1][1])
        print("  %-6s %7.0f ins %7.0f cyc  %7.0f ins %7.0f cyc  = %4.1f %% of a block's cycles"
              % (name, row[0][0], row[0][1], row[1][0], row[1][1], 100 * row[1][1] / 166667))
    print()
    if FAIL:
        print("%d FAILED" % len(FAIL))
        sys.exit(1)
    print("ALL EMULATOR CHECKS PASSED")


if __name__ == "__main__":
    main()
