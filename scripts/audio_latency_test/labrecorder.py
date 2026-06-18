"""Record LSL streams with ``LabRecorderCLI``.

``LabRecorderCLI`` is a headless recorder that resolves the requested streams
once, records until it reads a newline on stdin, and flushes the XDF on exit.
Deterministic and GUI-free; the streams must already exist when it launches (it
does not poll). See :class:`CLISession`.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.abspath(os.path.join(_HERE, "..", ".."))
_LSL_ROOT = os.path.dirname(_REPO)  # sibling dir holding App-LabRecorder, etc.


# --------------------------------------------------------------------------- #
# Discovery
# --------------------------------------------------------------------------- #
# Searched in order; the sibling App-LabRecorder build is the local default.
_KNOWN_CLI = [
    os.path.join(_LSL_ROOT, "App-LabRecorder", "cmake-build-release",
                 "install", "LabRecorderCLI"),
    "/usr/local/LabRecorder/LabRecorderCLI",
    "/usr/local/LabRecorder/LabRecorderCLI.app/Contents/MacOS/LabRecorderCLI",
    "/Applications/LabRecorder.app/Contents/MacOS/LabRecorderCLI",
]


def find_labrecorder_cli(explicit=None):
    """Locate a ``LabRecorderCLI`` binary, or return ``None``.

    Order: explicit arg, ``LABRECORDER_CLI`` env var, ``PATH``, known locations.
    """
    for cand in (explicit, os.environ.get("LABRECORDER_CLI"),
                 shutil.which("LabRecorderCLI"), *_KNOWN_CLI):
        if cand and os.path.exists(cand):
            return cand
    return None


# --------------------------------------------------------------------------- #
# LabRecorderCLI session
# --------------------------------------------------------------------------- #
class CLISession:
    """Run ``LabRecorderCLI`` against a set of stream queries.

    Streams must already be discoverable when ``start()`` is called -- the CLI
    resolves them once and does not poll. ``stop()`` writes a newline to its
    stdin, which makes it flush and close the XDF, then waits for exit.
    """

    def __init__(self, cli_path, xdf_path, queries):
        self.cli_path = cli_path
        self.xdf_path = os.path.abspath(xdf_path)
        self.queries = list(queries)
        self.proc: subprocess.Popen | None = None

    def start(self):
        os.makedirs(os.path.dirname(self.xdf_path), exist_ok=True)
        cmd = [self.cli_path, self.xdf_path, *self.queries]
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        # Give it a moment to resolve streams and bail early if it could not.
        time.sleep(1.0)
        if self.proc.poll() is not None:
            out = self.proc.stdout.read() if self.proc.stdout else ""
            raise RuntimeError(
                f"LabRecorderCLI exited immediately (code {self.proc.returncode}). "
                f"A query likely matched no stream.\n{out}")
        return True

    def stop(self, timeout=10.0):
        if self.proc is None or self.proc.poll() is not None:
            return
        try:
            if self.proc.stdin:
                self.proc.stdin.write("\n")
                self.proc.stdin.flush()
                self.proc.stdin.close()
        except (BrokenPipeError, OSError):
            pass
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
