"""Latency analysis: marker timestamp -> tone threshold crossing in MCC data.

The core routine :func:`compute_latencies` works on plain numpy arrays; the
marker times and MCC trace are loaded from the XDF that LabRecorderCLI writes.
"""

from __future__ import annotations

import numpy as np


# --------------------------------------------------------------------------- #
# Loaders
# --------------------------------------------------------------------------- #
def load_from_xdf(path, mcc_name, marker_type="Markers", mcc_channel=0):
    """Load (marker_times, mcc_timestamps, mcc_signal) from an XDF file.

    Clock synchronization and dejittering are applied so the marker and MCC
    timestamps live in a common, regularized time base.
    """
    import pyxdf

    streams, _ = pyxdf.load_xdf(
        path, synchronize_clocks=True, dejitter_timestamps=True)

    mcc = mrk = None
    for s in streams:
        name = s["info"]["name"][0]
        typ = s["info"]["type"][0] if s["info"]["type"] else ""
        if name == mcc_name:
            mcc = s
        elif typ == marker_type:
            mrk = s
    if mcc is None:
        raise RuntimeError(f"MCC stream '{mcc_name}' not found in {path}")
    if mrk is None:
        raise RuntimeError(f"No '{marker_type}' stream found in {path}")

    mcc_ts = np.asarray(mcc["time_stamps"], dtype=float)
    data = np.asarray(mcc["time_series"], dtype=float)
    if data.ndim == 1:
        data = data[:, None]
    sig = data[:, mcc_channel]
    marker_times = np.asarray(mrk["time_stamps"], dtype=float)
    return marker_times, mcc_ts, sig


# --------------------------------------------------------------------------- #
# Timestamp health check
# --------------------------------------------------------------------------- #
def check_timestamps(path, mcc_name, marker_type="Markers"):
    """Verify the *raw* (un-synced, un-dejittered) timestamps are sound.

    ``load_from_xdf`` deliberately loads with ``dejitter_timestamps=True``,
    which refits each stream's timestamps onto a clean monotonic line and so
    *hides* upstream defects. This routine instead loads the timestamps exactly
    as the outlet emitted them and checks for the two problems that surface
    downstream (e.g. in Orion) as "negative timestamps":

    * **negative absolute timestamps** -- a genuinely bad clock value;
    * **non-monotonic (backward) steps** -- consecutive samples whose timestamp
      goes *backwards*. These become negative inter-sample intervals (and, in a
      consumer that derives sample times by accumulating dt or that aligns
      without dejittering, negative timestamps). They are the expected artifact
      of stamping each chunk's most-recent sample with ``local_clock()`` at read
      time and letting liblsl back-date the rest: when one chunk's back-dated
      start lands before the previous chunk's end, time runs backward.

    Returns ``(ok, results)`` where ``ok`` is False if any stream has negative
    timestamps, and ``results`` maps stream name -> stats dict. Prints a
    human-readable summary.
    """
    import pyxdf

    streams, _ = pyxdf.load_xdf(
        path, synchronize_clocks=False, dejitter_timestamps=False)

    results = {}
    ok = True
    lines = ["Timestamp health check (raw, as emitted -- no sync/dejitter):"]
    for s in streams:
        name = s["info"]["name"][0]
        ts = np.asarray(s["time_stamps"], dtype=float)
        if ts.size == 0:
            continue
        neg = int(np.count_nonzero(ts < 0))
        dt = np.diff(ts)
        back = dt < 0
        n_back = int(np.count_nonzero(back))
        max_back_ms = float(-dt[back].min() * 1e3) if n_back else 0.0
        results[name] = {
            "n": int(ts.size),
            "t_min": float(ts.min()), "t_max": float(ts.max()),
            "n_negative": neg,
            "n_backward": n_back, "max_backward_ms": max_back_ms,
            "pct_backward": 100.0 * n_back / max(dt.size, 1),
        }
        if neg:
            status = "FAIL (negative timestamps)"
            ok = False
        elif n_back:
            status = "WARN (non-monotonic)"
        else:
            status = "OK"
        detail = f"  backward_steps={n_back}"
        if n_back:
            detail += (f" ({100.0 * n_back / max(dt.size, 1):.2f}% of samples,"
                       f" max {max_back_ms:.3f} ms back)")
        lines.append(
            f"  {name:14} n={ts.size:>8d}  t_min={ts.min():14.4f}"
            f"  neg={neg}{detail}  -> {status}")

    mcc = results.get(mcc_name)
    if mcc is not None and mcc["n_negative"] == 0 and mcc["n_backward"] > 0:
        lines.append(
            "  NOTE: no negative timestamps on this machine, but the MCC stream "
            "is non-monotonic. Across machines (or in a consumer that does not "
            "dejitter, e.g. Orion) those backward steps are what appear as "
            "negative timestamps. Fix upstream by making the outlet emit "
            "monotonic, device-anchored timestamps.")
    print("\n".join(lines))
    return ok, results


# --------------------------------------------------------------------------- #
# Core: per-marker onset detection
# --------------------------------------------------------------------------- #
def compute_latencies(marker_times, mcc_ts, mcc_sig,
                      window=0.05, pre=0.01, thresh_frac=0.2,
                      min_snr=6.0, abs_threshold=None, guard=0.01):
    """Return latency (seconds) from each marker to the tone onset.

    For each marker time ``tm`` we look in ``[tm - pre, tm + window]``:

    * baseline & noise are estimated from the pre-marker samples (robust
      median / MAD);
    * the onset is the first post-marker sample whose deviation from baseline
      exceeds the detection threshold;
    * the crossing time is linearly interpolated between the bracketing samples
      to beat sample quantization.

    ``thresh_frac`` sets the threshold as a fraction of the burst's peak
    deviation (auto-scaling to whatever physical level the audio path delivers).
    Pass ``abs_threshold`` to force a fixed deviation instead. Markers with no
    detectable tone (peak < ``min_snr`` * noise) yield ``nan``.

    The search window is also clamped to end ``guard`` seconds before the next
    marker, so a deliberately large ``window`` (needed when the latency is large,
    e.g. a big audio buffer) can never bleed into the following tone.

    Returns ``(latencies, info)`` where ``info`` is a list of per-marker dicts
    for plotting / debugging.
    """
    marker_times = np.asarray(marker_times, dtype=float)
    mcc_ts = np.asarray(mcc_ts, dtype=float)
    mcc_sig = np.asarray(mcc_sig, dtype=float)

    latencies = np.full(len(marker_times), np.nan)
    info = []

    order = np.argsort(marker_times)
    next_marker = np.full(len(marker_times), np.inf)
    next_marker[order[:-1]] = marker_times[order[1:]]

    for k, tm in enumerate(marker_times):
        win_end = min(tm + window, next_marker[k] - guard)
        i0 = np.searchsorted(mcc_ts, tm - pre)
        i1 = np.searchsorted(mcc_ts, win_end)
        rec = {"marker": tm, "i0": i0, "i1": i1, "latency": np.nan,
               "t_cross": np.nan, "threshold": np.nan, "peak": np.nan}
        info.append(rec)
        if i1 - i0 < 4:
            continue

        seg_ts = mcc_ts[i0:i1]
        seg = mcc_sig[i0:i1]
        pre_mask = seg_ts < tm
        if pre_mask.sum() >= 3:
            base = np.median(seg[pre_mask])
            noise = 1.4826 * np.median(np.abs(seg[pre_mask] - base)) + 1e-12
        else:
            base = np.median(seg)
            noise = 1.4826 * np.median(np.abs(seg - base)) + 1e-12

        dev = np.abs(seg - base)
        peak = float(dev.max())
        rec["peak"] = peak
        if peak < min_snr * noise:
            continue  # no tone detected in this window

        if abs_threshold is not None:
            thr = float(abs_threshold)
        else:
            thr = max(thresh_frac * peak, min_snr * noise)
        rec["threshold"] = thr

        # First crossing strictly after the marker.
        post0 = np.searchsorted(seg_ts, tm)
        dev_post = dev.copy()
        dev_post[:post0] = 0.0
        above = dev_post > thr
        if not above.any():
            continue
        cross = int(np.argmax(above))

        if cross > 0:
            d0, d1 = dev[cross - 1], dev[cross]
            frac = (thr - d0) / (d1 - d0) if d1 > d0 else 0.0
            frac = min(max(frac, 0.0), 1.0)
            t_cross = seg_ts[cross - 1] + frac * (seg_ts[cross] - seg_ts[cross - 1])
        else:
            t_cross = seg_ts[cross]

        rec["t_cross"] = t_cross
        rec["latency"] = t_cross - tm
        latencies[k] = t_cross - tm

    return latencies, info


# --------------------------------------------------------------------------- #
# Stats
# --------------------------------------------------------------------------- #
def summarize(latencies):
    lat = np.asarray(latencies, dtype=float)
    valid = lat[np.isfinite(lat)]
    n_total = len(lat)
    n_valid = len(valid)
    if n_valid == 0:
        return {"n_total": n_total, "n_valid": 0}
    ms = valid * 1e3
    return {
        "n_total": n_total,
        "n_valid": n_valid,
        "n_missed": n_total - n_valid,
        "mean_ms": float(np.mean(ms)),
        "median_ms": float(np.median(ms)),
        "std_ms": float(np.std(ms, ddof=1)) if n_valid > 1 else 0.0,
        "min_ms": float(np.min(ms)),
        "max_ms": float(np.max(ms)),
        "ptp_ms": float(np.ptp(ms)),          # peak-to-peak jitter
        "iqr_ms": float(np.subtract(*np.percentile(ms, [75, 25]))),
    }


def format_summary(stats, method=""):
    if stats.get("n_valid", 0) == 0:
        return (f"No tones detected ({stats.get('n_total', 0)} markers). "
                "Check the audio<->DAQ cabling, levels, and threshold.")
    lines = [
        f"Audio latency results{f'  [{method}]' if method else ''}",
        f"  markers / detected : {stats['n_total']} / {stats['n_valid']}"
        f" (missed {stats['n_missed']})",
        f"  mean   latency     : {stats['mean_ms']:8.3f} ms",
        f"  median latency     : {stats['median_ms']:8.3f} ms",
        f"  min / max          : {stats['min_ms']:8.3f} / {stats['max_ms']:.3f} ms",
        "  -- JITTER (the number to minimize) --",
        f"  std  dev           : {stats['std_ms']:8.3f} ms",
        f"  peak-to-peak       : {stats['ptp_ms']:8.3f} ms",
        f"  IQR                : {stats['iqr_ms']:8.3f} ms",
    ]
    if stats.get("n_missed", 0) > 0:
        lines.append(
            f"  NOTE: {stats['n_missed']} marker(s) had no tone in the search "
            "window -- if the latency is large, increase --window or --isi; "
            "otherwise check cabling/level/threshold.")
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Plotting (optional)
# --------------------------------------------------------------------------- #
def _auto_epoch_xrange(mcc_ts, mcc_sig, info, energy_cutoff=0.05,
                       margin_frac=0.25, min_margin=0.002):
    """Auto x-range (seconds, relative to marker) for the overlaid epochs.

    Looks only at epochs where a tone was detected, finds — per epoch — the span
    of time where the rectified signal exceeds ``energy_cutoff`` x that epoch's
    peak (an energy-envelope cutoff), and returns the union of those spans padded
    by a margin. The left edge is clamped to keep the marker (t = 0) in view.
    Returns ``None`` if nothing was detected (caller falls back to no zoom).
    """
    lo, hi = [], []
    for rec in info:
        if not np.isfinite(rec.get("latency", np.nan)):
            continue
        i0, i1 = rec["i0"], rec["i1"]
        if i1 - i0 < 4:
            continue
        t_rel = mcc_ts[i0:i1] - rec["marker"]
        seg = mcc_sig[i0:i1]
        pre_mask = t_rel < 0
        base = np.median(seg[pre_mask]) if pre_mask.sum() >= 3 else np.median(seg)
        dev = np.abs(seg - base)
        peak = dev.max()
        if peak <= 0:
            continue
        energetic = t_rel[dev > energy_cutoff * peak]
        if len(energetic):
            lo.append(float(energetic.min()))
            hi.append(float(energetic.max()))
    if not lo:
        return None
    t0, t1 = min(lo), max(hi)
    margin = max(margin_frac * (t1 - t0), min_margin)
    return min(t0 - margin, -min_margin), t1 + margin


def save_plots(latencies, marker_times, mcc_ts, mcc_sig, info, out_png,
               window=0.05, pre=0.01, title=""):
    """Histogram of latencies + overlaid tone epochs. Requires matplotlib."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return None

    lat = np.asarray(latencies, dtype=float)
    valid = lat[np.isfinite(lat)] * 1e3

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))

    if len(valid):
        ax1.hist(valid, bins=min(40, max(5, len(valid) // 2)),
                 color="#3b7dd8", edgecolor="white")
        ax1.axvline(np.median(valid), color="k", ls="--", lw=1,
                    label=f"median {np.median(valid):.2f} ms")
        ax1.legend()
    ax1.set_xlabel("marker -> tone latency (ms)")
    ax1.set_ylabel("count")
    ax1.set_title("Latency distribution")

    # Overlaid epochs aligned on the marker (t = 0).
    for rec in info:
        i0, i1 = rec["i0"], rec["i1"]
        if i1 - i0 < 4:
            continue
        t_rel = (mcc_ts[i0:i1] - rec["marker"]) * 1e3
        ax2.plot(t_rel, mcc_sig[i0:i1], color="#888", alpha=0.25, lw=0.6)
    ax2.axvline(0, color="r", lw=1, label="marker")
    ax2.set_xlabel("time relative to marker (ms)")
    ax2.set_ylabel("MCC signal")
    ax2.set_title("Tone epochs (overlaid)")
    ax2.legend()
    xr = _auto_epoch_xrange(mcc_ts, mcc_sig, info)
    if xr is not None:
        ax2.set_xlim(xr[0] * 1e3, xr[1] * 1e3)

    if title:
        fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(out_png, dpi=120)
    plt.close(fig)
    return out_png
