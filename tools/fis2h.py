#!/usr/bin/env python3
"""fis2h.py -- compile-time .fis -> C header codegen (CONTRACT.md section G).

Emits `static const` MF/var/rule/ante/conse tables plus one `fz_fis_t`, so a
FIS can live in .rodata on targets with no filesystem and no heap.

  python3 tools/fis2h.py examples/tipper_mamdani/tipper.fis -o out/tipper_fis.h
  python3 tools/fis2h.py tipper.fis --name tipper --points 201 -o -

Output compiles as C99 and C++11 with -Wall -Wextra -Werror -pedantic.
Exit 0 on success, 1 on any parse/validation error (errors go to stderr).
"""

import argparse
import os
import re
import sys

# --- contract tables (section B / D / E / F) -------------------------------

# Constant names must match src/fz_types.h exactly: the generated header is
# compiled against it, and a mismatch is a hard error rather than a silent skew.
MF_TYPES = {
    "trimf": "FZ_MF_TRIMF", "trapmf": "FZ_MF_TRAPMF", "gaussmf": "FZ_MF_GAUSSMF",
    "gauss2mf": "FZ_MF_GAUSS2MF", "gbellmf": "FZ_MF_GBELLMF",
    "sigmf": "FZ_MF_SIGMF", "dsigmf": "FZ_MF_DSIGMF", "psigmf": "FZ_MF_PSIGMF",
    "zmf": "FZ_MF_ZMF", "smf": "FZ_MF_SMF", "pimf": "FZ_MF_PIMF",
    "linsmf": "FZ_MF_LINSMF", "linzmf": "FZ_MF_LINZMF",
    "constant": "FZ_MF_CONSTANT", "linear": "FZ_MF_LINEAR",
}
# fixed parameter counts; None = depends on the number of inputs (linear)
MF_PARAMS = {
    "trimf": 3, "trapmf": 4, "gaussmf": 2, "gauss2mf": 4, "gbellmf": 3,
    "sigmf": 2, "dsigmf": 4, "psigmf": 4, "zmf": 2, "smf": 2, "pimf": 4,
    "linsmf": 2, "linzmf": 2, "constant": 1, "linear": None,
}
OPS = {
    "min": "FZ_OP_MIN", "max": "FZ_OP_MAX", "prod": "FZ_OP_PROD",
    "sum": "FZ_OP_SUM", "probor": "FZ_OP_PROBOR",
    "algebraic_product": "FZ_OP_ALGEBRAIC_PRODUCT",
    "algebraic_sum": "FZ_OP_ALGEBRAIC_SUM",
    "bounded_sum": "FZ_OP_BOUNDED_SUM",
    "einstein_product": "FZ_OP_EINSTEIN_PRODUCT",
    "einstein_sum": "FZ_OP_EINSTEIN_SUM",
    "hamacher_product": "FZ_OP_HAMACHER_PRODUCT",
    "hamacher_sum": "FZ_OP_HAMACHER_SUM",
}
DEFUZZ = {
    "centroid": "FZ_DEFUZZ_CENTROID", "bisector": "FZ_DEFUZZ_BISECTOR",
    "mom": "FZ_DEFUZZ_MOM", "som": "FZ_DEFUZZ_SOM", "lom": "FZ_DEFUZZ_LOM",
    "wtaver": "FZ_DEFUZZ_WTAVER", "wtsum": "FZ_DEFUZZ_WTSUM",
}
DEFAULT_POINTS = 101  # section B: n_points default 101
MAX_MF_PARAMS = 6    # fz_mf_t.p[FZ_MAX_MF_PARAMS]


class FisError(Exception):
    pass


def die(msg, lineno=None, path=""):
    where = "%s:%s: " % (path, lineno) if path and lineno else ("%s: " % path if path else "")
    raise FisError(where + msg)


# --- .fis parser (section F) ----------------------------------------------

def parse_fis(path):
    raw = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            raw = fh.readlines()
    except OSError as exc:
        die(str(exc))

    sysd = {}
    vars_ = []          # list of dicts, in [Input#] / [Output#] order
    rules = []
    cur = None          # current variable dict or "system"
    section = None
    counts = {"in": 0, "out": 0}

    for lineno, line in enumerate(raw, 1):
        line = line.split("%", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            if not line.endswith("]"):
                die("unterminated section header", lineno, path)
            name = line[1:-1].strip()
            low = name.lower()
            if low == "system":
                section, cur = "system", sysd
            elif low == "rules":
                section, cur = "rules", None
            elif low.startswith("input") or low.startswith("output"):
                kind = "in" if low.startswith("input") else "out"
                counts[kind] += 1
                cur = {"kind": kind, "num": counts[kind], "name": "",
                       "lo": 0.0, "hi": 1.0, "mfs": [], "lineno": lineno}
                vars_.append(cur)
                section = "var"
            else:
                section, cur = "ignore", None
            continue

        if section == "rules":
            rules.append((lineno, line))
            continue
        if section == "ignore" or cur is None:
            continue
        if "=" not in line:
            die("expected Key=value", lineno, path)
        key, val = line.split("=", 1)
        key, val = key.strip(), unquote(val.strip())

        if section == "system":
            sysd[key.lower()] = val
            continue
        # variable section
        if key.lower().startswith("mf"):
            if not re.fullmatch(r"MF\d+", key, re.IGNORECASE):
                die("bad membership key %r" % key, lineno, path)
            cur["mfs"].append(parse_mf(val, lineno, path))
        elif key.lower() == "range":
            cur["lo"], cur["hi"] = parse_range(val, lineno, path)
        elif key.lower() == "name":
            cur["name"] = val
        elif key.lower() == "nummfs":
            pass  # cross-checked below
        else:
            die("unknown variable key %r" % key, lineno, path)

    if not sysd:
        die("%s: missing [System]" % path)
    return finalize(sysd, vars_, rules, path)


def unquote(s):
    if len(s) >= 2 and s[0] == s[-1] and s[0] in "'\"":
        return s[1:-1]
    return s


def parse_range(val, lineno, path):
    body = val.strip().strip("[]")
    parts = re.split(r"[\s,]+", body.strip())
    if len(parts) != 2:
        die("Range needs [lo hi]", lineno, path)
    return float(parts[0]), float(parts[1])


def parse_mf(val, lineno, path):
    # 'name':'type',[p1 p2 ...]   (quotes optional on name and type)
    m = re.match(r"^(?:'([^']*)'|([^:]*)):\s*'?([A-Za-z0-9_]+)'?\s*,\s*\[(.*)\]\s*$", val)
    if not m:
        die("bad MF definition %r" % val, lineno, path)
    name = (m.group(1) if m.group(1) is not None else m.group(2)).strip()
    mtype = m.group(3).strip().lower()
    if mtype not in MF_TYPES:
        die("unknown MF type %r" % mtype, lineno, path)
    body = m.group(4).strip()
    params = [float(p) for p in re.split(r"[\s,]+", body) if p] if body else []
    return {"name": name, "type": mtype, "p": params, "lineno": lineno}


def parse_rule(lineno, line, n_in, path):
    """`MF_in1 MF_in2 ..., MF_out1 ... , (weight) : conn` -> (ante, conse, w, conn)."""
    parts = line.split(":")
    if len(parts) > 2:
        die("bad rule (extra ':')", lineno, path)
    conn = 1
    if len(parts) == 2:
        if not parts[1].strip():
            die("bad rule connection", lineno, path)
        conn = int(float(parts[1].strip()))
        if conn not in (1, 2):
            die("conn must be 1 (AND) or 2 (OR)", lineno, path)
    body = parts[0]
    weight = 1.0
    m = re.search(r"\(([^)]*)\)", body)
    if m:
        weight = float(m.group(1).strip() or 1.0)
        body = body[:m.start()] + " " + body[m.end():]
    groups = [g.strip() for g in body.split(",")]
    ante = [t for t in groups[0].split() if t]
    conse = [t for g in groups[1:] for t in g.split() if t]
    if len(ante) != n_in:
        die("rule has %d antecedent tokens, system has %d inputs"
            % (len(ante), n_in), lineno, path)
    enc = lambda toks: [encode_token(t, lineno, path) for t in toks]
    return enc(ante), enc(conse), weight, conn


def encode_token(tok, lineno, path):
    """0 omitted | sign = NOT | |v| = MF index | fraction/10 = hedge.

    Stored scaled by 100, matching src/fz_eval.c (fz_slot_decode: mf_index =
    mag/100, hedge = (mag%100)/10) and src/fz_fis_parse.c (whole*100 + frac).
    So "2.2" -> 220 -> MF2 with hedge 2, and "3" -> 300 -> MF3, no hedge.
    """
    try:
        v = float(tok)
    except ValueError:
        die("bad rule token %r" % tok, lineno, path)
    a = abs(v)
    idx = int(a)                                  # MATLAB fix(): truncate
    # Decode side is hedge = (mag % 100) / 10, so the stored low two digits are
    # 10 * hedge. The toolkit's hedge is round(100 * frac) / 10, hence
    # low_digits = round(100 * frac). Keeping the raw rounded value is what makes
    # "1.05" (hedge 0.5) distinguishable from "1" (no hedge); rounding to a whole
    # tenth first would collapse both to the same slot.
    frac100 = int(round(100.0 * (a - idx)))
    if not 0 <= idx <= 255 or not 0 <= frac100 <= 99:
        die("rule token %r out of range" % tok, lineno, path)
    enc = idx * 100 + frac100
    return -enc if v < 0 else enc


def finalize(sysd, vars_, rules, path):
    fis_type = (sysd.get("type") or "mamdani").strip().lower()
    if fis_type not in ("mamdani", "sugeno"):
        die("Type must be mamdani or sugeno", None, path)
    inputs = [v for v in vars_ if v["kind"] == "in"]
    outputs = [v for v in vars_ if v["kind"] == "out"]
    n_in, n_out = len(inputs), len(outputs)
    if n_in == 0 or n_out == 0:
        die("need at least one [Input#] and one [Output#]", None, path)
    for key, n in (("numinputs", n_in), ("numoutputs", n_out),
                   ("numrules", len(rules))):
        if key in sysd and int(sysd[key]) != n:
            die("%s=%s but found %d" % (key, sysd[key], n), None, path)
    if not rules:
        die("no rules", None, path)

    for v in vars_:
        if not v["mfs"]:
            die("%s%d has no MFs" % (v["kind"], v["num"]), v["lineno"], path)
        if v["lo"] >= v["hi"]:
            die("%s%d Range is empty" % (v["kind"], v["num"]), v["lineno"], path)
        for mf in v["mfs"]:
            want = MF_PARAMS[mf["type"]]
            if want is None:                       # linear: [a1..an c]
                want = (n_in if v["kind"] == "out" else 1) + 1
            if len(mf["p"]) != want:
                die("MF %r(%s) needs %d params, got %d"
                    % (mf["name"], mf["type"], want, len(mf["p"])), mf["lineno"], path)
            if len(mf["p"]) > MAX_MF_PARAMS:
                die("MF %r has more params than fz_mf_t holds"
                    % mf["name"], mf["lineno"], path)

    parsed = [parse_rule(ln, txt, n_in, path) for ln, txt in rules]

    # A rule slot past the variable's MF count is out-of-bounds at eval time.
    for (ante, conse, _w, _c), lineno in zip(parsed, [r[0] for r in rules]):
        for tok, var in zip(ante, inputs):
            idx = abs(tok) // 100
            if idx > len(var["mfs"]):
                die("rule references MF%d on input %r, which has %d MFs"
                    % (idx, var["name"], len(var["mfs"])), lineno, path)
        for tok, var in zip(conse, outputs):
            idx = abs(tok) // 100
            if idx > len(var["mfs"]):
                die("rule references MF%d on output %r, which has %d MFs"
                    % (idx, var["name"], len(var["mfs"])), lineno, path)

    # Mamdani needs shape MFs; Sugeno needs constant/linear. Mixing them yields
    # zeros at run time, so reject it here instead.
    sugeno_mfs = ("constant", "linear")
    for var in outputs:
        for mf in var["mfs"]:
            if fis_type == "sugeno" and mf["type"] not in sugeno_mfs:
                die("Sugeno output %r uses MF %r of type %r; only constant/linear "
                    "are valid" % (var["name"], mf["name"], mf["type"]), mf["lineno"], path)
            if fis_type == "mamdani" and mf["type"] in sugeno_mfs:
                die("Mamdani output %r uses MF %r of type %r; use a shape MF "
                    "or set Type='sugeno'" % (var["name"], mf["name"], mf["type"]),
                    mf["lineno"], path)
    return {
        "name": sysd.get("name") or os.path.splitext(os.path.basename(path))[0],
        "type": fis_type, "inputs": inputs, "outputs": outputs,
        "rules": parsed, "sys": sysd, "path": path,
    }


# --- emission -------------------------------------------------------------

def c_ident(s):
    s = re.sub(r"[^0-9A-Za-z_]", "_", s.strip().lower()) or "fis"
    if s[0].isdigit():
        s = "_" + s
    return s


def c_str(s, name_len):
    # FZ_NAME_LEN counts bytes incl. NUL, so keep name_len-1 chars.
    s = s[: name_len - 1]
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def c_float(v):
    s = format(float(v), ".9g")
    if "e" not in s and "E" not in s and "." not in s:
        s += ".0"
    return s + "f"


def emit(fis, base, name_len, points, source, warnings=None):
    up = base.upper()
    # fz_var_t.name and fz_fis_t.name are char[FZ_NAME_LEN]; the FIS name counts too.
    names = [fis["name"]] + [v["name"] for v in fis["inputs"] + fis["outputs"]]
    max_len = min(max([len(n) for n in names] + [1]), name_len - 1)
    if warnings is not None:
        for n in names:
            if len(n) > name_len - 1:
                warnings.append("name %r truncated to %d chars (FZ_NAME_LEN=%d)"
                                % (n, name_len - 1, name_len))
    out = []
    w = out.append
    w("/* Generated by tools/fis2h.py from %s -- do not edit. */" % source)
    w("/* Contract: CONTRACT.md section G. */")
    w("#ifndef FZ_%s_FIS_H" % up)
    w("#define FZ_%s_FIS_H" % up)
    w("")
    w('#include "fz_types.h"')
    w("")
    w("#ifndef FZ_NAME_LEN")
    w("#define FZ_NAME_LEN %d" % name_len)
    w("#endif")
    w("")
    w("#ifndef FZ_FIS_HAS_NAME")
    w("#define FZ_FIS_HAS_NAME 1 /* fz_fis_t has a name field (src/fz_types.h) */")
    w("#endif")
    w("")
    w("/* fz_var_t.name is char[FZ_NAME_LEN]; fail loudly rather than truncate at use. */")
    w("#if FZ_NAME_LEN < %d" % (max_len + 1))
    w('#error "FZ_NAME_LEN is too small for the names in this FIS"')
    w("#endif")
    w("")
    w("#ifndef FZ_GEN_UNUSED")
    w("#if defined(__GNUC__) || defined(__clang__)")
    w("#define FZ_GEN_UNUSED __attribute__((unused))")
    w("#else")
    w("#define FZ_GEN_UNUSED")
    w("#endif")
    w("#endif")
    w("")
    if points != DEFAULT_POINTS:
        w("/* This FIS needs %d sample points: build with -DFZ_MAX_POINTS>=%d. */"
          % (points, points))
        w("#define FZ_%s_MAX_POINTS %d" % (up, points))
    # A too-small work buffer would silently shrink the grid, so refuse to build.
    w("#if defined(FZ_MAX_POINTS) && (FZ_MAX_POINTS < %d)" % points)
    w('#error "FZ_MAX_POINTS is too small for this FIS: raise -DFZ_MAX_POINTS or regenerate with fis2h.py --points <n>"')
    w("#endif")
    w("")

    for group in ("in", "out"):
        for k, var in enumerate(fis[group + "puts"]):
            tag = "%s_%s%d" % (base, group, k)
            mfs = var["mfs"]
            w("static const fz_mf_t %s_mf[%d] = {" % (tag, len(mfs)))
            for mf in mfs:
                pad = MAX_MF_PARAMS - len(mf["p"])
                p = [c_float(x) for x in mf["p"]] + ["0.0f"] * pad
                w("    { %s, %d, { %s } }," % (MF_TYPES[mf["type"]], len(mf["p"]),
                                               ", ".join(p)))
            w("};")
            w("")
    for group, label in (("in", "input"), ("out", "output")):
        w("static const fz_var_t %s_%s[%d] = {"
          % (base, group, len(fis[group + "puts"])))
        for k, var in enumerate(fis[group + "puts"]):
            w("    { %s, %s, %s, %d, %s_%s%d_mf },"
              % (c_str(var["name"], name_len), c_float(var["lo"]),
                 c_float(var["hi"]), len(var["mfs"]), base, group, k))
        w("};")
        w("")

    ante_all, conse_all = [], []
    for i, (ante, conse, _w, _c) in enumerate(fis["rules"]):
        for slot, idx in enumerate(ante):
            ante_all.append((i, slot, idx))
        for slot, idx in enumerate(conse):
            conse_all.append((i, slot, idx))
    w("static const int16_t %s_ante[%d] = { %s };"
      % (base, len(ante_all), ", ".join(str(t[2]) for t in ante_all)))
    w("static const int16_t %s_conse[%d] = { %s };"
      % (base, len(conse_all), ", ".join(str(t[2]) for t in conse_all)))
    w("")
    n_in = len(fis["inputs"])
    n_out = len(fis["outputs"])
    w("static const fz_rule_t %s_rules[%d] = {" % (base, len(fis["rules"])))
    for i, (ante, conse, weight, conn) in enumerate(fis["rules"]):
        w("    { %d, %d, %s, %s_ante + %d, %s_conse + %d },"
          % (n_in, conn, c_float(weight), base, i * n_in, base, i * n_out))
    w("};")
    w("")

    sysd = fis["sys"]
    and_op = OPS[(sysd.get("andmethod") or "min").strip().lower()]
    or_op = OPS[(sysd.get("ormethod") or "max").strip().lower()]
    if fis["type"] == "sugeno":
        # section C.5: Sugeno implication is always prod, aggregation always sum
        imp_op, agg_op = "FZ_OP_PROD", "FZ_OP_SUM"
        defz = DEFUZZ[(sysd.get("defuzzmethod") or "wtaver").strip().lower()]
    else:
        imp_op = OPS[(sysd.get("impmethod") or "min").strip().lower()]
        agg_op = OPS[(sysd.get("aggmethod") or "max").strip().lower()]
        defz = DEFUZZ[(sysd.get("defuzzmethod") or "centroid").strip().lower()]

    w("FZ_GEN_UNUSED static const fz_fis_t fz_fis_%s = {" % base)
    # src/fz_types.h carries a char[FZ_NAME_LEN] name in fz_fis_t; CONTRACT.md §B
    # does not. Set FZ_FIS_HAS_NAME=0 to build against a §B-literal header.
    w("#if FZ_FIS_HAS_NAME")
    w("    %s," % c_str(fis["name"], name_len))
    w("#endif")
    w("    %s, %d, %d, %d," % ("FZ_SUGENO" if fis["type"] == "sugeno" else "FZ_MAMDANI",
                              n_in, n_out, len(fis["rules"])))
    w("    %s_in, %s_out, %s_rules," % (base, base, base))
    w("    %s, %s, %s, %s, %s, %d" % (and_op, or_op, imp_op, agg_op, defz, points))
    w("};")
    w("")
    w("#endif /* FZ_%s_FIS_H */" % up)
    return "\n".join(out) + "\n"


def selftest():
    """Slot encoding and name sanitizing -- the two places a silent bug hides."""
    # CONTRACT.md section B lists these encodings verbatim.
    cases = {"0": 0, "2": 200, "2.2": 220, "1.3": 130, "2.05": 205, "-2.2": -220}
    for token, want in cases.items():
        got = encode_token(token, 1, "selftest")
        assert got == want, "encode(%r) = %d, want %d" % (token, got, want)
        # Decode the way src/fz_eval.c does, and check we land on the same MF.
        mag = abs(got)
        assert mag // 100 == int(abs(float(token))), token
        hedge = (mag % 100) / 10
        if token == "2.05":
            assert hedge == 0.5, hedge
        if token == "2.2":
            assert hedge == 2.0, hedge
    assert c_ident("My Tipper 2.0") == "my_tipper_2_0"
    assert c_ident("2x") == "_2x"
    assert c_ident("") == "fis"
    assert c_str("abcdef", 4) == '"abc"', c_str("abcdef", 4)
    assert c_float(1) == "1.0f" and c_float(0.5) == "0.5f"
    print("fis2h selftest: ok")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compile a .fis file into a C header.")
    ap.add_argument("fis", nargs="?", help="input .fis file")
    ap.add_argument("--selftest", action="store_true",
                    help="check slot encoding and name handling, then exit")
    ap.add_argument("-o", "--output", help="output header (default <stem>_fis.h, '-' = stdout)")
    ap.add_argument("-n", "--name", help="C symbol base (default: [System] Name)")
    ap.add_argument("--points", type=int, default=DEFAULT_POINTS,
                    help="sample points for Mamdani defuzzification (default %d)" % DEFAULT_POINTS)
    ap.add_argument("--name-len", type=int, default=20,
                    help="FZ_NAME_LEN used for truncation (default 20)")
    ap.add_argument("--source", help="path recorded in the 'generated from' comment")
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if not args.fis:
        ap.error("a .fis file is required")
    if args.points < 2:
        ap.error("--points must be >= 2")
    if args.name_len < 2:
        ap.error("--name-len must be >= 2")

    try:
        fis = parse_fis(args.fis)
        base = c_ident(args.name or fis["name"])
        warnings = []
        text = emit(fis, base, args.name_len, args.points,
                    args.source or args.fis, warnings)
    except FisError as exc:
        sys.stderr.write("fis2h: %s\n" % exc)
        return 1

    for msg in warnings:
        sys.stderr.write("fis2h: warning: %s\n" % msg)
    if args.output in (None, ""):
        stem = os.path.splitext(os.path.basename(args.fis))[0]
        args.output = stem + "_fis.h"
    if args.output == "-":
        sys.stdout.write(text)
    else:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
