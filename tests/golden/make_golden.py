#!/usr/bin/env python3
"""make_golden.py -- regenerate tests/golden/ from a real Octave run.

Not part of the shipped tooling contract (that is fis2json.py / oracle.m); this
is the harness that produced the checked-in CSVs, kept so the numbers can be
rebuilt instead of trusted.  Every value comes from tools/oracle.m; nothing is
computed in Python here except the input grids.

    python3 tests/golden/make_golden.py [--n-points 101]

Cross-checks each row against fis2json.py's reference evaluator and fails if the
max abs error exceeds 1e-9.
"""

import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import fis2json  # noqa: E402

TOOLKIT = '/var/tmp/flt-src/inst'
OCTAVE_VERSION = 'GNU Octave 10.3.0 + fuzzy-logic-toolkit 0.4.6'
RUN_ORACLE = os.path.join(REPO, 'tools', 'run_oracle.sh')

# name: (fis path, [(lo, hi, n) per input]) -- grids follow MATLAB's linspace
CORPUS = {
    'mamdani_tip_calculator': (f'{TOOLKIT}/mamdani_tip_calculator.fis',
                               [(1, 10, 11), (1, 10, 11)]),
    'sugeno_tip_calculator': (f'{TOOLKIT}/sugeno_tip_calculator.fis',
                              [(1, 10, 11), (1, 10, 11)]),
    'linear_tip_calculator': (f'{TOOLKIT}/linear_tip_calculator.fis',
                              [(1, 10, 11), (1, 10, 11)]),
    'heart_disease_risk': (f'{TOOLKIT}/heart_disease_risk.fis',
                           [(0, 300, 11), (0, 100, 11)]),
    'cubic_approximator': (f'{TOOLKIT}/cubic_approximator.fis',
                           [(-5, 5, 21)]),
    'investment_portfolio': (f'{TOOLKIT}/investment_portfolio.fis',
                             [(20, 100, 11), (0, 10, 11)]),
}
# stress fixtures live in tests/golden/ itself
for _name in ('mf-gauss', 'mf-sigmoid', 'mf-zs', 'hedges', 'sugeno-linear2',
              'defuzz-mom', 'defuzz-lom'):
    _spec = json.load(open(os.path.join(HERE, _name + '.inputs.json')))
    CORPUS[_name] = (os.path.join(HERE, _name + '.fis'), _spec)


def grid(spec):
    rows = [[]]
    for lo, hi, n in spec:
        vals = [lo + k * (hi - lo) / (n - 1) for k in range(n)]
        rows = [r + [v] for r in rows for v in vals]
    return rows


def _maxdiff(a, b):
    """Max abs difference between matching nested lists/numbers."""
    if isinstance(a, list) or isinstance(b, list):
        if not isinstance(a, list) or not isinstance(b, list) or len(a) != len(b):
            raise SystemExit('shape mismatch: %r vs %r' % (str(a)[:60], str(b)[:60]))
        return max([_maxdiff(u, v) for u, v in zip(a, b)] + [0.0])
    if a is None or b is None:
        return 0.0
    if a != a and b != b:                    # both NaN
        return 0.0
    return abs(a - b)


def main(argv):
    n_points = 101
    if '--n-points' in argv:
        n_points = int(argv[argv.index('--n-points') + 1])
    worst = 0.0
    total = 0
    summary = []
    for name, (fis_path, spec) in sorted(CORPUS.items()):
        rows = grid(spec)
        base = os.path.join(HERE, name)
        in_path = base + '.in.txt'
        csv_path = base + '.csv'
        detail_path = base + '.detail.json'
        with open(in_path, 'w') as fh:
            for r in rows:
                fh.write(' '.join('%.17g' % v for v in r) + '\n')
        if os.path.abspath(fis_path) != os.path.abspath(base + '.fis'):
            shutil.copyfile(fis_path, base + '.fis')
        cmd = [RUN_ORACLE, base + '.fis', in_path, str(n_points), csv_path, detail_path]
        subprocess.run(cmd, check=True)

        fis = fis2json._load(base + '.fis')
        seen = 0
        err = 0.0
        det = json.load(open(detail_path))
        mine = fis2json.details(fis, det['inputs'], n_points)
        keys = ['output', 'firing', 'rule_input', 'rule_output']
        keys += ['fuzzy_output'] if det['type'] == 'mamdani' else ['singletons']
        err = max([err] + [_maxdiff(det[k], mine[k]) for k in keys])
        with open(csv_path) as fh:
            for row, line in zip(rows, fh.read().split()):
                if 'error' in line:
                    raise SystemExit('%s: oracle rejected a row: %s' % (name, line))
                got = [float(v) for v in line.split(',')]
                ref = fis2json.evaluate(fis, row, n_points)
                if len(got) != len(ref):
                    raise SystemExit('%s: %d vs %d outputs' % (name, len(got), len(ref)))
                for a, b in zip(got, ref):
                    d = abs(a - b)
                    if d > 1e-12 * max(1.0, abs(a)):     # ignore print rounding
                        err = max(err, d)
                seen += 1
        total += seen
        worst = max(worst, err)
        summary.append((name, seen, err))
        print('%-24s %4d rows  max|oracle-python| %.3e' % (name, seen, err))

    print('---')
    print('files: %d  rows: %d  worst abs error: %.3e' % (len(summary), total, worst))
    if worst > 1e-9:
        print('FAIL: oracle and fis2json disagree by more than 1e-9')
        return 1
    with open(os.path.join(HERE, 'SUMMARY.json'), 'w') as fh:
        json.dump({'generated_by': 'tests/golden/make_golden.py',
                   'octave': OCTAVE_VERSION,
                   'n_points': n_points,
                   'files': [{'fis': n, 'rows': c, 'max_abs_error': e}
                             for n, c, e in summary],
                   'rows_total': total,
                   'max_abs_error': worst}, fh, indent=2)
        fh.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
