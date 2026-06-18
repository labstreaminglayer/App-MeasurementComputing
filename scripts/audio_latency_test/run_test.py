#!/usr/bin/env python3
"""End-to-end audio<->LSL latency / jitter test for the MCC DAQ.

Pipeline
--------
1. Launch ``MCCOutletCLI`` streaming one channel of a USB-1608FS-Plus at a
   high (audio-band) sample rate, full +/-10V range.
2. Create an LSL ``Markers`` stream and play pure sine bursts at a fixed
   interval; every burst emits a marker stamped as accurately as the chosen
   audio back-end allows.
3. If LabRecorderCLI is found, use it to record both streams to XDF.
   Otherwise fall back to an in-memory pylsl recording.
4. After N tones, stop recording and measure, for every marker, the latency to
   the tone's threshold crossing in the MCC data. Report mean latency and --
   the headline number -- the jitter (std / peak-to-peak).

Physical setup: connect an audio output (headphone jack / interface) to MCC
analog input channel 0 (and AGND). Keep the level inside +/-10V.

Run ``python run_test.py --help`` for options. Use ``--analyze-xdf FILE`` to
re-analyze an existing recording without touching hardware.
"""

from __future__ import annotations

import argparse
import datetime
import os
import signal
import subprocess
import sys
import time

import numpy as np

import audio
import analysis
import labrecorder as lr

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CLI = os.path.join(
    REPO, "cmake-build-release", "src", "cli", "MCCOutletCLI")


# --------------------------------------------------------------------------- #
# Arguments
# --------------------------------------------------------------------------- #
def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    # MCC / DAQ
    p.add_argument("--cli", default=DEFAULT_CLI, help="Path to MCCOutletCLI")
    p.add_argument("--mcc-name", default="MCCDaq", help="LSL stream name")
    p.add_argument("--device-name", default="USB-1608FS",
                   help="DAQ product-name substring")
    p.add_argument("--channel", type=int, default=0, help="DAQ input channel")
    p.add_argument("--rate", type=float, default=96000.0,
                   help="DAQ sample rate (Hz); 1608FS-Plus single-chan max 100k")
    p.add_argument("--mcc-range", type=int, default=5,
                   help="uldaq Range id (5 = +/-10V full range on 1608FS-Plus)")

    # Audio
    p.add_argument("--method", default="sd-callback",
                   choices=list(audio.PLAYERS),
                   help="Audio playback back-end")
    p.add_argument("--audio-fs", type=float, default=48000.0,
                   help="Audio output sample rate (Hz)")
    p.add_argument("--audio-device", default=None,
                   help="Output device name substring or index (default: system)")
    p.add_argument("--tone-freq", type=float, default=1000.0,
                   help="Sine frequency (Hz)")
    p.add_argument("--tone-dur", type=float, default=0.05,
                   help="Tone duration (s)")
    p.add_argument("--amplitude", type=float, default=0.5,
                   help="Tone amplitude (0-1 full scale)")
    p.add_argument("--ramp", type=float, default=0.0,
                   help="Onset/offset raised-cosine ramp (s); 0 = hard onset")
    p.add_argument("--blocksize", type=int, default=0,
                   help="Audio callback block size (0 = back-end default)")
    p.add_argument("--out-channels", type=int, default=2,
                   help="Output channel count")

    # Run
    p.add_argument("--n-tones", type=int, default=100, help="Number of tones")
    p.add_argument("--isi", type=float, default=0.75,
                   help="Inter-stimulus interval (s)")
    p.add_argument("--marker-label", default="tone")
    p.add_argument("--marker-name", default="AudioMarkers")

    # Recording / output
    p.add_argument("--outdir", default=os.path.join(HERE, "recordings"))
    p.add_argument("--labrecorder-cli", default=None,
                   help="Path to LabRecorderCLI (default: auto-detect)")
    p.add_argument("--no-plot", action="store_true")

    # Analysis tuning
    p.add_argument("--window", type=float, default=None,
                   help="Search window after each marker (s). Default: auto "
                        "(~95%% of ISI), so even a large audio buffer is captured")
    p.add_argument("--pre", type=float, default=0.01,
                   help="Baseline window before each marker (s)")
    p.add_argument("--thresh-frac", type=float, default=0.2,
                   help="Onset threshold as fraction of burst peak")
    p.add_argument("--abs-threshold", type=float, default=None,
                   help="Force an absolute deviation threshold instead")

    # Re-analyze only
    p.add_argument("--analyze-xdf", default=None,
                   help="Analyze an existing XDF and exit (no hardware)")

    return p.parse_args(argv)


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
def resolve_audio_device(spec):
    """Map an --audio-device substring/index to a sounddevice index."""
    if spec is None:
        return None
    try:
        return int(spec)
    except ValueError:
        pass
    import sounddevice as sd
    for idx, dev in enumerate(sd.query_devices()):
        if dev["max_output_channels"] > 0 and spec.lower() in dev["name"].lower():
            return idx
    raise SystemExit(f"No output device matching '{spec}'")


def wait_for_stream(name, timeout=15.0):
    from pylsl import resolve_byprop
    return bool(resolve_byprop("name", name, 1, timeout=timeout))


def report(args, latencies, marker_times, mcc_ts, mcc_sig, info, tag):
    stats = analysis.summarize(latencies)
    print()
    print(analysis.format_summary(stats, method=args.method))
    print()
    if not args.no_plot:
        png = os.path.join(args.outdir, f"latency_{tag}.png")
        saved = analysis.save_plots(
            latencies, marker_times, mcc_ts, mcc_sig, info, png,
            window=args.window, pre=args.pre,
            title=f"{args.method} @ {args.rate:.0f} Hz")
        if saved:
            print(f"Plot saved: {saved}")
    npz = os.path.join(args.outdir, f"latency_{tag}.npz")
    np.savez(npz, latencies=latencies, marker_times=marker_times)
    print(f"Latencies saved: {npz}")
    return stats


def _terminate(proc, timeout=5.0):
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()


# --------------------------------------------------------------------------- #
# Analyze-only mode
# --------------------------------------------------------------------------- #
def run_analyze_only(args):
    if args.window is None:
        args.window = max(0.1, args.isi * 0.95)
    marker_times, mcc_ts, mcc_sig = analysis.load_from_xdf(
        args.analyze_xdf, args.mcc_name, mcc_channel=args.channel)
    latencies, info = analysis.compute_latencies(
        marker_times, mcc_ts, mcc_sig,
        window=args.window, pre=args.pre,
        thresh_frac=args.thresh_frac, abs_threshold=args.abs_threshold)
    tag = os.path.splitext(os.path.basename(args.analyze_xdf))[0]
    os.makedirs(args.outdir, exist_ok=True)
    report(args, latencies, marker_times, mcc_ts, mcc_sig, info, tag)
    return 0


# --------------------------------------------------------------------------- #
# Full live run
# --------------------------------------------------------------------------- #
def run_live(args):
    from pylsl import StreamInfo, StreamOutlet

    os.makedirs(args.outdir, exist_ok=True)
    if args.window is None:
        args.window = max(0.1, args.isi * 0.95)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    tag = f"{args.method}_{int(args.rate)}_{stamp}"
    cli_proc = player = lr_cli = None
    xdf_path = None

    try:
        # -- 1. Launch MCC CLI --------------------------------------------- #
        if not os.path.exists(args.cli):
            raise SystemExit(f"MCCOutletCLI not found at {args.cli}. "
                             "Build it or pass --cli.")
        cli_cmd = [
            args.cli, "--name", args.mcc_name,
            "--device-name", args.device_name,
            "--low-chan", str(args.channel), "--high-chan", str(args.channel),
            "--rate", str(args.rate),
        ]
        if args.mcc_range is not None and args.mcc_range >= 0:
            cli_cmd += ["--range", str(args.mcc_range)]
        cli_log = open(os.path.join(args.outdir, f"mcc_{tag}.log"), "w")
        print(f"Launching: {' '.join(cli_cmd)}")
        cli_proc = subprocess.Popen(cli_cmd, stdout=cli_log,
                                    stderr=subprocess.STDOUT)
        if not wait_for_stream(args.mcc_name, timeout=15.0):
            raise SystemExit(f"MCC stream '{args.mcc_name}' never appeared. "
                             f"See {cli_log.name}")
        print(f"MCC stream '{args.mcc_name}' is live.")

        # -- 2. Marker outlet + audio player ------------------------------- #
        info = StreamInfo(args.marker_name, "Markers", 1, 0, "string",
                          f"audiomarkers_{stamp}")
        outlet = StreamOutlet(info)
        tone = audio.make_tone(args.audio_fs, args.tone_freq, args.tone_dur,
                               args.amplitude, ramp=args.ramp)
        dev = resolve_audio_device(args.audio_device)
        player = audio.make_player(
            args.method, args.audio_fs, tone, outlet, args.marker_label,
            device=dev, out_channels=args.out_channels,
            blocksize=args.blocksize)
        player.start()
        print(f"Audio player '{args.method}' ready "
              f"(clock offset {getattr(player, 'offset', 0.0) * 1e3:.3f} ms).")

        # -- 3. Record with LabRecorderCLI --------------------------------- #
        # Both streams already exist (MCC live, marker outlet created), which is
        # exactly what LabRecorderCLI needs since it resolves streams just once.
        cli_bin = lr.find_labrecorder_cli(args.labrecorder_cli)
        if not cli_bin:
            raise SystemExit("LabRecorderCLI not found; pass --labrecorder-cli "
                             "or set LABRECORDER_CLI.")
        queries = [f'name="{args.mcc_name}"', f'name="{args.marker_name}"']
        xdf_name = f"audio_latency_{tag}.xdf"
        xdf_path = os.path.join(args.outdir, xdf_name)
        lr_cli = lr.CLISession(cli_bin, xdf_path, queries)
        lr_cli.start()
        print(f"LabRecorderCLI recording -> {xdf_path}")

        # -- 4. Tone loop -------------------------------------------------- #
        print(f"Playing {args.n_tones} tones at {args.isi}s ISI...")
        t_next = time.perf_counter()
        for i in range(args.n_tones):
            player.play()
            if (i + 1) % 10 == 0:
                print(f"  {i + 1}/{args.n_tones}")
            t_next += args.isi
            sleep = t_next - time.perf_counter()
            if sleep > 0:
                time.sleep(sleep)
        time.sleep(max(args.window, 0.2) + 0.5)  # let the last tone record

        # -- 5. Stop recording --------------------------------------------- #
        lr_cli.stop()

        # -- 6. Tear down audio + DAQ -------------------------------------- #
        player.close()
        player = None
        _terminate(cli_proc)
        cli_proc = None

        # -- 7. Analyze ---------------------------------------------------- #
        if not (xdf_path and os.path.exists(xdf_path)):
            raise SystemExit("No XDF produced; cannot analyze.")
        marker_times, mcc_ts, mcc_sig = analysis.load_from_xdf(
            xdf_path, args.mcc_name, mcc_channel=args.channel)

        latencies, info_recs = analysis.compute_latencies(
            marker_times, mcc_ts, mcc_sig,
            window=args.window, pre=args.pre,
            thresh_frac=args.thresh_frac, abs_threshold=args.abs_threshold)
        report(args, latencies, marker_times, mcc_ts, mcc_sig, info_recs, tag)
        return 0

    finally:
        for closer in (
            lambda: player and player.close(),
            lambda: lr_cli and lr_cli.stop(),
            lambda: _terminate(cli_proc),
        ):
            try:
                closer()
            except Exception:
                pass


# --------------------------------------------------------------------------- #
def main(argv=None):
    args = parse_args(argv)
    if args.analyze_xdf:
        return run_analyze_only(args)
    return run_live(args)


if __name__ == "__main__":
    sys.exit(main())
