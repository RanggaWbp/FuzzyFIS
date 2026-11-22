#!/usr/bin/env python3
"""fis2json.py -- independent .fis reader and double-precision reference evaluator.

Reference implementation for CONTRACT.md.  Stdlib only, no numpy.

  fis2json.py MODEL.fis [-o out.json]            canonical JSON (CONTRACT §H)
  fis2json.py MODEL.fis --eval in.txt [-n N]     CSV of evalfis() outputs
  fis2json.py MODEL.fis --details in.txt [-n N]  JSON with rule_input / firing /
                                                 rule_output / fuzzy_output
  fis2json.py --selftest

Golden numbers come from real Octave + fuzzy-logic-toolkit runs of tools/oracle.m;
see docs/MATLAB-VERIFY.md.  Floating point is IEEE double throughout.
"""

import json
import math
import re
import sys

DEFUZZ_METHODS = ('centroid', 'centroid_integral', 'bisector', 'mom', 'som',
                  'lom', 'wtaver', 'wtsum')
DEFAULT_POINTS = 101


# --------------------------------------------------------------------------
# parser (CONTRACT §F)
# --------------------------------------------------------------------------

def _num(tok):
    return float(tok)


def _floats(text):
    text = text.strip().strip('[]').strip()
    if not text:
        return []
    return [_num(t) for t in re.split(r'[\s,]+', text) if t]


def _unquote(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        return text[1:-1]
    return text


MF_RE = re.compile(r'^MF(\d+)\s*=\s*(.*?)\s*:\s*(.*?)\s*,\s*\[(.*)\]\s*$')


def parse_fis(text):
    """Parse .fis text into the canonical dict (same shape as the JSON output)."""
    fis = {'type': 'mamdani', 'and': 'min', 'or': 'max', 'imp': 'min',
           'agg': 'max', 'defuzz': 'centroid', 'n_points': DEFAULT_POINTS,
           'name': '', 'inputs': [], 'outputs': [], 'rules': []}
    syskeys = {'name': 'name', 'type': 'type', 'andmethod': 'and',
               'ormethod': 'or', 'impmethod': 'imp', 'aggmethod': 'agg',
               'defuzzmethod': 'defuzz'}
    section = None          # 'input' / 'output' / None
    index = 0
    var = None
    in_rules = False

    for raw in text.splitlines():
        line = raw.split('%')[0].split('#')[0].strip()
        if not line:
            continue
        if line.startswith('['):
            name = line.strip('[]').strip()
            in_rules = name.lower() == 'rules'
            if in_rules:
                section, var = None, None
            elif name.lower().startswith('input'):
                section, var, index = 'input', {'name': '', 'range': [0.0, 1.0],
                                               'mfs': []}, index + 1
                fis['inputs'].append(var)
            elif name.lower().startswith('output'):
                section, var, index = 'output', {'name': '', 'range': [0.0, 1.0],
                                                'mfs': []}, index + 1
                fis['outputs'].append(var)
            else:                                   # [System]
                section, var = None, None
            continue

        if in_rules:
            rule = _parse_rule(line)
            if rule:
                fis['rules'].append(rule)
            continue

        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        key, value = key.strip(), value.strip()

        if section is None:
            low = key.lower()
            if low == 'numinputs' or low == 'numoutputs' or low == 'numrules':
                continue
            if low in syskeys:
                fis[syskeys[low]] = _unquote(value)
            continue

        if key.lower().startswith('mf'):
            m = MF_RE.match(line)
            if not m:
                raise ValueError('bad MF line: %r' % raw)
            var['mfs'].append({'name': _unquote(m.group(2)),
                               'type': _unquote(m.group(3)),
                               'params': _floats(m.group(4))})
            continue

        if key.lower() == 'name':
            var['name'] = _unquote(value)
        elif key.lower() == 'range':
            var['range'] = _floats(value)

    return fis


def _parse_rule(line):
    conn = 1
    if ':' in line:
        line, c = line.rsplit(':', 1)
        conn = int(float(c.strip())) if c.strip() else 1
    weight = 1.0
    m = re.search(r'\(([-+0-9.eE]+)\)', line)
    if m:
        weight = _num(m.group(1))
        line = line[:m.start()] + line[m.end():]
    halves = line.split(',')
    if len(halves) < 2:
        return None
    return {'antecedent': [_num(t) for t in re.split(r'[\s]+', halves[0].strip()) if t],
            'consequent': [_num(t) for t in re.split(r'[\s]+', halves[1].strip()) if t],
            'weight': weight, 'conn': conn}


# --------------------------------------------------------------------------
# membership functions (CONTRACT §D, toolkit-exact)
# --------------------------------------------------------------------------

def _div(u, v):
    return u / v if v else 0.0


def _gauss(x, sig, c):
    return math.exp(-((x - c) ** 2) / (2 * sig * sig)) if sig else 0.0


def _clamp01(v):
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)


def mf_value(kind, p, x):
    """One membership value; contract's degenerate guards turn 0/0 into 0."""
    if kind == 'trimf':
        a, b, c = p[0], p[1], p[2]
        return max(0.0, min(1.0, _div(x - a, b - a), _div(c - x, c - b)))
    if kind == 'trapmf':
        a, b, c, d = p[0], p[1], p[2], p[3]
        return max(0.0, min(1.0, _div(x - a, b - a), _div(d - x, d - c)))
    if kind == 'gaussmf':
        return _gauss(x, p[0], p[1])
    if kind == 'gauss2mf':
        # toolkit (and MATLAB for sig1,c1 <= sig2,c2): product of the two halves
        f1 = _gauss(x, p[0], p[1]) if x <= p[1] else 1.0
        f2 = 1.0 if x <= p[3] else _gauss(x, p[2], p[3])
        return f1 * f2
    if kind == 'gbellmf':
        a, b, c = p[0], p[1], p[2]
        if not a:
            return 0.0
        return 1.0 / (1.0 + abs((x - c) / a) ** (2.0 * b))
    if kind == 'sigmf':
        return 1.0 / (1.0 + math.exp(-p[0] * (x - p[1])))
    if kind == 'dsigmf':
        v = (1.0 / (1.0 + math.exp(-p[0] * (x - p[1])))
             - 1.0 / (1.0 + math.exp(-p[2] * (x - p[3]))))
        return max(0.0, min(1.0, v))
    if kind == 'psigmf':
        return (1.0 / (1.0 + math.exp(-p[0] * (x - p[1])))
                * 1.0 / (1.0 + math.exp(-p[2] * (x - p[3]))))
    if kind == 'zmf':
        a, b = p[0], p[1]
        if x <= a:
            return 1.0
        if x <= (a + b) / 2.0:
            return 1.0 - 2.0 * _div(x - a, b - a) ** 2
        if x < b:
            return 2.0 * _div(x - b, b - a) ** 2
        return 0.0
    if kind == 'smf':
        a, b = p[0], p[1]
        if x <= a:
            return 0.0
        if x <= (a + b) / 2.0:
            return 2.0 * _div(x - a, b - a) ** 2
        if x < b:
            return 1.0 - 2.0 * _div(x - b, b - a) ** 2
        return 1.0
    if kind == 'pimf':
        a, b, c, d = p[0], p[1], p[2], p[3]
        if x <= a:
            return 0.0
        if x <= (a + b) / 2.0:
            return 2.0 * _div(x - a, b - a) ** 2
        if x < b:
            return 1.0 - 2.0 * _div(x - b, b - a) ** 2
        if x <= c:
            return 1.0
        if x <= (c + d) / 2.0:
            return 1.0 - 2.0 * _div(x - c, d - c) ** 2
        if x < d:
            return 2.0 * _div(x - d, d - c) ** 2
        return 0.0
    if kind == 'linsmf':
        a, b = p[0], p[1]
        if a == b:
            return 1.0 if x >= a else 0.0
        return _clamp01(_div(x - a, b - a))
    if kind == 'linzmf':
        a, b = p[0], p[1]
        if a == b:
            return 1.0 if x < a else 0.0
        return _clamp01(_div(b - x, b - a))
    if kind in ('constant', 'linear'):
        return 1.0             # no domain shape; Sugeno uses the params directly
    raise ValueError('unknown MF type %r' % kind)


# --------------------------------------------------------------------------
# operators (CONTRACT §E)
# --------------------------------------------------------------------------

def op2(kind, a, b):
    if kind == 'min':
        return min(a, b)
    if kind == 'max':
        return max(a, b)
    if kind in ('prod', 'algebraic_product'):
        return a * b
    if kind in ('sum',):
        return a + b
    if kind in ('probor', 'algebraic_sum'):
        return a + b - a * b
    if kind == 'bounded_sum':
        return min(1.0, a + b)
    if kind == 'einstein_product':
        return (a * b) / (2.0 - (a + b - a * b))
    if kind == 'einstein_sum':
        return (a + b) / (1.0 + a * b)
    if kind == 'hamacher_product':
        return 0.0 if (a == 0.0 and b == 0.0) else (a * b) / (a + b - a * b)
    if kind == 'hamacher_sum':
        return 1.0 if (a == 1.0 and b == 1.0) else (a + b - 2.0 * a * b) / (1.0 - a * b)
    raise ValueError('unknown operator %r' % kind)


def _fold(kind, values):
    """Left-to-right fold, toolkit vector_arg identities."""
    if not values:
        return 1.0
    if kind == 'min':
        return min(values)
    if kind == 'max':
        return max(values)
    start = 1.0 if kind in ('prod', 'algebraic_product', 'einstein_product',
                            'hamacher_product') else 0.0
    acc = start
    for v in values:
        acc = op2(kind, acc, v)
    return acc


def _round_half_away(v):
    return math.floor(v + 0.5) if v >= 0 else -math.floor(-v + 0.5)


def hedge_of(token):
    """(mf_index, hedge, not_flag) for a rule token like -2.3 (CONTRACT §C.2)."""
    neg = token < 0
    v = abs(token)
    idx = int(v)
    hedge = _round_half_away(100.0 * (v - idx)) / 10.0 if idx else 0.0
    return idx, hedge, neg


# --------------------------------------------------------------------------
# evaluation (CONTRACT §C)
# --------------------------------------------------------------------------

def _apply_hedge(kind, mu, hedge, neg):
    if kind not in ('constant', 'linear') and hedge:
        mu = mu ** hedge
    return 1.0 - mu if neg else mu


def fuzzify(fis, x):
    """Q x N matrix of matching degrees."""
    table = []
    for rule in fis['rules']:
        row = []
        for j, token in enumerate(rule['antecedent']):
            idx, hedge, neg = hedge_of(token)
            if idx == 0:
                row.append(0.0)                     # omitted variable
                continue
            mf = fis['inputs'][j]['mfs'][idx - 1]
            row.append(_apply_hedge(mf['type'], mf_value(mf['type'], mf['params'], x[j]),
                                    hedge, neg))
        table.append(row)
    return table


def firing(fis, rule_input):
    out = []
    for rule, row in zip(fis['rules'], rule_input):
        mus = [mu for mu, token in zip(row, rule['antecedent']) if token != 0]
        kind = fis['and'] if rule['conn'] == 1 else fis['or']
        out.append(rule['weight'] * _fold(kind, mus))
    return out


def _linspace(lo, hi, n):
    return [lo + k * (hi - lo) / (n - 1) for k in range(n)]


def _trapz(xs, ys):
    return sum((xs[i] - xs[i - 1]) * (ys[i - 1] + ys[i]) / 2.0
               for i in range(1, len(xs)))


def defuzz(xs, ys, method):
    if method in ('centroid', 'wtaver'):
        den = sum(ys)
        return sum(w * v for w, v in zip(ys, xs)) / den if den else float('nan')
    if method == 'wtsum':
        return sum(w * v for w, v in zip(ys, xs))
    if method == 'centroid_integral':
        den = _trapz(xs, ys)
        return _trapz(xs, [x * y for x, y in zip(xs, ys)]) / den if den else float('nan')
    if method == 'bisector':
        half = _trapz(xs, ys) / 2.0
        acc = 0.0
        for i in range(1, len(xs)):
            acc += (xs[i] - xs[i - 1]) * (ys[i - 1] + ys[i]) / 2.0
            if acc >= half:
                return xs[i]
        return xs[-1]
    top = max(ys)
    locs = [x for x, y in zip(xs, ys) if y == top]
    if method == 'mom':
        return sum(locs) / len(locs)
    if method == 'som':
        return min(locs)
    if method == 'lom':
        return max(locs)
    raise ValueError('unknown defuzz method %r' % method)


def eval_mamdani(fis, x, n_points=DEFAULT_POINTS):
    """Crisp outputs for one input row, plus per-rule fuzzy columns."""
    strengths = firing(fis, fuzzify(fis, x))
    outputs, columns, aggregated = [], [], []
    for j, var in enumerate(fis['outputs']):
        xs = _linspace(var['range'][0], var['range'][1], n_points)
        acc = [0.0] * n_points
        cols = []
        for rule, w in zip(fis['rules'], strengths):
            idx, hedge, neg = hedge_of(rule['consequent'][j])
            if w == 0.0 or idx == 0:
                cols.append([0.0] * n_points)
                continue
            mf = var['mfs'][idx - 1]
            ys = [_apply_hedge(mf['type'], mf_value(mf['type'], mf['params'], xv), hedge, neg)
                  for xv in xs]
            if fis['imp'] == 'min':
                ys = [min(w, y) for y in ys]
            elif fis['imp'] == 'prod':
                ys = [w * y for y in ys]
            else:
                ys = [op2(fis['imp'], w, y) for y in ys]
            cols.append(ys)
            acc = [op2(fis['agg'], a, y) for a, y in zip(acc, ys)]
        outputs.append(defuzz(xs, acc, fis['defuzz']))
        columns.append(cols)
        aggregated.append(acc)
    return outputs, strengths, columns, aggregated


def eval_sugeno(fis, x):
    strengths = firing(fis, fuzzify(fis, x))
    outputs, columns, aggregated = [], [], []
    for j, var in enumerate(fis['outputs']):
        singles, cols = [], []
        for rule, w in zip(fis['rules'], strengths):
            idx, hedge, neg = hedge_of(rule['consequent'][j])
            h = w
            if hedge:
                h = h ** (1.0 / hedge)
            if neg:
                h = 1.0 - h
            loc = 0.0
            if w != 0.0 and idx != 0:
                mf = var['mfs'][idx - 1]
                if mf['type'] == 'constant':
                    loc = mf['params'][0]
                else:
                    loc = sum(a * b for a, b in zip(mf['params'], list(x) + [1.0]))
                singles.append((loc, h))
                cols.append([loc, h])
            else:
                # toolkit leaves the (location, height) pair at 0 when the rule
                # is silent or the consequent slot is empty (`0` in the rule)
                cols.append([0.0, 0.0])
        singles.sort(key=lambda t: t[0])
        merged = []
        for loc, h in singles:
            if merged and merged[-1][0] == loc:
                merged[-1][1] = (merged[-1][1] + h if fis['agg'] == 'sum'
                                 else op2(fis['agg'], merged[-1][1], h))
            else:
                merged.append([loc, h])
        merged = [m for m in merged if m[1] != 0.0]
        if merged:
            outputs.append(defuzz([m[0] for m in merged], [m[1] for m in merged],
                                  fis['defuzz']))
        else:
            outputs.append(float('nan'))        # contract §C.5
        columns.append(cols)
        aggregated.append(merged)
    return outputs, strengths, columns, aggregated


def evaluate(fis, x, n_points=DEFAULT_POINTS):
    if fis['type'] == 'sugeno':
        return eval_sugeno(fis, x)[0]
    return eval_mamdani(fis, x, n_points)[0]


def _T(rows):
    """Transpose a list-of-lists (MATLAB matrices are serialised row-major)."""
    return [list(col) for col in zip(*rows)] if rows else []


def details(fis, x, n_points=DEFAULT_POINTS):
    """Intermediates laid out exactly like Octave's evalfis 4-output form."""
    if fis['type'] == 'sugeno':
        out, w, cols, agg = eval_sugeno(fis, x)
        # cols: [output][rule] -> (location, height); MATLAB: 2 x (Q*M) per output
        flattened = [[c[0] for per_out in cols for c in per_out],
                     [c[1] for per_out in cols for c in per_out]]
        return {'type': 'sugeno', 'n_points': n_points, 'inputs': list(x),
                'output': out, 'firing': w, 'rule_input': fuzzify(fis, x),
                'rule_output': flattened,
                # Octave returns each (location, height) matrix already
                # transposed, i.e. 2 x L; keep its layout verbatim.
                'singletons': [list(map(list, zip(*m))) for m in agg]}
    out, w, cols, agg = eval_mamdani(fis, x, n_points)
    flat = [a for per_out in cols for a in per_out]      # Q*M columns
    return {'type': 'mamdani', 'n_points': n_points, 'inputs': list(x),
            'output': out, 'firing': w, 'rule_input': fuzzify(fis, x),
            'rule_output': _T(flat), 'fuzzy_output': _T(agg)}


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _load(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        return parse_fis(fh.read())


def _read_inputs(path):
    rows = []
    with open(path, 'r', encoding='utf-8') as fh:
        for line in fh:
            line = line.split('#')[0].replace(',', ' ').strip()
            if line:
                rows.append([float(t) for t in line.split()])
    return rows


def _fmt(v):
    if math.isnan(v):
        return 'nan'
    return '%.17g' % v


def main(argv):
    import argparse

    ap = argparse.ArgumentParser(description='reference .fis reader / evaluator')
    ap.add_argument('fis', nargs='?', help='path to .fis')
    ap.add_argument('-o', dest='out', help='write canonical JSON here')
    ap.add_argument('-n', dest='n_points', type=int, default=DEFAULT_POINTS,
                    help='sample points for Mamdani (default %d)' % DEFAULT_POINTS)
    ap.add_argument('-i', dest='inputs', help='input file, one row per line')
    ap.add_argument('--eval', action='store_true', help='print CSV of outputs')
    ap.add_argument('--details', action='store_true',
                    help='print JSON with intermediates for the last input row')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv[1:])

    if a.selftest:
        return _selftest()
    if not a.fis:
        ap.print_help()
        return 2
    fis = _load(a.fis)

    if a.details:
        print(json.dumps(details(fis, _read_inputs(a.inputs)[-1], a.n_points)))
    elif a.eval:
        for row in _read_inputs(a.inputs):
            print(','.join(_fmt(v) for v in evaluate(fis, row, a.n_points)))
    else:
        canonical = {k: v for k, v in fis.items() if k != 'name'}   # CONTRACT §H
        text = json.dumps(canonical, indent=2) + '\n'
        if a.out:
            with open(a.out, 'w', encoding='utf-8') as fh:
                fh.write(text)
        else:
            sys.stdout.write(text)
    return 0


def _selftest():
    """Degenerate cases must not leak NaN/inf into MF values."""
    fis = parse_fis("""
[System]
Name='t'
Type='mamdani'
NumInputs=1
NumOutputs=1
NumRules=1
AndMethod='min'
OrMethod='max'
ImpMethod='min'
AggMethod='max'
DefuzzMethod='centroid'
[Input1]
Name='x'
Range=[0 10]
NumMFs=1
MF1='lo':'trimf',[0 5 10]
[Output1]
Name='y'
Range=[-1 1]
NumMFs=1
MF1='c':'trimf',[-1 0 1]
[Rules]
1, 1 (1) : 1
""")
    # symmetric consequence -> centroid is the MF centre
    assert abs(evaluate(fis, [5.0])[0]) < 1e-12, evaluate(fis, [5.0])
    # no rule fires -> 0/0, contract answers NaN
    assert math.isnan(evaluate(fis, [0.0])[0])
    # degenerate params (a == b) give 0, never ZeroDivisionError
    assert mf_value('trimf', [1.0, 1.0, 2.0], 1.0) == 0.0
    assert mf_value('gaussmf', [0.0, 1.0], 1.0) == 0.0
    assert hedge_of(-2.3) == (2, 3.0, True) and hedge_of(1.0) == (1, 0.0, False)
    assert op2('probor', 0.5, 0.5) == 0.75
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
