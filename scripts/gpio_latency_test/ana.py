"""ana.py REC.bin GPIO.csv: per-edge (LSL stamp of the DAQ edge crossing) - (GPIO write time), in us.
The mean is the latency left in the timestamps: add it to timestamp_offset_us."""
import sys, numpy as np
rec = np.fromfile(sys.argv[1], dtype=[("t", "<f8"), ("v", "<i2")])
t, v = rec["t"], rec["v"].astype(float)
g = np.loadtxt(sys.argv[2], delimiter=",", comments="#", skiprows=2)
tg = (g[:, 2] + g[:, 3]) / 2e9
lo, hi = np.percentile(v, 5), np.percentile(v, 95)
thr = (lo + hi) / 2
above = v > thr
idx = np.flatnonzero(above[1:] != above[:-1])  # crossing between idx and idx+1
frac = (thr - v[idx]) / (v[idx + 1] - v[idx])
tc = t[idx] + frac * (t[idx + 1] - t[idx])
rising = above[idx + 1]
# match each GPIO edge to the nearest crossing within 20 ms
j = np.searchsorted(tc, tg)
err = []
for k, tgk in enumerate(tg):
    c = [x for x in (j[k] - 1, j[k]) if 0 <= x < len(tc)]
    if not c: continue
    b = min(c, key=lambda x: abs(tc[x] - tgk))
    if abs(tc[b] - tgk) < 0.02 and rising[b] == bool(g[k, 1]): err.append(tc[b] - tgk)
e = np.array(err) * 1e6
dt = np.diff(t)
print(f"{len(tg)} gpio edges, {len(tc)} crossings, matched {len(e)}; samples {len(t)}, eff rate {(len(t)-1)/(t[-1]-t[0]):.3f} Hz")
print(f"stamp - true (us): mean {e.mean():.1f} SD {e.std():.1f} p1 {np.percentile(e,1):.1f} median {np.median(e):.1f} p99 {np.percentile(e,99):.1f} min {e.min():.1f} max {e.max():.1f}")
# slow drift of the error over the run (first vs last 10% of edges)
n10 = max(1, len(e) // 10)
print(f"first10% mean {e[:n10].mean():.1f}  last10% mean {e[-n10:].mean():.1f}")
np.save(sys.argv[1] + ".err.npy", e)
