"""DH-101 0.24 linked into the real OS image (1.53 or 1.54) with core and machine-pages (elekloader pull request
#103), run under Unicorn (ColdFire V4e): machine-pages accepts its two-page description, and names both pages'
parameters on the LFO page (page 1 by SLICE's ids, page 2 by the eight ids machine-pages adds, 164..171).
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

ACTIVE = sym.get('fw_active_track')
KITP = sym.get('fw_kit')
ui5 = sym.get('machine-pages:mp_ui5') or sym.get('mp_ui5')
check(ui5 is not None and call(ui5, [29], STOP) != 0, 'machine-pages accepts DH-101\'s two pages (mp_ui5(29))')
check(call(ui5, [7], STOP) == 0, 'and no other machine as one')

KIT = 0x41f00000
uc.mem_write(KITP, struct.pack('>I', KIT))            # the UI's kit
uc.mem_write(KIT + 0x9e, bytes([29]))                 # track 1: DH-101
uc.mem_write(ACTIVE, struct.pack('>I', 0))
dd = sym.get('machine-pages:mp_lfo52') or sym.get('mp_lfo52')
def cstr(a):
    b = bytes(uc.mem_read(a, 40)); return b[:b.index(0)].decode()
STOCKS = 0x41f10000
uc.mem_write(STOCKS, b'stock\0')
names = [(0x84, 1, 'Tune'), (0x85, 1, 'Range'), (0x86, 2, 'SAW'), (0x87, 1, 'Noise Level'), (0x8b, 1, 'Sub Osc'),
         (0x85, 0, '101'), (164, 1, 'Portamento'), (165, 1, 'Filter Freq'), (165, 2, 'FREQ'), (166, 1, 'Filter Reso'),
         (167, 1, 'Env Depth'), (169, 1, 'Keyboard Follow'), (171, 1, 'LFO Rate'), (171, 0, '101')]
for i, what, want in names:
    got = cstr(call(dd, [i * 52, what, STOCKS], STOP))
    check(got == want, 'DEST name of id %d, field %d: %s (%s)' % (i, what, want, got))
check(cstr(call(dd, [30 * 52, 1, STOCKS], STOP)) == 'stock', 'a stock destination keeps its own name')
uc.mem_write(KIT + 0x9e, bytes([0]))
check(cstr(call(dd, [165 * 52, 1, STOCKS], STOP)) == 'stock', 'not on a DH-101 track')
print('all passed' if not fails else 'FAILED')
sys.exit(fails != 0)
