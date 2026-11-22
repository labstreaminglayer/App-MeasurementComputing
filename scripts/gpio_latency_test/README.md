# GPIO timestamp calibration (Raspberry Pi 5)

Measures the latency left in MCCOutlet's timestamps, i.e. the value for `timestamp_offset_us`.

A GPIO pin on the same host is toggled at random 150–350 ms intervals. Each write goes straight to the
RP1 register and is bracketed by `clock_gettime(CLOCK_MONOTONIC)` (liblsl's clock on Linux), with a
read-back confirming the pad level, so every edge's true time is known to well under 1 µs. The pin is
wired to DAQ analog input 1; each edge is located in the recorded LSL stream and its stamp is compared
with the GPIO time.

## Setup

- GPIO16 (header pin 36) → DAQ CH1 input, GND → AGND.
- Build on the Pi (liblsl installed):

```sh
gcc -O2 -Wall -o gpio_edges gpio_edges.c
g++ -O2 -o rec rec.cpp -llsl
```

## Run

```sh
MCC_CLI=/path/to/MCCOutletCLI ./run.sh base 300
```

Output, for example:

```
1201 gpio edges, 1206 crossings, matched 1201; ...
stamp - true (us): mean 96.9 SD 7.4 ...
```

The mean is the remaining latency: put it in `timestamp_offset_us` (or pass `--ts-offset-us`), then
rerun with the offset applied to check that the mean is ~0. ~1000 edges (300 s) give a mean to ~0.2 µs.
The SD is mostly the 20 µs sample spacing at 50 kHz, not timestamp jitter.
