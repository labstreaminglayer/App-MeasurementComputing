#!/bin/sh
# run.sh LABEL SECONDS [MCCOutletCLI args...]
# Streams DAQ ch0-1 (+/-5 V, raw, 50 kHz), records ch1, toggles GPIO16 (wired to ch1) for SECONDS,
# then prints stamp - true edge time. Extra args go to MCCOutletCLI (e.g. --ts-offset-us 95).
# Env: MCC_CLI (default: MCCOutletCLI on PATH). Needs sudo for /dev/gpiomem0.
set -e
cd "$(dirname "$0")" && mkdir -p runs
L=$1 S=$2; shift 2
${MCC_CLI:-MCCOutletCLI} --low-chan 0 --high-chan 1 --rate 50000 --range 6 --raw "$@" > runs/$L.app.log 2>&1 & P=$!
sleep 4
./rec runs/$L.bin $((S + 4)) 1 > runs/$L.rec.log 2>&1 & R=$!
sleep 2
sudo ./gpio_edges runs/$L.gpio.csv "$S" 7 > runs/$L.gpio.log 2>&1
wait $R; kill -INT $P; wait $P || true
python3 ana.py runs/$L.bin runs/$L.gpio.csv | tee runs/$L.txt
