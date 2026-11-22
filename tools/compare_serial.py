#!/usr/bin/env python3
"""Live compare: MCU serial stream vs reference (fis2json.py, MATLAB semantics).

Reads one line per evaluation from serial. Each line: comma-separated floats,
    in1,in2,...,firing1,...,firingN,out1,...,outM
The line format is configured by --format (default: inputs + firing + outputs).
For each line it prints the deviation from the float64 reference, per stage.

Usage:
    python3 tools/compare_serial.py --port /dev/ttyUSB0 --baud 115200 \
        --fis examples/tipper_mamdani/tipper.fis
    python3 tools/compare_serial.py --port COM3 --baud 9600 --fis m.fis \
        --tol 1e-4 --quiet

Options:
    --port   serial port (e.g. /dev/ttyUSB0, COM3)     [required]
    --baud   baud rate (default 115200)
    --fis    path to .fis model used to evaluate reference   [required]
    --tol    relative tolerance (default 1e-4)
    --format inputs|input_firing|input_firing_output
             which columns arrive on the wire (default input_firing_output)
    --quiet  only print failures and a final tally
    --csv    append a CSV log (line, lib, ref, delta_rel, status)
"""
import argparse
import sys
import time

sys.path.insert(0, "tools")

def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--fis", required=True)
    p.add_argument("--tol", type=float, default=1e-4)
    p.add_argument("--format", default="input_firing_output",
                   choices=["inputs", "input_firing", "input_firing_output"])
    p.add_argument("--quiet", action="store_true")
    p.add_argument("--csv")
    return p.parse_args()

def load_model(path):
    from fis2json import parse_fis
    return parse_fis(open(path, encoding="utf-8").read())

def rel_diff(a, b):
    if b == 0.0:
        return abs(a - b) if a != 0.0 else 0.0
    return abs(a - b) / abs(b)

def main():
    args = parse_args()
    fis = load_model(args.fis)
    n_in = len(fis["inputs"])
    n_out = len(fis["outputs"])
    n_rules = len(fis["rules"])

    fmt = args.format
    if fmt == "inputs":
        n_firing = 0
    elif fmt == "input_firing":
        n_firing = n_rules
    else:
        n_firing = n_rules
    expect = n_in + n_firing + n_out

    import serial
    ser = serial.Serial(args.port, args.baud, timeout=2)
    print(f"connected {args.port} @ {args.baud}  expect {expect} cols/line", flush=True)

    tally = {"ok": 0, "fail": 0}
    csv_fh = open(args.csv, "w") if args.csv else None
    if csv_fh:
        csv_fh.write("input,firing,lib,ref,delta_rel,status\n")

    # warm up: drain stale bytes
    time.sleep(1.5)
    ser.reset_input_buffer()

    while True:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").strip()
        if not line or line.startswith("#"):
            continue
        try:
            cols = [float(x) for x in line.split(",")]
        except ValueError:
            print(f"[skip] unparsable: {line!r}", flush=True)
            continue
        if len(cols) != expect:
            print(f"[warn] expected {expect} cols, got {len(cols)}: {line}", flush=True)
            continue

        ins = cols[:n_in]
        firing = cols[n_in:n_in + n_firing] if n_firing else None
        outs = cols[n_in + n_firing:]

        from fis2json import evaluate
        ref_out = evaluate(fis, ins)

        worst = 0.0
        for j, o in enumerate(outs):
            d = rel_diff(o, ref_out[j])
            worst = max(worst, d)
        status = "OK" if worst <= args.tol else "FAIL"
        tally[status.lower()] += 1

        rec = (ins, firing, outs, ref_out, worst)
        if not args.quiet or status == "FAIL":
            print(f"{line}  -> lib={outs} ref={ref_out} Δrel={worst:.2e} {status}", flush=True)
        if csv_fh:
            csv_fh.write(f"{ins},{outs},{ref_out},{worst:.6e},{status}\n")
            csv_fh.flush()

if __name__ == "__main__":
    main()
