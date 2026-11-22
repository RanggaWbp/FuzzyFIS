#!/usr/bin/env python3
"""Convert legacy .fis files to the canonical format fz parses.

Handles files saved by R2018a and older / hand-edited files:
  * CRLF or CR line endings, UTF-8 BOM, non-ASCII in comments
  * `Version=2.0` / `Version = 1.0` system key (dropped: it is ignored anyway)
  * `MF1='name':'type',[a b c]` with quoted names, missing spaces, tabs
  * `MF1 = 'name' : 'trapmf' , [ a b c ]`
  * rules written without the `(weight) : conn` tail, or with `&`/`|`
  * `%` and `#` comments

Output is normalised (`Key=value` no spaces, `MFn='name':'type',[p ...]`,
rules one per line) and is a strict subset of what src/fz_fis_parse.c accepts.

  tools/convert_fis.py in.fis [-o out.fis] [--check]
  tools/convert_fis.py --all DIR [-o OUTDIR]        # batch convert a directory

`--check` parses without writing and exits 1 on a malformed file.
Exit codes: 0 ok, 1 malformed input, 2 usage/IO error.
"""
import argparse
import io
import re
import sys
from pathlib import Path

SECTIONS = re.compile(r"^\[(System|Input\d+|Output\d+|Rules)\]\s*$", re.I)
KEYVAL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")
MF = re.compile(
    r"^MF(\d+)\s*=\s*'?([^':]*)'?\s*:\s*'?([A-Za-z0-9_]+)'?\s*,\s*\[([^\]]*)\]\s*$", re.I)
RULE_TAIL = re.compile(r"\(\s*([0-9.eE+-]+)\s*\)\s*:\s*([12])\s*$")


def strip_comment(line):
    out = []
    quote = False
    for ch in line:
        if ch == "'" and (not out or out[-1] != "\\"):
            quote = not quote
        if not quote and ch in "%#":
            break
        out.append(ch)
    return "".join(out).strip()


def norm_params(body):
    toks = [t for t in re.split(r"[,\s]+", body.strip()) if t]
    return " ".join(toks)


def convert(text):
    text = text.lstrip("\ufeff").replace("\r\n", "\n").replace("\r", "\n")
    out = []
    section = None
    for raw in text.split("\n"):
        line = strip_comment(raw)
        if not line:
            continue
        m = SECTIONS.match(line)
        if m:
            raw = m.group(1)
            section = raw[0].upper() + raw[1:].lower()   # System / Input1 / Output2 / Rules
            out.append(f"[{section}]")
            section = section.lower()
            continue
        if section is None:
            raise ValueError(f"content before any [Section]: {line!r}")
        if section == "system":
            m = KEYVAL.match(line)
            if not m:
                raise ValueError(f"bad system line: {line!r}")
            key, val = m.group(1), m.group(2).strip()
            if key.lower() == "version":
                continue                  # canonical position is right after Name
            out.append(f"{key}={val}")
            if key.lower() == "type":
                out.append("Version=2.0")  # readfis wants Name, Type, Version in order
            continue
        if section == "rules":
            rule = line.replace("&", " ").replace("|", " ")
            if not RULE_TAIL.search(rule):
                rule = rule + " (1) : 1"
            out.append(re.sub(r"\s+", " ", rule).replace(" ,", ","))
            continue
        # [Input#] / [Output#]
        m = MF.match(line)
        if m:
            idx, var, kind, body = m.groups()
            var = var.replace("'", "").strip()
            out.append(f"MF{idx}='{var}':'{kind.lower()}',[{norm_params(body)}]")
            continue
        m = KEYVAL.match(line)
        if not m:
            raise ValueError(f"bad variable line in [{section}]: {line!r}")
        key, val = m.group(1), re.sub(r"\s+", " ", m.group(2).strip())
        if key.lower() == "range":
            rng = re.match(r"^\[\s*([^\]\s]+)\s+([^\]\s]+)\s*\]$", val)
            if not rng:
                raise ValueError(f"bad Range: {val!r}")
            lo, hi = rng.groups()
            val = f"[{lo} {hi}]"
        out.append(f"{key}={val}")
    if not out:
        raise ValueError("empty file")
    return "\n".join(out) + "\n"


def one(path, out_path, check):
    try:
        src = Path(path).read_text(encoding="utf-8", errors="replace")
        res = convert(src)
    except (OSError, ValueError) as e:
        print(f"{path}: {e}", file=sys.stderr)
        return 1
    if check:
        print(f"{path}: ok ({len(res.splitlines())} lines)")
        return 0
    dst = Path(out_path) if out_path else Path(path).with_suffix(".canon.fis")
    if dst.resolve() == Path(path).resolve():
        raise SystemExit("refusing to overwrite input in place; pass -o")
    dst.write_text(res, encoding="utf-8")
    print(f"{path} -> {dst}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("fis", nargs="?", help="input .fis")
    ap.add_argument("-o", "--output", help="output path (single file or directory)")
    ap.add_argument("--all", metavar="DIR", help="convert every *.fis in DIR")
    ap.add_argument("--check", action="store_true", help="validate only, write nothing")
    a = ap.parse_args()

    if a.all:
        d = Path(a.all)
        files = sorted(d.glob("*.fis"))
        if not files:
            print(f"no .fis in {d}", file=sys.stderr)
            return 2
        rc = 0
        for f in files:
            outp = None
            if a.output:
                outp = Path(a.output) / f.name
                outp.parent.mkdir(parents=True, exist_ok=True)
            rc |= one(f, outp, a.check)
        return rc
    if not a.fis:
        ap.error("need a .fis file or --all DIR")
    return one(a.fis, a.output, a.check)


if __name__ == "__main__":
    sys.exit(main())
