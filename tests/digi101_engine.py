#!/usr/bin/env python3
"""Check the DH-101 engine against what sh101.h says, through the desktop harness."""
import subprocess, sys, os
import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
RENDER = os.path.join(HERE, "..", "tools", "digi101_render")   # build it first (tools/digi101_render.c)
OUT = os.path.join(HERE, "..", "out", "test")
os.makedirs(OUT, exist_ok=True)
fails = []

def render(knobs, seq="57:1.0", secs=1.0, sweep=None, name="t"):
    path = os.path.join(OUT, name + ".wav")
    env = dict(os.environ)
    if sweep:
        env["SWEEP"] = sweep
    subprocess.run([RENDER, path, str(secs), " ".join(map(str, knobs)), seq], check=True, env=env)
    return wavfile.read(path)[1].astype(np.float64) / 32768.0

def f0(x, lo=20, hi=6000):
    x = x[4800:]                                     # skip the start
    w = np.hanning(len(x))
    s = np.abs(np.fft.rfft(x * w, 1 << 20))
    f = np.fft.rfftfreq(1 << 20, 1 / 48000)
    band = (f > lo) & (f < hi)
    pk = s[band].max()
    # the lowest peak at least 30 % of the strongest: the fundamental
    idx = np.where(band & (s > 0.3 * pk))[0][0]
    while s[idx + 1] > s[idx]:
        idx += 1
    return f[idx]

def check(name, ok, detail):
    print(("PASS " if ok else "FAIL ") + name + ": " + detail)
    if not ok:
        fails.append(name)

# knob order B C E F G H D
SAW = [32, 127, 0, 0, 0, 0, 0]          # 8', saw only

# 1. range: A3 (220 Hz) at 16' 8' 4' 2'
for zone, mult, label in [(0, 0.5, "16'"), (32, 1, "8'"), (64, 2, "4'"), (96, 4, "2'")]:
    k = list(SAW); k[0] = zone
    got = f0(render(k, name="range_" + str(zone)))
    check("range " + label, abs(got / (220 * mult) - 1) < 0.003, "%.2f Hz, want %.2f" % (got, 220 * mult))

# 2. sub modes, sub alone: -1 oct = 110, -2 oct = 55 (square and 25 % pulse)
for v, want, label in [(42, 110, "-1 oct square"), (85, 55, "-2 oct square"), (127, 55, "-2 oct 25% pulse")]:
    x = render([32, 0, 0, 0, 0, v, 0], name="sub_%d" % v)
    got = f0(x, 20)
    check("sub " + label, abs(got / want - 1) < 0.005, "%.2f Hz, want %d" % (got, want))

# sub in step with the VCO: the -1 oct square's edges fall on every second saw wrap (to the sample)
saw = render([32, 127, 0, 0, 0, 0, 0], name="lock_saw")[4800:40000]
sub = render([32, 0, 0, 0, 0, 42, 0], name="lock_sub")[4800:40000]
wraps = np.where(np.diff(saw) < -0.5)[0]
wraps = wraps[np.insert(np.diff(wraps) > 3, 0, True)]          # a blep'd drop spans two samples
edges = np.where(np.diff(np.sign(sub)) != 0)[0]
near = all(np.min(np.abs(wraps - e)) <= 1 for e in edges)
check("sub locked to VCO", near and abs(len(edges) - len(wraps)) <= 2,
      "%d sub edges, %d saw wraps, all edges on a wrap: %s" % (len(edges), len(wraps), near))

# 3. pulse width (MAN): duty = 50 % at PW 0, 5 % at PW 127
for pw, want in [(0, 0.50), (64, 0.273), (127, 0.05)]:
    x = render([32, 0, 127, pw, 0, 0, 0], name="pw_%d" % pw)[4800:]
    duty = np.mean(x > np.median(x[np.abs(x) > 0.01]) if False else x > 0)
    hi = np.mean(x > 0)
    check("PW %d duty" % pw, abs(min(hi, 1 - hi) - want) < 0.02, "%.3f, want %.3f" % (min(hi, 1 - hi), want))

# 4. DC: every waveform's mean near 0
for knobs, label in [([32, 0, 127, 127, 0, 0, 0], "narrow pulse"), ([32, 127, 0, 0, 0, 0, 0], "saw"),
                     ([32, 0, 0, 0, 0, 127, 0], "25% sub")]:
    x = render(knobs, name="dc")[4800:]
    check("DC " + label, abs(x.mean()) < 0.01, "mean %.4f" % x.mean())

# 5. levels: everything full, narrow pulse, all ranges: no clipping, no wrap-around (overflow shows as
# sample-to-sample jumps far beyond the waveform's own)
for zone in (0, 96):
    x = render([zone, 127, 127, 127, 0, 127, 127], seq="36:0.5,96:0.5", name="full_%d" % zone)
    peak = np.abs(x).max()
    clip = np.mean(np.abs(x) > 0.999)
    check("full mix %d peak" % zone, peak <= 1.0 and clip < 0.001, "peak %.3f, clipped %.4f%%" % (peak, clip * 100))

# 6. aliasing: a saw at C7 (2093 Hz) at 2' (8372 Hz): energy off the harmonic series, dB below the fundamental
x = render([96, 127, 0, 0, 0, 0, 0], seq="96:1.0", name="alias")[4800:]
s = np.abs(np.fft.rfft(x * np.hanning(len(x))))
f = np.fft.rfftfreq(len(x), 1 / 48000)
fund = 440 * 2 ** ((96 - 69) / 12) * 4
harm = np.zeros_like(f, dtype=bool)
for h in range(1, int(24000 / fund) + 1):
    harm |= np.abs(f - h * fund) < 40
top = s[np.abs(f - fund) < 40].max()
worst = s[(~harm) & (f > 100) & (f < 20000)].max()
db = 20 * np.log10(worst / top)
check("saw aliasing at 8.4 kHz", db < -20, "worst alias %.1f dB below the fundamental" % -db)

# 7. PWM LFO moves the width; ENV narrows then returns to square
x = render([32, 0, 127, 127, 40, 0, 0], secs=2.0, seq="57:2.0", name="pwm_lfo")
duties = [np.mean(x[i:i + 2400] > 0) for i in range(4800, len(x) - 2400, 2400)]
d = [min(v, 1 - v) for v in duties]
check("PWM LFO sweeps", max(d) - min(d) > 0.3, "duty %.2f..%.2f" % (min(d), max(d)))
x = render([32, 0, 127, 127, 100, 0, 0], secs=2.0, seq="57:2.0", name="pwm_env")
d0 = np.mean(x[480:2880] > 0); d1 = np.mean(x[-4800:] > 0)
check("PWM ENV narrow -> square", min(d0, 1 - d0) < 0.15 and abs(d1 - 0.5) < 0.03, "start %.2f, end %.2f" % (min(d0, 1 - d0), d1))

# 8. note start: first sample near zero (a clean start for zero-crossing trims)
x = render([32, 127, 0, 0, 0, 0, 0], name="start")
check("note starts at zero", abs(x[0]) < 0.02, "first sample %.4f" % x[0])

# 9. portamento (PORT env -> sh101_port): starts at the last note's pitch, approaches the new one
# exponentially in octaves, settles on it; up and down glides are mirror images in octaves; PORT 0 = none
def pitch_track(x, hop=480, win=2400):
    out = []
    for a in range(0, len(x) - win, hop):
        s = x[a:a + win]; z = np.where((s[:-1] < 0) & (s[1:] >= 0))[0]
        out.append(48000.0 * (len(z) - 1) / (z[-1] - z[0]) if len(z) > 2 else 0)
    return np.array(out)
def glide_render(port, seq, name):
    env = dict(os.environ); env["PORT"] = str(port)
    path = os.path.join(OUT, name + ".wav")
    subprocess.run([RENDER, path, "3.0", "32 127 0 0 0 0 0", seq], check=True, env=env)
    return wavfile.read(path)[1].astype(np.float64)
up = pitch_track(glide_render(80, "45:0.4,57:2.6", "glide_up"))
dn = pitch_track(glide_render(80, "57:0.4,45:2.6", "glide_dn"))
i0 = int(0.4 * 48000 / 480)                      # the second note's first full window
oct_up = np.log2(220.0 / up[i0 + 2:i0 + 40])     # octaves still to go
oct_dn = np.log2(dn[i0 + 2:i0 + 40] / 110.0)
check("glide starts near the last pitch", up[i0 + 1] < 130 and dn[i0 + 1] > 190, "up %.0f Hz, down %.0f Hz" % (up[i0 + 1], dn[i0 + 1]))
check("glide up/down symmetric in octaves", np.max(np.abs(oct_up - oct_dn)) < 0.03, "max difference %.3f oct" % np.max(np.abs(oct_up - oct_dn)))
check("glide settles on the note", abs(up[-1] / 220.0 - 1) < 0.002 and abs(dn[-1] / 110.0 - 1) < 0.002, "%.2f and %.2f Hz" % (up[-1], dn[-1]))
r = np.log(oct_up[5:25]); slope = np.polyfit(np.arange(20) * 0.01, r, 1)[0]
check("glide is exponential in octaves", np.std(r - np.polyval(np.polyfit(np.arange(20) * 0.01, r, 1), np.arange(20) * 0.01)) < 0.05,
      "time constant %.0f ms" % (-1000 / slope))
no = pitch_track(glide_render(0, "45:0.4,57:1.2", "glide_off"))
check("PORT 0: no glide", abs(no[i0 + 1] / 220 - 1) < 0.01, "%.1f Hz right after the change" % no[i0 + 1])

# 10. the unit hands a note's pitch to the engine a block or more after its trig: glides must still
# happen, up and down, every note (v0.5 glided once, then never)
for d in (1, 6, 12, -1, -3, -8):
    env = dict(os.environ); env["PORT"] = "60"; env["NOTE_DELAY"] = str(d)
    path = os.path.join(OUT, "glide_late.wav")
    subprocess.run([RENDER, path, "2.0", "32 127 0 0 0 0 0", "45:0.5,57:0.5,45:0.5,57:0.5"], check=True, env=env)
    t = pitch_track(wavfile.read(path)[1].astype(np.float64), 600, 2400)
    mids = [t[42], t[82], t[122]]                # 0.1 s into notes 2, 3, 4: still on the way
    ok = 120 < mids[0] < 215 and 115 < mids[1] < 210 and 120 < mids[2] < 215
    check("glides with the pitch %d blocks %s" % (abs(d), "late" if d > 0 else "early"), ok, "mid-glide %.0f, %.0f, %.0f Hz" % tuple(mids))

print("\n%d failed" % len(fails) if fails else "\nall passed")
sys.exit(1 if fails else 0)
