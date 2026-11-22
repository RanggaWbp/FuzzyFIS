# Verifying fz against MATLAB

Everything in `tests/golden/` is output of **real** runs of `tools/oracle.m` on
GNU Octave 10.3.0 + fuzzy-logic-toolkit 0.4.6. No number in this repo was typed
by hand. This page is how to (a) rebuild that data, (b) re-run the same thing in
real MATLAB, and (c) what is known to differ between the Octave toolkit and the
MATLAB Fuzzy Logic Toolbox.

```
tools/fis2json.py     reference reader + evaluator (stdlib only, IEEE double)
tools/oracle.m        Octave/MATLAB CSV oracle, also emits intermediates
tools/run_oracle.sh   one-shot Octave runner (micromamba + octenv)
tests/golden/*.fis    fixtures (copies of the toolkit demos + 5 stress models)
tests/golden/*.in.txt input grids, one row per line
tests/golden/*.csv    evalfis output, '%.17g', one row per input row
tests/golden/*.detail.json  rule_input / firing / rule_output / aggregate
tests/golden/make_golden.py rebuilds all of the above and cross-checks
```

## 1. What was run (Octave, reproducible)

```sh
# one file, print CSV to stdout
tools/run_oracle.sh /var/tmp/flt-src/inst/mamdani_tip_calculator.fis \
    tests/golden/mamdani_tip_calculator.in.txt 101

# same call the harness makes: CSV + intermediate JSON
tools/run_oracle.sh MODEL.fis INPUTS.txt 101 OUT.csv DETAIL.json

# all fixtures + cross-check against the Python reference
python3 tests/golden/make_golden.py
```

`run_oracle.sh` expands to

```sh
FLT_PATH=/var/tmp/flt-src/inst \
/home/ranggawbp/.hermes/cache/scratch/mm/bin/micromamba run -p /var/tmp/octenv \
  octave --no-gui --quiet \
  --eval "addpath('<repo>/tools'); \
          oracle('/abs/MODEL.fis','/abs/INPUTS.txt',101,'/abs/OUT.csv','/abs/DETAIL.json')"
```

## 2. Result of that run

13 FIS files, **1424 input rows**, `n_points = 101`, all Mamdani runs at the
toolkit default. Comparison: `fis2json.py` (independent double-precision Python
evaluator) vs the CSV that Octave printed.

| fixture | inputs | rows | max abs error |
|---|---|---|---|
| mamdani_tip_calculator | 2 | 121 | 6.8e-15 |
| sugeno_tip_calculator | 2 | 121 | 0 |
| linear_tip_calculator | 2 | 121 | 0 |
| heart_disease_risk | 2 | 121 | 0 |
| investment_portfolio | 2 | 121 | 0 |
| cubic_approximator | 1 | 21 | 0 |
| mf-gauss (gauss/gauss2/smf/gbell) | 2 | 441 | 4.4e-16 |
| mf-sigmoid (dsig/psig/sig/linsm/linzm) | 2 | 187 | 5.7e-14 |
| mf-zs (zmf/pimf, som) | 1 | 13 | 0 |
| defuzz-mom | 1 | 11 | 0 |
| defuzz-lom | 1 | 11 | 0 |
| hedges (2.3/1.2/0.75/±, sum/max) | 2 | 36 | 1.8e-15 |
| sugeno-linear2 (linear + constant, 2 outputs) | 2 | 99 | 0 |

Overall worst absolute error **5.7e-14** — the contract's parity budget is 3e-5,
so the Python reference and the Octave oracle are the same implementation to
within double-rounding. The whole `.detail.json` payload (rule_input, firing,
rule_output, aggregated output, singletons) matches on the last row of every
fixture at the same tolerance; `make_golden.py` exits non-zero above 1e-9.

Grids are `linspace` over each input range: 11 points per axis (121 rows) for the
2-input demos, 21 for `cubic_approximator` and both `mf-gauss` axes, 17x11 for
`mf-sigmoid`, 13 for `mf-zs`. `tests/golden/SUMMARY.json` carries the same table
in machine-readable form.

## 3. Running the same check in real MATLAB

Requires MATLAB with the **Fuzzy Logic Toolbox** (R2018b or newer; `readfis` and
the 4-output `evalfis` form are older than that, `-batch` needs R2019a).

```matlab
cd /path/to/embedded-fuzzy
addpath('tools')

% 1. one model, straight through
fis = readfis('tests/golden/mamdani_tip_calculator.fis');
out = evalfis([7 4], fis);            % expect [16.469667189796205  1.1646966718979612]

% 2. the same call the golden data came from
oracle('tests/golden/mamdani_tip_calculator.fis', ...
       'tests/golden/mamdani_tip_calculator.in.txt', 101, ...
       '/tmp/matlab_tip.csv', '/tmp/matlab_tip.detail.json');

% 3. compare against the Octave-generated CSV
a = csvread('tests/golden/mamdani_tip_calculator.csv');
b = csvread('/tmp/matlab_tip.csv');
fprintf('max abs diff = %g\n', max(abs(a(:) - b(:))));

% 4. intermediates, same field names as `fis2json.py --details`
d = jsondecode(fileread('/tmp/matlab_tip.detail.json'));
d.firing, d.rule_input, d.aggregate

% 5. library vs MATLAB, per output
%    (check_parity.py is the automated form; this is the manual one)
ref = a;                             % golden = reference
fprintf('max rel err = %g\n', max(abs(b(:) - ref(:)) ./ max(1, abs(ref(:)))));
```

Run every fixture at once:

```matlab
addpath('tools');
files = dir('tests/golden/*.fis');
for k = 1 : numel(files)
  base = fullfile('tests/golden', files(k).name(1:end-4));
  oracle([base '.fis'], [base '.in.txt'], 101, [base '.matlab.csv'], '');
  a = csvread([base '.csv']);
  b = csvread([base '.matlab.csv']);
  fprintf('%-24s %6d rows  max|matlab-octave| = %g\n', ...
          files(k).name, numel(b), max(abs(a(:) - b(:))));
end
```

Prediction, not measurement — this repo has no MATLAB here: expect 1e-13..1e-15
level differences on `centroid` models (both sides are double, but the summation
order inside `defuzz` is not guaranteed identical) and near-exact agreement on the
Sugeno models, whose output is a weighted average of a handful of numbers. The
`bisector` row will differ by the output range, not by a rounding error — see 4.4.
Anything larger is a real finding, and the measured Octave column in section 2 is
what it should be compared against.

## 4. Toolkit vs toolbox — what was actually observed

Measured on Octave 10.3.0 + fuzzy-logic-toolkit 0.4.6. Each item below is a
divergence you can see in this environment, with the decision the library takes.

### 4.1 Sugeno `linear` consequents: two code paths inside the toolkit

`evalmf_private` (the path used for plotting and for `evalmf(x, params, 'linear')`)
calls `eval_linear(x, params)`, which uses **only** `a = params(1)` and
`c = params(end)`, ignoring every coefficient in between:

```
evalmf(5, [1 2 3], 'linear')  ->  8      % a=1, c=3 -> 1*5+3, the 2 is dropped
dot([1 2 3], [5 1])           ->  15
```

`evalfis` does **not** use that path. `eval_rules_sugeno` computes the real dot
product `params * [user_input 1]'`. With three or more coefficients the two
answers are different numbers, and the golden data records which one `evalfis`
produces: `tests/golden/sugeno-linear2.fis` has `MF1 = 'linear',[2 -3 5]`, and for
input `[10 1]` the oracle's `rule_output` row 1 is `22`, i.e.
`2*10 + (-3)*1 + 5`, not `2*10 + 5 = 25`. The same value appears in
`sugeno-linear2.detail.json` and is reproduced by `fis2json.py` to 0.0.

**Decision: the library implements the dot product** (`p0*x0 + ... + pn-1*xn-1 + pn`,
CONTRACT §C.5). That is what MATLAB documents for `linear` output MFs and what
`evalfis` does. Never port `eval_linear`'s two-coefficient shortcut; it exists only
so the toolkit can draw a 2-D slice of an n-D surface.

### 4.2 Zero aggregation

| case | toolkit result |
|---|---|
| Mamdani, every rule weight 0 | `NaN NaN` (0/0 in `wtaver`) |
| Sugeno, every rule weight 0 | hard error: `next_agg_output(1,_): out of bound 0` |

MATLAB R2018b+ documents a fallback: when nothing fires, the output is the
midpoint of the output range.

**Decision (deliberate, documented divergence):** the C library returns the
**midpoint of the output range** for both FIS types — deterministic, allocation
free, and matching current MATLAB documentation. `evalfis`-compatible NaN is not
usable on an MCU (it poisons every downstream comparison). The oracle keeps
whatever the engine does, so a golden row can be `NaN`; `make_golden.py` treats
`NaN == NaN` as equal, and no fixture in the suite actually reaches this case.

### 4.3 `probor` is not a callable method name

`readfis` accepts `OrMethod='probor'` and `AggMethod='probor'`, but `evalfis`
resolves method names with `str2func`, and the toolkit ships no `probor.m`:

```
evalfis(...) -> error: invalid function handle, unable to find function for @probor
```

`algebraic_sum` is the same operation and does exist, so this repository's
fixtures use `algebraic_sum` (`mf-sigmoid.fis`). The library still accepts
`probor` as a method name (CONTRACT §E), because MATLAB does.

### 4.4 `bisector` returns an index, not an x value

`defuzz(x, y, 'bisector')` returns `midpoint`, a **1-based position in x**, while
`centroid`, `mom`, `som`, `lom` all return x values. The binary search itself is
correct — it finds the right sample — but it hands back the index:

```
x = linspace(0, 10, 101); y = triangle centred at 5
defuzz(x, y, 'bisector')  ->  55        % not the x value 5.4
x(55)                     ->  5.4

x = linspace(0, 30, 101); y = gaussian centred at 15
defuzz(x, y, 'bisector')  ->  51        % x(51) = 15, the right sample, wrong units
```

Measured on 1-D shapes with `x = linspace(0, 10, 11)`: `[0 0 0 1 1 1 0 0 0 0 0]`
→ `4`, `1:-0.05:0.5` → `4`, `0.5:0.05:1` → `6`. Each is the correct sample
position; none is an x value.

**Decision:** the library implements the real bisector (scan the cumulative
trapezoid area, return the first x whose left area reaches half the total,
0-based index into the sample vector, CONTRACT §C/E). No golden fixture uses
`bisector`, because the oracle cannot produce a correct value for it — this is
the one defuzzification method with **no** oracle coverage, and
`docs/COMPATIBILITY.md` should say so. If you compare library vs Octave on a
`bisector` FIS, expect a difference of the output range, not a numeric error:
translate the oracle's answer with `x(oracle_answer)` before comparing.

### 4.5 `mom` / `som` / `lom` depend on the exact `linspace` bits

`mom` averages every sample equal to the maximum. If the maximum is a plateau
(which it is whenever an aggregated output is clipped flat, i.e. any `min`
implication with `w = 1`), which samples are "equal" depends on whether the
sample grid reproduces the plateau edge exactly. Python's double-precision
`lo + k*(hi-lo)/(n-1)` and Octave's `linspace` disagree in the last bit for some
`(lo, hi, n)` — measured 27 of 181 samples across the grids used here — and that
one bit can add or remove a tie at the boundary.

Observed on a discarded fixture: `mom` returned 1.2 from the oracle and 1.17 from
Python because a sample at 2.7 evaluated to 0.49999999999999978 on one side and
0.5 on the other.

**Decision:** keep one-bit-sample divergences out of the golden set — both
`defuzz-mom` and `defuzz-lom` fixtures use a triangular consequent with a unique
sampled maximum, and `mf-zs` uses `som` on a shape with a single peak. The
library's `mom/som/lom` use the same equality test as MATLAB
(`y == max(y)`), so on real MATLAB data the tie set matches; what cannot be
guaranteed across toolchains is the sample grid itself. This is the reason
CONTRACT §H claims bit-exactness only for `n_points` and operators, never for
float32-vs-float64 or for grid construction.

### 4.6 Hedge rounding is `round()`, and the two engines round differently

The hedge comes from the rule token's fractional part: `hedge = round(100*(v-fix(v)))/10`
(CONTRACT §C.2). Octave's `round` is round-half-away-from-zero; MATLAB's `round`
is round-half-away-from-zero too (since R2006a it is *not* banker's rounding), so
these agree — but the input is a decimal literal, so `2.3` is really
`2.2999999999999998` and `100*(2.3-2)` is `29.999999999999996`:

```
token 2.25 -> hedge 2.5      (2.25 is exact, rounds up)
token 2.26 -> hedge 2.6
token 2.35 -> hedge 3.5      (2.35 - 2 = 0.34999999999999998)
```

The library copies the formula verbatim rather than "fixing" it, so a rule written
`-2.3` behaves exactly as it does in MATLAB. `hedges.fis` exercises 2.5, 1.2, 2.3,
1.1, 2.2, 3.4 and 0.75 and matches the oracle to 1.8e-15.

### 4.7 Input range is validated by the oracle, not by the library

`evalfis` rejects out-of-range inputs (`is_input_matrix` requires
`range(1) <= x <= range(2)`) and the run stops with
`evalfis's 1st argument must be a matrix of input values`. MATLAB errors the same
way. The C library does not clamp (CONTRACT §C.1) — it evaluates the MF formulas,
which are defined outside the range, and the caller is responsible for the range.
All golden grids stay inside the declared ranges.

### 4.8 `readfis` parsing constraints worth knowing

The toolkit's `readfis` is stricter than the CONTRACT §F grammar:

- `trimf` needs `a < b < c`, `trapmf`/`pimf` need `a < b <= c < d`, `smf`/`zmf`
  need `a < b`, `linzmf`/`linsmf` need `a <= b`, `gbellmf` needs `a != 0` and an
  **integer** `b`. A `trapmf` with `b == a` (a valid MATLAB shoulder) is rejected
  outright — `mf-sigmoid.fis` uses `[0 1 25 55]` and `[40 70 99 100]` for that
  reason.
- `readfis` looks for `Method = '...'` with exactly one space around `=`, so the
  fixture files use that spacing.
- Comments: `readfis` strips lines starting with `%`. The `##` header blocks that
  the toolkit's own demo `.fis` files carry parse fine (they are read as key/value
  misses, not as sections), and the copies in `tests/golden/` keep them verbatim
  so they are byte-identical to `/var/tmp/flt-src/inst/*.fis`.

None of this limits the library: `fis2json.py` implements the CONTRACT §F grammar,
which is a superset, and the C parser follows the contract, not the toolkit.

## 5. What to trust where

- **Oracle for behaviour**: `evalfis` output, `rule_input`, `rule_output`,
  aggregated output — all captured in `tests/golden/*.detail.json`.
- **Contract for formulas**: `fis2json.py` is the executable form of CONTRACT
  §C/§D/§E, including the degenerate-parameter guards (`a == b` → 0) that the
  toolkit does not have (it divides by zero and produces `NaN`).
- **Not covered by the oracle, by construction**: `bisector` (§4.4), zero
  aggregation (§4.2), out-of-range inputs, `gbellmf` with non-integer `b`, and
  degenerate MF parameters. Those are contract behaviour and are exercised by
  `tests/host/`, not by the golden CSVs.
