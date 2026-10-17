"""DH-101's firmware hooks, run in the linked OS 1.54 image under Unicorn (ColdFire V4e): the LFO DEST
storage forms (save / load, both LFOs), the LFO page's list order (0x40078f44) and its names (machine-pages,
from DH-101's page).
Usage: tests/digi101_hooks_fw.py STOCK.syx mod.elemod ... (the same mods as the firmware build)."""
import sys, os, struct
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.environ.get('ELEKLOADER', os.path.join(ROOT, 'out/dev/tools/elekloader')))
# Unicorn 2.1.4 with digiemu's EMAC patches (tools/dev.sh setup puts it in digiemu's venv: run with that python)
if os.environ.get('UNICORN_PY'):
    sys.path.insert(0, os.environ['UNICORN_PY'])
from elekloader import formats, elemod, link
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import *

stock, dev, rel = formats.load(sys.argv[1])
img0 = formats.main_image(stock, dev)
mods = [elemod.load_any(p) for p in sys.argv[2:]]
L = link.link(mods, img0)
img, lay, sym = L.image, L.layout, L.map
base = dev.main_load
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
def mapr(lo, size):
    lo &= ~0xfff; size = (size + 0x1fff) & ~0xfff
    if 0x40000000 <= lo < 0x42400000 and lo != 0x40000000:
        return                                     # inside the big map already
    uc.mem_map(lo, size)
mapr(0x40000000, 0x02400000)                       # flash image and the RAM (tables at 0x4199...., 0x421f....)
uc.mem_write(base, img[:0x02000000])
ddr0, ddr1 = lay['ddr']
mapr(ddr0, lay['bss'][1] - ddr0 + 0x1000)
uc.mem_write(ddr0, img[lay['run_load'] - base: lay['run_load'] - base + (ddr1 - ddr0)])
mapr(0x80000000, 0x20000)
mapr(0x7f000000, 0x10000)
STOP = 0x7f00f000
uc.mem_write(STOP, b'\x4e\x71' * 4)
fails = 0
def check(ok, msg):
    global fails
    print(('PASS ' if ok else 'FAIL ') + msg)
    fails += not ok

def call(fn, args, ret, regs=None):
    sp = 0x7f008000
    stack = struct.pack('>I', ret) + b''.join(struct.pack('>i', a) for a in args)
    sp -= len(stack)
    uc.mem_write(sp, stack)
    uc.reg_write(UC_M68K_REG_A7, sp)
    for r, v in (regs or {}).items():
        uc.reg_write(r, v & 0xffffffff)
    # the return address is in the firmware: stop on reaching it
    uc.emu_start(fn, ret, count=20000)
    d0 = uc.reg_read(UC_M68K_REG_D0)
    return d0 - (1 << 32) if d0 & 0x80000000 else d0

ACTIVE = 0x4197c6b4
ctm = sym.get('core_track_machine')
def set_track(t, machine):
    uc.mem_write(ACTIVE, struct.pack('>I', t))
    uc.mem_write(ctm + t, bytes([machine]))

# ---- slot -> id (0x40078f44(slot, machine)): the DEST list's order on a DH-101 track ----
want = [108, 110, 111, 112, 113, 115, 109, 114]
got = [call(0x40078f44, [s_, 29], 0x400113be) for s_ in range(17, 25)]
check(got == want, 'DEST list, DH-101 track: slots 17..24 -> %s' % got)
# elsewhere the stock answer (its RAM tables are boot-filled, so zero here: compared with an unknown caller)
stock_ids = lambda m, ss: [call(0x40078f44, [s_, m], 0x40001000) for s_ in ss]
check([call(0x40078f44, [s_, 29], 0x40065d94) for s_ in range(17, 25)] == stock_ids(29, range(17, 25)),
      'DEST box: the stock answer for each slot')
check([call(0x40078f44, [s_, 0], 0x400113be) for s_ in range(17, 25)] == stock_ids(0, range(17, 25)),
      'another machine: the stock answer')
check(call(0x40078f44, [26, 29], 0x400113be) == stock_ids(29, [26])[0], 'Filter Freq: the stock answer')
# every id listed is an LFO1 and LFO2 destination (0x400790ea, the flags the list filters on)
check(all(call(0x400790ea, [i], 0x40001000) & 0xe00 == 0xe00 for i in want), 'all eight pass the list filter')
# ---- id -> slot (0x40078d80(id)): stock, so DEST holds 18 / 22 / 23 ----
check([call(0x40078d80, [i], 0x400116ce) for i in (109, 113, 114)] == [18, 22, 23], 'FREQ 18, PWM Source 22, RESO 23')

# ---- the storage forms ----
A_STORE, A_RUN = 0x80001000, 0x80002000
save_tbl, load_tbl = 0x401ac4d4, 0x401ac58c
def run_hook(name, store_bytes, run_bytes, a1):
    uc.mem_write(A_STORE, store_bytes)
    uc.mem_write(A_RUN, run_bytes)
    return call(sym[name], [], STOP, {UC_M68K_REG_A1: a1,
                                      UC_M68K_REG_A2: A_STORE if 'save' in name else A_RUN,
                                      UC_M68K_REG_A3: A_RUN if 'save' in name else A_STORE})
def rec(off, byte):
    b = bytearray(0x100)
    b[off] = byte & 0xff
    return bytes(b)
def tbl(t, i):
    return struct.unpack('>i', img[t - base + 4 * i:t - base + 4 * i + 4])[0]
for slot in (26, 18, 22, 23, -1):
    got = run_hook('digi101_save_hook', rec(42, slot), bytes(0x100), save_tbl)
    check(slot < 0 or got == tbl(save_tbl, slot),
          'save DEST slot %d -> storage form %d, the stock table' % (slot, got))
for form, want in ((25, tbl(load_tbl, 25)), (51, tbl(load_tbl, 51)), (18, tbl(load_tbl, 18)),
                   (0x60, 18), (0x61, 23), (0x62, tbl(load_tbl, 51)), (0x66, tbl(load_tbl, 51))):
    got = run_hook('digi101_load_hook', bytes(0x100), rec(28, form), load_tbl)
    got2 = run_hook('digi101_load2_hook', bytes(0x100), rec(44, form), load_tbl)
    check(got == want and got2 == want, 'load storage form %#x -> slot %d (LFO1 %d, LFO2 %d)' % (form, want, got, got2))
# slots 46/47 copied as before
st = bytearray(0x100); st[0x1c + 92:0x1c + 96] = b'\x12\x34\x56\x78'
run_hook('digi101_load_hook', bytes(st), bytes(0x100), load_tbl)
check(uc.mem_read(A_RUN + 0x14 + 92, 4) == b'\x12\x34\x56\x78', 'load: slots 46/47 still copied')
# ---- machine-pages names them, from DH-101's page (lfo, lfo_long) ----
KIT = 0x41f00000
uc.mem_write(0x4199ec44, struct.pack('>I', KIT))      # the UI's kit
uc.mem_write(KIT + 0x9e, bytes([29]))                 # track 1: DH-101
set_track(0, 29)
dd = sym.get('machine-pages:mp_lfo52') or sym.get('mp_lfo52')
def cstr(a):
    b = bytes(uc.mem_read(a, 40)); return b[:b.index(0)].decode()
STOCKS = 0x41f10000
uc.mem_write(STOCKS, b'stock\0')
for i, what, want in ((109, 0, '101'), (109, 1, 'Filter Freq'), (109, 2, 'FREQ'),
                      (114, 1, 'Filter Reso'), (114, 2, 'RESO'), (113, 1, 'PWM Source'), (113, 2, 'PWM'),
                      (110, 1, 'Saw Level'), (110, 2, 'SAW'), (111, 1, 'Noise Level'), (115, 1, 'Sub Osc'),
                      (108, 1, 'stock')):
    got = cstr(call(dd, [i * 52, what, STOCKS], STOP))
    check(got == want, 'DEST name of id %d, field %d: %s (%s)' % (i, what, want, got))
check(cstr(call(dd, [30 * 52, 1, STOCKS], STOP)) == 'stock', 'a stock destination keeps its own name')
check(cstr(call(dd, [116 * 52, 1, STOCKS], STOP)) == 'stock', 'id 116 (0.20): not ours any more')
uc.mem_write(KIT + 0x9e, bytes([0]))
check(cstr(call(dd, [109 * 52, 1, STOCKS], STOP)) == 'stock', 'not on a DH-101 track')
print('all passed' if not fails else 'FAILED')
sys.exit(fails != 0)
