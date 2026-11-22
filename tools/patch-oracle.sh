#!/bin/bash
# patch-oracle.sh — make a fuzzy-logic-toolkit copy behave like MATLAB.
#
# The reference toolkit deviates from the MATLAB Fuzzy Logic Toolbox in four
# places (see docs/COMPATIBILITY.md §3). Generating golden data from the
# unpatched toolkit bakes those bugs into the fixtures, so the oracle must be a
# patched copy. This script builds that copy reproducibly.
#
#   tools/patch-oracle.sh [src-toolkit] [dst-toolkit]
#
# Defaults: /var/tmp/flt-src/inst -> /var/tmp/flt-oracle/inst
# After running, point FLT_PATH at the destination (tools/run_oracle.sh does).
set -eu

SRC=${1:-/var/tmp/flt-src/inst}
DST=${2:-/var/tmp/flt-oracle/inst}

if [ ! -d "$SRC" ]; then
  echo "patch-oracle: source toolkit not found: $SRC" >&2
  echo "  git clone --depth 1 https://github.com/lmarkowsky/fuzzy-logic-toolkit.git /var/tmp/flt-src" >&2
  exit 2
fi

rm -rf "$DST"
mkdir -p "$(dirname "$DST")"
cp -r "$SRC" "$DST"

# 5. The toolkit ships no `probor` at all, so a FIS whose OrMethod is 'probor'
#    cannot be evaluated. MATLAB defines it as a + b - a*b.
cat > "$DST/probor.m" <<'M'
## probor -- probabilistic OR, as defined by the MATLAB Fuzzy Logic Toolbox.
## The reference toolkit omits this method entirely, which makes any FIS with
## OrMethod='probor' unevaluable there.
function retval = probor (x, y = 0)
  if (nargin == 1)
    retval = 0;
    for i = 1 : length (x)
      retval = retval + x(i) - retval * x(i);
    endfor
    return;
  endif
  retval = x + y - x .* y;
endfunction
M
echo "  patched: probor (missing from the toolkit)"

python3 - "$DST" <<'PY'
import pathlib, sys

root = pathlib.Path(sys.argv[1])

def patch(rel, old, new, what):
    p = root / rel
    s = p.read_text()
    if new in s:
        print("  already patched: %s" % what)
        return
    if old not in s:
        raise SystemExit("  FAILED to locate: %s in %s" % (what, rel))
    p.write_text(s.replace(old, new, 1))
    print("  patched: %s" % what)

# 1. MATLAB accepts degenerate shoulders such as [0 0 1 3] and [7 9 10 10].
patch("private/are_mf_params.m",
      """  y = four_reals (params) && ...
      (params(1) < params(2)) && ...
      (params(2) <= params(3)) && ...
      (params(3) < params(4));""",
      """  y = four_reals (params) && ...
      (params(1) <= params(2)) && ...
      (params(2) <= params(3)) && ...
      (params(3) <= params(4));""",
      "trapmf accepts shoulders")

patch("private/are_mf_params.m",
      """  y = three_reals (params) && ...
      (params(1) < params(2)) && ...
      (params(2) < params(3));""",
      """  y = three_reals (params) && ...
      (params(1) <= params(2)) && ...
      (params(2) <= params(3));""",
      "trimf accepts a degenerate point")

# 2. Sugeno aggregation is ALWAYS sum in MATLAB. The toolkit honours the file's
#    AggMethod, which drops duplicate singletons when a file says 'max'.
patch("private/aggregate_output_sugeno.m",
      """      switch (fis_aggmethod)
        case 'sum'""",
      """      switch ('sum')  ## MATLAB: Sugeno always aggregates by sum
        case 'sum'""",
      "sugeno aggregation is always sum")

# 3. defuzz bisector must return the x value, and MATLAB documents the midpoint
#    of the output range when the aggregated area is zero.
patch("defuzz.m",
      "  crisp_x = midpoint;",
      """  crisp_x = x(midpoint);""",
      "bisector returns x, not the array index")

patch("defuzz.m",
      """  half_area = trapz (x, y) / 2;""",
      """  if (trapz (x, y) == 0)
    crisp_x = (x(1) + x(end)) / 2;
    return;
  endif

  half_area = trapz (x, y) / 2;""",
      "bisector zero area -> range midpoint")

# 4. einstein_product's single-argument fold starts at 1 and then folds x(1)
#    again, squaring the first term.
p = root / "einstein_product.m"
s = p.read_text()
if "identity element 1" not in s:
    old = """  elseif (nargin == 1)
    if (isvector (x))
      retval = vector_arg (x);"""
    new = """  elseif (nargin == 1)
    if (isvector (x))
      ## T-norm fold starts from the identity 1, not by re-folding x(1).
      retval = 1;
      for i = 1 : length (x)
        retval = (retval * x(i)) / (2 - (retval + x(i) - retval * x(i)));
      endfor
      return;
      """
    if old not in s:
        raise SystemExit("  FAILED to locate: einstein_product vector fold")
    p.write_text(s.replace(old, new, 1))
    print("  patched: einstein_product fold identity")
else:
    print("  already patched: einstein_product fold identity")
PY

echo "patched oracle at: $DST"
echo "next: FLT_PATH=$DST tools/run_oracle.sh <fis> <inputs> [points] [out.csv]"
