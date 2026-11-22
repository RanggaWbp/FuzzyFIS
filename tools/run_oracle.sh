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

# micromamba env that carries the toolkit under Octave 10.3
MAMBA=${MAMBA:-/home/ranggawbp/.hermes/cache/scratch/mm/bin/micromamba}
ENV=${OCTAVE_ENV:-/var/tmp/octenv}
OCTAVE=${OCTAVE_BIN:-}
export FLT_PATH=${FLT_PATH:-/var/tmp/flt-src/inst}

OUT=''
DETAIL=''
if [ "$#" -ge 4 ]; then
  case $4 in /*) OUT=$4 ;; *) OUT=$(pwd)/$4 ;; esac
fi
if [ "$#" -ge 5 ]; then
  case $5 in /*) DETAIL=$5 ;; *) DETAIL=$(pwd)/$5 ;; esac
fi

CALL="addpath('$HERE'); oracle('$FIS','$IN',$N,'$OUT','$DETAIL')"

if [ -n "$OCTAVE" ]; then
  exec "$OCTAVE" --no-gui --quiet --eval "$CALL"
fi
exec "$MAMBA" run -p "$ENV" octave --no-gui --quiet --eval "$CALL"
