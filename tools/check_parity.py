#!/usr/bin/env python3
"""Parity harness: fz (C) vs the reference FIS evaluator.

  tools/check_parity.py --fis X.fis --inputs in.txt [--tol 3e-5]
                        [--ref auto|fis2json|octave] [--lib build/libfz.a]
                        [--keep] [-v]

n_points is fixed at 101 (FZ_MAX_POINTS default, the toolkit/`evalfis` default).

Builds a tiny C driver in a temp dir, compiles it with src/*.c (CONTRACT §B/§H),
runs fz_eval_all over every input row, and prints max abs/rel error per output.

Reference (--ref):
  fis2json   tools/fis2json.py --eval  (double-precision Python reference)
  octave     tools/oracle.m via tools/run_oracle.sh, else inline evalfis()
  auto       fis2json when it can evaluate, else octave

Exit 0 within tolerance, 1 on |got-ref| > tol*max(1,|ref|), 2 on build/usage error.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"
TOOLS = REPO / "tools"
DEFAULT_OCTAVE = (
    "/home/ranggawbp/.hermes/cache/scratch/mm/bin/micromamba run "
    "-p /var/tmp/octenv octave --no-gui --quiet"
)
TOOLKIT = os.environ.get("FLT_PATH", "/var/tmp/flt-src/inst")

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fz.h"
#include "fz_fis_parse.h"

#define MAX_VARS 8
#define MAX_MF 256
#define MAX_RULES 512
#define MAX_IDX 8192
#define MAX_LIN 256

static fz_mf_t   mf_pool[MAX_MF];
static fz_var_t  in_pool[MAX_VARS], out_pool[MAX_VARS];
static fz_rule_t rule_pool[MAX_RULES];
static int16_t   idx_pool[MAX_IDX];
static fz_work_t work;
static char      text[65536];
static float     lin_pool[256];

int main(int argc, char **argv)
{
    FILE *fp;
    size_t n;
    int n_in, n_out, i, j;
    float in[MAX_VARS], out[MAX_VARS];
    fz_fis_t fis;

    if (argc < 4) { fprintf(stderr, "usage: %s fis n_in n_out\n", argv[0]); return 2; }
    n_in = atoi(argv[2]);
    n_out = atoi(argv[3]);

    fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    n = fread(text, 1, sizeof(text) - 1, fp);
    fclose(fp);
    text[n] = 0;

    memset(&fis, 0, sizeof fis);
    i = fz_fis_parse_text(text, sizeof text, &fis, mf_pool, MAX_MF,
                          in_pool, MAX_VARS, out_pool, MAX_VARS,
                          rule_pool, MAX_RULES, idx_pool, MAX_IDX,
                          lin_pool, MAX_LIN);
    if (i != 0) { fprintf(stderr, "parse error %d\n", i); return 3; }

    while (1) {
        for (j = 0; j < n_in; j++)
            if (scanf("%f", &in[j]) != 1) return 0;
        fz_eval_all(&fis, in, out, &work);
        for (j = 0; j < n_out; j++)
            printf(j + 1 < n_out ? "%.9g," : "%.9g\n", (double)out[j]);
    }
}
"""


def var_counts(text):
    n_in = n_out = 0
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("[Input"):
            n_in += 1
        elif s.startswith("[Output"):
            n_out += 1
    return n_in, n_out


def octave_cmd():
    return os.environ.get("OCTAVE_CMD", DEFAULT_OCTAVE).split()


def read_csv(text, n_out, what):
    rows = []
    for ln in text.strip().splitlines():
        if not ln.strip():
            continue
        if ln.startswith("error:"):
            raise SystemExit(f"{what} rejected an input row: {ln}")
        vals = [v for v in ln.replace("\t", ",").split(",") if v.strip()]
        if len(vals) != n_out:
            raise SystemExit(f"{what} row has {len(vals)} columns, expected {n_out}: {ln!r}")
        rows.append([float(v) for v in vals])
    return rows


def ref_fis2json(fis_path, inp, n_out, cwd):
    py = TOOLS / "fis2json.py"
    if not py.is_file():
        return None
    cmd = [sys.executable, str(py), str(fis_path), "--eval", "-i", str(inp)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(f"note: fis2json.py could not evaluate, falling back:\n{p.stderr[-800:]}",
              file=sys.stderr)
        return None
    return read_csv(p.stdout, n_out, "fis2json.py")


def ref_octave(fis_path, inp, n_out, cwd):
    csv = cwd / "ref.csv"
    runner = TOOLS / "run_oracle.sh"
    if runner.is_file():
        cmd = [str(runner), str(fis_path), str(inp), "101", str(csv)]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode == 0 and csv.is_file():
            return read_csv(csv.read_text(), n_out, "oracle.m")
        print(f"note: run_oracle.sh failed, using inline evalfis:\n{p.stderr[-800:]}",
              file=sys.stderr)
    script = cwd / "eval.m"
    script.write_text(f"""addpath('{TOOLKIT}');
f = readfis('{fis_path}');
M = dlmread('{inp}');
for i = 1:rows(M)
  y = evalfis(M(i,:), f, 101);
  printf('%.17g,', y(1:end-1));
  printf('%.17g\\n', y(end));
end
""")
    p = subprocess.run(octave_cmd() + [str(script)], capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit(f"octave oracle failed (exit {p.returncode})")
    return read_csv(p.stdout, n_out, "octave evalfis")


def build_harness(tmp, sources, lib=None):
    (tmp / "harness.c").write_text(HARNESS)
    cmd = [os.environ.get("CC", "gcc"), "-std=c11", "-O2", f"-I{SRC}",
           str(tmp / "harness.c")]
    cmd += [str(lib)] if lib else [str(p) for p in sources]
    cmd += ["-lm", "-o", str(tmp / "harness")]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-6000:])
        raise SystemExit(2)
    return tmp / "harness"


def main():
    ap = argparse.ArgumentParser(description="fz vs reference FIS parity check")
    ap.add_argument("--fis", required=True)
    ap.add_argument("--inputs", required=True, help="one input row per line")
    ap.add_argument("--tol", type=float, default=3e-5,
                    help="err <= tol*max(1,|ref|); default 3e-5 (CONTRACT §H)")
    ap.add_argument("--ref", choices=("auto", "fis2json", "octave"), default="auto")
    ap.add_argument("--lib", default=None, help="prebuilt library instead of compiling src/*.c")
    ap.add_argument("--keep", action="store_true", help="keep harness and inputs")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    fis_path = Path(a.fis).resolve()
    n_in, n_out = var_counts(fis_path.read_text(errors="replace"))
    if not n_in or not n_out:
        raise SystemExit(f"cannot determine input/output counts from {fis_path}")

    rows = [[float(v) for v in ln.replace(",", " ").split()]
            for ln in Path(a.inputs).read_text().splitlines()
            if ln.strip() and not ln.lstrip().startswith("#")]
    for r in rows:
        if len(r) != n_in:
            raise SystemExit(f"input row has {len(r)} values, FIS has {n_in} inputs: {r}")
    if not rows:
        raise SystemExit("no input rows")

    tmp = Path(tempfile.mkdtemp(prefix="fz_parity_"))
    skip_cleanup = a.keep
    try:
        lib = Path(a.lib) if a.lib else None
        if lib and not lib.is_file():
            print(f"note: --lib {lib} not found, compiling src/*.c instead", file=sys.stderr)
            lib = None
        sources = []
        if not lib:
            sources = sorted(SRC.glob("*.c")) + sorted(SRC.glob("*.cpp"))
            if not sources:
                raise SystemExit("src/ has no sources yet; nothing to compare")
        exe = build_harness(tmp, sources, lib)

        in_txt = tmp / "in.txt"
        in_txt.write_text("".join(" ".join(repr(v) for v in r) + "\n" for r in rows))
        drv = subprocess.run([str(exe), str(fis_path), str(n_in), str(n_out)],
                             input=in_txt.read_text(), capture_output=True, text=True)
        if drv.returncode != 0:
            sys.stderr.write(drv.stderr)
            raise SystemExit(f"harness failed (exit {drv.returncode})")
        got = read_csv(drv.stdout, n_out, "fz harness")
        if len(got) != len(rows):
            raise SystemExit(f"harness returned {len(got)} rows for {len(rows)} inputs")

        ref = None
        if a.ref in ("auto", "fis2json"):
            ref = ref_fis2json(fis_path, in_txt, n_out, tmp)
            if ref is None and a.ref == "fis2json":
                raise SystemExit("fis2json.py reference unavailable")
        if ref is None:
            ref = ref_octave(fis_path, in_txt, n_out, tmp)
        if len(ref) != len(rows):
            raise SystemExit(f"reference returned {len(ref)} rows, expected {len(rows)}")
    finally:
        if skip_cleanup:
            print(f"harness + inputs kept in {tmp}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)

    max_abs = [0.0] * n_out
    max_rel = [0.0] * n_out
    max_ref = [0.0] * n_out
    worst = [None] * n_out
    for ri, (r_row, g_row) in enumerate(zip(ref, got)):
        for j in range(n_out):
            r, g = r_row[j], g_row[j]
            err = abs(g - r)
            rel = err / max(1.0, abs(r))
            max_ref[j] = max(max_ref[j], abs(r))
            if err > max_abs[j]:
                max_abs[j] = err
            if rel > max_rel[j]:
                max_rel[j] = rel
                worst[j] = (ri, rows[ri], g, r)

    breach = 0
    print(f"{fis_path.name}: {len(rows)} points, n_points=101, tol={a.tol:g} rel")
    print(f"{'output':>6}  {'max_abs':>12}  {'max_rel':>12}  {'max|ref|':>12}  status")
    for j in range(n_out):
        bad = max_rel[j] > a.tol
        breach += bad
        print(f"{j:>6}  {max_abs[j]:>12.3e}  {max_rel[j]:>12.3e}  {max_ref[j]:>12.6g}  "
              f"{'FAIL' if bad else 'ok'}")
        if a.verbose and worst[j]:
            ri, ri_in, g, r = worst[j]
            print(f"       worst row {ri} in={ri_in} got={g!r} ref={r!r}")
    print(f"{'':>6}  overall max_rel = {max(max_rel):.3e}")
    return 1 if breach else 0


if __name__ == "__main__":
    sys.exit(main())
