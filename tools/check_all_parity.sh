#!/usr/bin/env bash
# check_all_parity.sh — run check_parity.py (--ref fis2json, no Octave) over
# every golden fixture. Exit non-zero if any fixture breaches tolerance.
# Set FZ_DOUBLE=1 to run the 64-bit build (fz_real_t = double).
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
GOLDEN="$REPO/tests/golden"

TOL="${FZ_TOL:-3e-5}"
DOUBLE="${FZ_DOUBLE:-0}"
FAIL=0
COUNT=0
EXTRA=()
[ "$DOUBLE" = "1" ] && EXTRA+=(--double)

for fis in "$GOLDEN"/*.fis; do
    base="${fis%.fis}"
    inputs="$base.in.txt"
    [ -f "$inputs" ] || { echo "skip $base (no inputs)"; continue; }
    case "$(basename "$fis")" in
        *bisector*)
            echo "skip $(basename "$fis") (grid-quantised bisector; see docs/COMPATIBILITY.md §bisector)"
            continue
            ;;
    esac
    echo "=== $(basename "$fis") ==="
    if python3 "$HERE/check_parity.py" --fis "$fis" --inputs "$inputs" \
        --ref fis2json --tol "$TOL" "${EXTRA[@]}"; then
        :
    else
        rc=$?
        echo "FAIL $base (exit $rc)"
        FAIL=1
    fi
    COUNT=$((COUNT + 1))
done

echo
echo "checked $COUNT golden fixtures, tol=$TOL double=$DOUBLE"
[ "$FAIL" -eq 0 ] || { echo "PARITY FAILURES"; exit 1; }
echo "all parity ok"
