# Audio ↔ LSL Latency Test (MeasurementComputing)

Measures the latency **and jitter** between an LSL marker that says "a tone was
played" and the moment that tone's waveform actually shows up in the MCC DAQ
data stream. It is the LSL/MeasurementComputing analogue of Blackrock's
[`analog_latency_test.py`](https://github.com/BlackrockNeurotech/orion/blob/dev/Python/examples/analog_latency_test.py).

The primary goal is **minimizing jitter** (run-to-run variation of the latency),
which is what determines how well you can align events to neural/physio data.

## What it does

1. Launches `MCCOutletCLI` streaming **one channel** of a USB-1608FS-Plus at a
   high audio-band rate (default 96 kHz, single-channel max is 100 kHz), full
   ±10 V range (`--mcc-range 5`).
2. Creates an LSL `Markers` stream and plays pure sine bursts at a fixed
   interval, emitting one marker per tone.
3. Records both streams to XDF using **`LabRecorderCLI`** (headless,
   deterministic — it resolves the streams once and records until told to stop),
   auto-detected at `../App-LabRecorder/.../install/LabRecorderCLI`, on `PATH`, or
   via `$LABRECORDER_CLI`. Override the path with `--labrecorder-cli`.
4. Stops after `--n-tones` and measures, per marker, the latency to the tone's
   threshold crossing in the MCC data; prints mean/median latency and the
   jitter (std, peak-to-peak, IQR), plus a plot.

## Physical setup

Connect a computer audio output (headphone jack or audio-interface output) to
**MCC analog input channel 0** and AGND. Keep the signal inside ±10 V. A simple
3.5 mm jack → bare-wire / BNC cable works. Start with a modest output volume and
raise `--amplitude` / system volume until the tone is well above the noise floor
but not clipping the ±10 V range.

## Install

```bash
# from the repo root
python3.13 -m venv .venv
.venv/bin/pip install -r scripts/audio_latency_test/requirements.txt
# pyaudio needs portaudio: brew install portaudio
```

## Run

```bash
cd scripts/audio_latency_test
../../.venv/bin/python run_test.py            # defaults: sd-callback, 96 kHz, 100 tones
```

Useful options:

```bash
# Compare back-ends (the experiment that matters):
run_test.py --method sd-trigger       # marker stamped at trigger time
run_test.py --method sd-callback      # DAC-time stamped (low jitter)   [default]
run_test.py --method pyaudio-callback # PyAudio equivalent of sd-callback

# Tuning:
run_test.py --blocksize 64            # smaller callback block -> lower latency
run_test.py --audio-device "USB"      # pick a specific output device
run_test.py --n-tones 200 --isi 0.5

# Point at a specific LabRecorderCLI:
run_test.py --labrecorder-cli /path/to/LabRecorderCLI

# Re-analyze an existing recording (no hardware):
run_test.py --analyze-xdf recordings/audio_latency_xxx.xdf

# Verify timestamps and exit non-zero if the MCC stream is bad:
run_test.py --analyze-xdf recordings/audio_latency_xxx.xdf --fail-on-bad-timestamps
```

## Timestamp verification

Every run (live or `--analyze-xdf`) prints a **timestamp health check** for the
MCC stream. Unlike the latency analysis — which loads with
`dejitter_timestamps=True` and so *cleans up* the timestamps — this check loads
the **raw** timestamps exactly as the outlet emitted them
(`synchronize_clocks=False, dejitter_timestamps=False`) and reports:

- **negative absolute timestamps** — a genuinely bad clock value;
- **non-monotonic (backward) steps** — consecutive samples whose timestamp goes
  *backwards*. These are the artifact of stamping each chunk's most-recent
  sample with `local_clock()` at read time and letting liblsl back-date the
  rest; when one chunk's back-dated start lands before the previous chunk's end,
  time runs backward. In a consumer that does **not** dejitter (e.g. Orion), or
  across machines, those backward steps are what surface as *negative
  timestamps* / negative inter-sample intervals.

Pass `--fail-on-bad-timestamps` to make the run exit non-zero when the MCC
stream has negative or non-monotonic raw timestamps (useful as a regression
gate once the outlet is fixed to emit monotonic, device-anchored timestamps).

Outputs land in `scripts/audio_latency_test/recordings/`:
`audio_latency_<tag>.xdf`, `latency_<tag>.png`, `latency_<tag>.npz`, and the
`mcc_<tag>.log` CLI log.

## How the marker is time-stamped (and why jitter differs)

All three keep a persistent output stream open and play a pre-loaded tone on a
flag trigger — they differ only in **what timestamp the marker gets**:

| Method | Marker timestamp | Expectation |
|---|---|---|
| `sd-trigger` | `local_clock()` at the `play()` trigger | Includes the buffering between trigger and output → shows the real, naively-logged latency and its jitter |
| `sd-callback` | PortAudio `outputBufferDacTime` (predicted DAC time of the first tone sample) mapped into the LSL clock | Removes buffering jitter → **low jitter** |
| `pyaudio-callback` | PyAudio `output_buffer_dac_time`, same idea | Comparison point for PortAudio vs PyAudio |

A constant clock-offset error shifts the *absolute* latency but cancels out of
the jitter, so the std/peak-to-peak numbers are the meaningful comparison.

## Ideas to reduce jitter further

- Smaller `--blocksize` (e.g. 64/128) and `latency='low'` (already used).
- Pre-built tone buffer + arm/trigger (already done in all players — the tone is
  generated once and triggered by a flag the callback reads).
- Try a dedicated audio interface (more deterministic than built-in audio).
- Raise process/thread priority for the audio callback.
- Use a hard onset (`--ramp 0`, default) for a crisp, consistently-detected edge.

## How latency is measured

For each marker time `tm`, the analyzer looks in `[tm−pre, tm+window]`, estimates
a robust baseline/noise from the pre-marker samples, then finds the first
post-marker sample whose deviation exceeds a threshold (default 20 % of the
burst peak, auto-scaling to the physical level), and linearly interpolates the
crossing time to beat sample quantization. See `analysis.compute_latencies`.

`--window` defaults to **auto** (~95 % of the ISI) and each epoch is clamped to
end just before the next marker, so even a large audio buffer is captured
without bleeding into the following tone. (A fixed small window risks missing a
high-latency tone entirely, leaving the epoch plot showing only baseline noise.)

## Files

| File | Purpose |
|---|---|
| `run_test.py` | Orchestrator / CLI entry point |
| `audio.py` | Tone generation + playback back-ends (the experiment knobs) |
| `labrecorder.py` | LabRecorderCLI discovery + recording session |
| `analysis.py` | Threshold-crossing latency, stats, plots, XDF loader |
