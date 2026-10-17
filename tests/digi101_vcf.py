"""vcf.s (the ColdFire loop) against sh101.c's C reference, in Unicorn's ColdFire V4e with EMAC.

Needs the cross toolchain (CROSS=.../m68k-linux-gnu-) and a Unicorn 2.1.4 with digiemu's EMAC patches
(UNICORN_PY = its bindings/python, LIBUNICORN_PATH = its build). Prints the largest difference."""
import ctypes, os, subprocess, sys, random, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
CROSS = os.environ.get('ELEKLOADER_CROSS', 'm68k-linux-gnu-')
# Unicorn 2.1.4 with digiemu's EMAC patches (tools/dev.sh setup puts it in digiemu's venv: run with that python)
if os.environ.get('UNICORN_PY'):
    sys.path.insert(0, os.environ['UNICORN_PY'])
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_PC, UC_M68K_REG_A7
W = '/tmp/claude-0/vt'
os.makedirs(W, exist_ok=True)

def build():
    subprocess.run([CROSS + 'as', '-mcpu=54455', ROOT + '/mods/digi101/vcf.s', '-o', W + '/vcf.o'], check=True)
    subprocess.run([CROSS + 'as', '-mcpu=54455', HERE + '/digi101_vcf_stub.s', '-o', W + '/stub.o'], check=True)
    subprocess.run([CROSS + 'ld', '-Ttext=0x1000', '-e', '_start', W + '/stub.o', W + '/vcf.o', '-o', W + '/t.elf'], check=True)
    subprocess.run([CROSS + 'objcopy', '-O', 'binary', W + '/t.elf', W + '/t.bin'], check=True)
    stop = int([l for l in subprocess.run([CROSS + 'nm', W + '/t.elf'], capture_output=True, text=True).stdout.split('\n') if l.endswith(' _stop')][0].split()[0], 16)
    subprocess.run(['gcc', '-O2', '-shared', '-fPIC', '-o', W + '/ref.so', ROOT + '/mods/digi101/sh101.c'], check=True)
    return open(W + '/t.bin', 'rb').read(), stop, ctypes.CDLL(W + '/ref.so')

def coefs(fc, k):
    g = math.tan(math.pi * fc / 48000); G = g / (1 + g); om = 1 - G
    inv = 1 / (1 + k * G ** 4); kinv = k * inv
    q28 = lambda v: int(round(v / 8 * 2 ** 31)); q31 = lambda v: min(int(round(v * 2 ** 31)), 2 ** 31 - 1)
    return [q28(kinv * G ** 4), q28(kinv * G ** 3 * om), q28(kinv * G ** 2 * om), q28(kinv * G * om), q28(kinv * om),
            q31(G), q28(1 + 0.42 * k)]

def run_asm(code, stop, io, c, s):
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, cpu=UC_CPU_M68K_CFV4E) if 'cpu' in Uc.__init__.__code__.co_varnames else Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    try:
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    except Exception:
        pass
    uc.mem_map(0, 0x100000)
    uc.mem_write(0x1000, code)
    n = len(io)
    uc.mem_write(0x90000, b''.join(int(v).to_bytes(2, 'big', signed=True) for v in io))
    uc.mem_write(0x88000, b''.join(int(v).to_bytes(4, 'big', signed=True) for v in c))
    uc.mem_write(0x88100, b''.join(int(v).to_bytes(4, 'big', signed=True) for v in s))
    uc.mem_write(0x80000, b''.join(v.to_bytes(4, 'big') for v in (0x90000, n, 0x88000, 0x88100)))
    uc.reg_write(UC_M68K_REG_A7, 0x7fff0)
    uc.emu_start(0x1000, stop)
    out = np.frombuffer(bytes(uc.mem_read(0x90000, 2 * n)), dtype='>i2').astype(int)
    st = [int.from_bytes(uc.mem_read(0x88100 + 4 * i, 4), 'big', signed=True) for i in range(4)]
    return out, st

def run_ref(lib, io, c, s):
    n = len(io)
    a = (ctypes.c_int16 * n)(*io); cc = (ctypes.c_int32 * 7)(*c); ss = (ctypes.c_int32 * 4)(*s)
    lib.sh101_vcf_loop(a, n, cc, ss)
    return np.array(list(a)), list(ss)

def main():
    code, stop, lib = build()
    rng = random.Random(1)
    worst = 0; fails = 0
    cases = [(30, 0), (60, 4.4), (250, 1.9), (250, 3.8), (1200, 4.4), (5000, 2.5), (15000, 3.3), (20000, 0), (100, 3.78)]
    for fc, k in cases:
        for amp in (1000, 30000):
            n = 3000
            io = [int(max(-32768, min(32767, amp * (2 * ((i * 0.0123) % 1) - 1) + rng.randint(-200, 200)))) for i in range(n)]
            c = coefs(fc, k)
            ref, rs = run_ref(lib, io, c, [0] * 4)
            asm, as_ = run_asm(code, stop, io, c, [0] * 4)
            d = int(np.max(np.abs(ref - asm))); rms = float(np.sqrt(np.mean((ref - asm) ** 2)))
            sig = float(np.sqrt(np.mean(ref.astype(float) ** 2))) + 1e-9
            ok = d <= 2 or 20 * math.log10(max(rms, 1e-9) / sig) < -80
            fails += not ok
            print('fc %5d k %.2f amp %5d: largest difference %d LSB (rms %.3f, %.0f dB below the signal) %s'
                  % (fc, k, amp, d, rms, 20 * math.log10(max(rms, 1e-9) / sig), 'ok' if ok else 'FAIL'))
            worst = max(worst, d)
    print('all passed' if not fails else 'FAILED')
    return fails
if __name__ == '__main__':
    sys.exit(main())
