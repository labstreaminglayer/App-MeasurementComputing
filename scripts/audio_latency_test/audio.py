"""Tone generators and audio playback back-ends for the latency test.

Each player owns the LSL marker outlet and is responsible for time-stamping the
marker as accurately as it can. The whole point of the experiment is to compare
how well different strategies place that marker relative to when the waveform
actually leaves the DAC (and therefore shows up in the MCC data stream).

Players implemented:

* ``TriggerPlayer`` -- a persistent ``sounddevice`` output stream driven by a
  callback. The tone is *armed* ahead of time and *triggered* by setting a flag;
  the marker is stamped with ``local_clock()`` at the instant ``play()`` is
  called (the trigger), NOT the predicted DAC time. The measured latency
  therefore still includes the audio buffering between trigger and actual
  output -- this is the "what an app would naively log" number, but without the
  per-call device-open cost of ``sounddevice.play()``.

* ``CallbackPlayer``  -- same armed/triggered persistent stream, but the
  callback stamps the marker with PortAudio's ``outputBufferDacTime`` (the
  predicted time the first tone sample hits the DAC) converted into the LSL
  clock. This removes the buffering jitter from the marker timestamp.

* ``PyAudioPlayer`` -- same idea as ``CallbackPlayer`` but on the PyAudio
  back-end, for an apples-to-apples comparison. Only available if ``pyaudio``
  imports.
"""

from __future__ import annotations

import queue
import threading

import numpy as np
from pylsl import local_clock


# --------------------------------------------------------------------------- #
# Tone generation
# --------------------------------------------------------------------------- #
def make_tone(fs: float, freq: float, dur: float, amplitude: float,
              ramp: float = 0.0) -> np.ndarray:
    """Return a mono float32 sine burst.

    A hard onset (``ramp == 0``) gives the crispest edge for threshold
    detection. A short raised-cosine ramp can be used to suppress the onset
    click if it is contaminating the measurement.
    """
    n = int(round(dur * fs))
    t = np.arange(n) / fs
    wave = amplitude * np.sin(2.0 * np.pi * freq * t)
    r = int(round(ramp * fs))
    if r > 0 and 2 * r < n:
        env = np.ones(n)
        up = 0.5 * (1.0 - np.cos(np.pi * np.arange(r) / r))
        env[:r] = up
        env[-r:] = up[::-1]
        wave = wave * env
    return wave.astype(np.float32)


# --------------------------------------------------------------------------- #
# Trigger-time player (armed/triggered, marker stamped at trigger)
# --------------------------------------------------------------------------- #
class TriggerPlayer:
    """Armed/triggered persistent stream; marker stamped at the trigger instant.

    Identical plumbing to :class:`CallbackPlayer` (pre-loaded tone, persistent
    output stream, triggered by a flag the callback reads) but the marker
    timestamp is ``local_clock()`` captured when ``play()`` is called -- it does
    NOT use the predicted DAC time. The latency it measures therefore includes
    the audio buffering between trigger and actual output.

    The marker is pushed from ``play()`` on the calling (main) thread, so the
    audio callback never touches the LSL outlet.
    """

    name = "sd-trigger"

    def __init__(self, fs, tone, outlet, marker_label, device=None,
                 out_channels=2, blocksize=0, latency="low"):
        import sounddevice as sd
        self._sd = sd
        self.fs = fs
        self.tone = tone
        self.tone_len = len(tone)
        self.out_channels = out_channels
        self.outlet = outlet
        self.marker_label = marker_label
        self.marker_times: list[float] = []

        self._pos = None                 # None == idle, else index into tone
        self._trigger = threading.Event()

        self.stream = sd.OutputStream(
            samplerate=fs, channels=out_channels, dtype="float32",
            device=device, blocksize=blocksize, latency=latency,
            callback=self._callback,
        )

    def start(self):
        self.stream.start()

    def close(self):
        self.stream.stop()
        self.stream.close()

    def _callback(self, outdata, frames, time_info, status):
        outdata.fill(0.0)
        if self._pos is None and self._trigger.is_set():
            self._trigger.clear()
            self._pos = 0
        if self._pos is not None:
            n = min(frames, self.tone_len - self._pos)
            outdata[:n, :] = self.tone[self._pos:self._pos + n, None]
            self._pos += n
            if self._pos >= self.tone_len:
                self._pos = None

    def play(self):
        # Capture the timestamp and arm the trigger back-to-back so the marker
        # time reflects the trigger as closely as possible; the (slower) LSL
        # push and bookkeeping happen afterwards.
        t0 = local_clock()
        self._trigger.set()
        self.outlet.push_sample([self.marker_label], t0)
        self.marker_times.append(t0)


# --------------------------------------------------------------------------- #
# sounddevice callback player (DAC-time stamped)
# --------------------------------------------------------------------------- #
class CallbackPlayer:
    """Persistent output stream; marker stamped with the predicted DAC time.

    The marker is pushed from a dispatcher thread (fed by a queue) so the audio
    callback never blocks on the LSL outlet.
    """

    name = "sd-callback"

    def __init__(self, fs, tone, outlet, marker_label, device=None,
                 out_channels=2, blocksize=0, latency="low"):
        import sounddevice as sd
        self._sd = sd
        self.fs = fs
        self.tone = tone
        self.tone_len = len(tone)
        self.out_channels = out_channels
        self.outlet = outlet
        self.marker_label = marker_label
        self.marker_times: list[float] = []

        self._pos = None                 # None == idle, else index into tone
        self._trigger = threading.Event()
        self._mq: "queue.Queue[float | None]" = queue.Queue()
        self.offset = 0.0                # local_clock() - stream.time

        self.stream = sd.OutputStream(
            samplerate=fs, channels=out_channels, dtype="float32",
            device=device, blocksize=blocksize, latency=latency,
            callback=self._callback,
        )
        self._dispatch = threading.Thread(target=self._dispatcher, daemon=True)

    # -- lifecycle --------------------------------------------------------- #
    def start(self):
        self._dispatch.start()
        self.stream.start()
        self._calibrate_clock()

    def close(self):
        try:
            self.stream.stop()
            self.stream.close()
        finally:
            self._mq.put(None)

    # -- clock alignment --------------------------------------------------- #
    def _calibrate_clock(self, n=200):
        """Estimate offset between PortAudio stream time and the LSL clock.

        A constant offset error does not affect jitter (it cancels per marker);
        it only shifts the absolute latency. We still average many reads to keep
        the absolute number meaningful.
        """
        diffs = []
        for _ in range(n):
            t_lsl = local_clock()
            t_pa = self.stream.time
            diffs.append(t_lsl - t_pa)
        self.offset = float(np.median(diffs))

    # -- audio callback ---------------------------------------------------- #
    def _callback(self, outdata, frames, time_info, status):
        outdata.fill(0.0)
        if self._pos is None and self._trigger.is_set():
            self._trigger.clear()
            self._pos = 0
            # outputBufferDacTime is the predicted DAC time of the FIRST frame
            # in this block -- which is exactly the first tone sample we write.
            t0 = time_info.outputBufferDacTime + self.offset
            self._mq.put(t0)
        if self._pos is not None:
            n = min(frames, self.tone_len - self._pos)
            outdata[:n, :] = self.tone[self._pos:self._pos + n, None]
            self._pos += n
            if self._pos >= self.tone_len:
                self._pos = None

    def _dispatcher(self):
        while True:
            t0 = self._mq.get()
            if t0 is None:
                break
            self.outlet.push_sample([self.marker_label], t0)
            self.marker_times.append(t0)

    # -- trigger ----------------------------------------------------------- #
    def play(self):
        self._trigger.set()


# --------------------------------------------------------------------------- #
# PyAudio callback player (DAC-time stamped)
# --------------------------------------------------------------------------- #
class PyAudioPlayer:
    """PyAudio equivalent of :class:`CallbackPlayer`."""

    name = "pyaudio-callback"

    def __init__(self, fs, tone, outlet, marker_label, device=None,
                 out_channels=2, blocksize=256):
        import pyaudio
        self._pa_mod = pyaudio
        self.fs = int(fs)
        self.tone = tone
        self.tone_len = len(tone)
        self.out_channels = out_channels
        self.outlet = outlet
        self.marker_label = marker_label
        self.blocksize = blocksize
        self.marker_times: list[float] = []

        self._pos = None
        self._trigger = threading.Event()
        self._mq: "queue.Queue[float | None]" = queue.Queue()
        self.offset = 0.0

        self._pa = pyaudio.PyAudio()
        self._stream = self._pa.open(
            format=pyaudio.paFloat32, channels=out_channels, rate=self.fs,
            output=True, output_device_index=device,
            frames_per_buffer=blocksize, stream_callback=self._callback,
            start=False,
        )
        self._dispatch = threading.Thread(target=self._dispatcher, daemon=True)
        self._silence = np.zeros((blocksize, out_channels), dtype=np.float32)

    def start(self):
        self._dispatch.start()
        self._stream.start_stream()
        self._calibrate_clock()

    def _calibrate_clock(self, n=200):
        diffs = []
        for _ in range(n):
            t_lsl = local_clock()
            t_pa = self._stream.get_time()
            diffs.append(t_lsl - t_pa)
        self.offset = float(np.median(diffs))

    def _callback(self, in_data, frame_count, time_info, status):
        if frame_count != self.blocksize:
            out = np.zeros((frame_count, self.out_channels), dtype=np.float32)
        else:
            out = self._silence.copy()
        if self._pos is None and self._trigger.is_set():
            self._trigger.clear()
            self._pos = 0
            t0 = time_info["output_buffer_dac_time"] + self.offset
            self._mq.put(t0)
        if self._pos is not None:
            n = min(frame_count, self.tone_len - self._pos)
            out[:n, :] = self.tone[self._pos:self._pos + n, None]
            self._pos += n
            if self._pos >= self.tone_len:
                self._pos = None
        return (out.tobytes(), self._pa_mod.paContinue)

    def _dispatcher(self):
        while True:
            t0 = self._mq.get()
            if t0 is None:
                break
            self.outlet.push_sample([self.marker_label], t0)
            self.marker_times.append(t0)

    def play(self):
        self._trigger.set()

    def close(self):
        try:
            self._stream.stop_stream()
            self._stream.close()
            self._pa.terminate()
        finally:
            self._mq.put(None)


# --------------------------------------------------------------------------- #
# Factory
# --------------------------------------------------------------------------- #
PLAYERS = {
    TriggerPlayer.name: TriggerPlayer,
    CallbackPlayer.name: CallbackPlayer,
    PyAudioPlayer.name: PyAudioPlayer,
}


def make_player(method, fs, tone, outlet, marker_label, **kwargs):
    try:
        cls = PLAYERS[method]
    except KeyError:
        raise SystemExit(f"Unknown audio method '{method}'. "
                         f"Choices: {', '.join(PLAYERS)}")
    return cls(fs, tone, outlet, marker_label, **kwargs)
