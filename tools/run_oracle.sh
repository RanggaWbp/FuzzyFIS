#!/bin/sh
# run_oracle.sh -- one-shot wrapper around tools/oracle.m.
#
#   tools/run_oracle.sh MODEL.fis INPUTS.txt [NUM_POINTS] [OUT.csv] [DETAIL.json]
#
# Prints CSV to stdout when OUT.csv is omitted.  Override the interpreter and
# the toolkit location with OCTAVE_BIN / FLT_PATH.
set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: $0 MODEL.fis INPUTS.txt [NUM_POINTS] [OUT.csv] [DETAIL.json]" >&2
  exit 2
fi

HERE=$(cd "$(dirname "$0")" && pwd)
FIS=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
IN=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
N=${3:-101}

# Octave comes from a relocatable conda env alongside the repo when it exists,
# so the checkout carries its own oracle and does not depend on the host's
# package manager. FLT_PATH points at the PATCHED toolkit (tools/patch-oracle.sh).
REPO=$(cd "$HERE/.." && pwd)
ENV=${OCTAVE_ENV:-$REPO/../../tools/octenv}
[ -d "$ENV" ] || ENV=${OCTAVE_ENV:-/var/tmp/octenv}
OCTAVE=${OCTAVE_BIN:-$ENV/bin/octave-cli-10.3.0}
export OCTAVE_HOME=${OCTAVE_HOME:-$ENV}
export FLT_PATH=${FLT_PATH:-$REPO/../../tools/flt-oracle/inst}

if [ ! -x "$OCTAVE" ]; then
  echo "run_oracle: no Octave at $OCTAVE" >&2
  echo "  set OCTAVE_BIN, or install the env: see docs/README.md" >&2
  exit 3
fi

OUT=''
DETAIL=''
if [ "$#" -ge 4 ]; then
  case $4 in /*) OUT=$4 ;; *) OUT=$(pwd)/$4 ;; esac
fi
if [ "$#" -ge 5 ]; then
  case $5 in /*) DETAIL=$5 ;; *) DETAIL=$(pwd)/$5 ;; esac
fi

CALL="addpath('$HERE'); oracle('$FIS','$IN',$N,'$OUT','$DETAIL')"

exec "$OCTAVE" --no-gui --quiet --eval "$CALL"
