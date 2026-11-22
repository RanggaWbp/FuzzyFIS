#!/usr/bin/env bash
# check_all_parity.sh — run check_parity.py (--ref fis2json, no Octave) over
# every golden fixture. Exit non-zero if any fixture breaches tolerance.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
GOLDEN="$REPO/tests/golden"

TOL="${FZ_TOL:-3e-5}"
FAIL=0
COUNT=0

for fis in "$GOLDEN"/*.fis; do
    base="${fis%.fis}"
    inputs="$base.in.txt"
    [ -f "$inputs" ] || { echo "skip $base (no inputs)"; continue; }
    echo "=== $(basename "$fis") ==="
    if python3 "$HERE/check_parity.py" --fis "$fis" --inputs "$inputs" \
        --ref fis2json --tol "$TOL"; then
        :
    else
        rc=$?
        echo "FAIL $base (exit $rc)"
        FAIL=1
    fi
    COUNT=$((COUNT + 1))
done

echo
echo "checked $COUNT golden fixtures, tol=$TOL"
[ "$FAIL" -eq 0 ] || { echo "PARITY FAILURES"; exit 1; }
echo "all parity ok"
